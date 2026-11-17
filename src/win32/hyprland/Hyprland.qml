pragma Singleton

import QtQuick
import Quickshell
import Quickshell.Io

// Presents komorebi in the shape of Quickshell's Hyprland singleton, so a
// configuration written against Hyprland shows komorebi's monitors, workspaces
// and windows and can switch between them.
//
// State comes from `komorebic state` once, then from komorebi's event pipe:
// this hosts a named pipe and asks komorebi to write notifications to it. Each
// notification carries the full state, which is applied to long lived objects
// so bindings held by the configuration stay valid.
//
// Hyprland workspace ids are global and komorebi's are per monitor, so ids are
// assigned across monitors in order: the first monitor's workspaces are 1..n,
// the second's follow on.
Singleton {
    id: root

    signal rawEvent(var event)

    readonly property bool usingLua: false
    readonly property string requestSocketPath: ""
    readonly property string eventSocketPath: ""

    property HyprlandMonitor focusedMonitor: null
    property HyprlandWorkspace focusedWorkspace: null
    property HyprlandToplevel activeToplevel: null

    readonly property ObjectList monitors: ObjectList {}
    readonly property ObjectList workspaces: ObjectList {}
    readonly property ObjectList toplevels: ObjectList {}

    // True once state has been received from komorebi at least once.
    property bool ready: false

    // Returned by monitorFor() until komorebi has reported its monitors, so
    // configurations that read a monitor at startup do not meet null. Bindings
    // that called monitorFor() are evaluated again once the real ones exist.
    readonly property HyprlandMonitor placeholderMonitor: HyprlandMonitor {}

    readonly property string pipeName: `quickshell-komorebi-${Quickshell.processId}-${Math.random().toString(36).slice(2, 8)}`

    property var monitorsByKey: ({})
    property var workspacesByKey: ({})
    property var toplevelsByKey: ({})
    property var commandQueue: []

    function monitorFor(screen: var): HyprlandMonitor {
        if (!screen)
            return focusedMonitor ?? monitors.values[0] ?? placeholderMonitor;

        // Qt names a screen after its model ("DELL U2723QE (1)") and komorebi
        // after its display device ("DISPLAY1"), so names cannot be compared.
        // Both report the panel's serial number, and both place a screen's
        // origin at the same point in the virtual desktop.
        const serial = screen.serialNumber ?? "";
        if (serial !== "") {
            const bySerial = monitors.values.find(m => m.serialNumber === serial);
            if (bySerial)
                return bySerial;
        }

        const byPosition = monitors.values.find(m => m.x === screen.x && m.y === screen.y);
        if (byPosition)
            return byPosition;

        return focusedMonitor ?? monitors.values[0] ?? placeholderMonitor;
    }

    function refreshMonitors(): void {
        refresh();
    }

    function refreshWorkspaces(): void {
        refresh();
    }

    function refreshToplevels(): void {
        refresh();
    }

    function refresh(): void {
        if (!stateQuery.running)
            stateQuery.running = true;
    }

    // Accepts the subset of Hyprland dispatchers that has a komorebi equivalent.
    function dispatch(request: string): void {
        const parts = request.trim().split(/\s+/);
        const name = parts[0];
        const arg = parts.slice(1).join(" ");

        if (name === "workspace") {
            if (/^r?[+-]\d+$/.test(arg)) {
                komorebic(["cycle-workspace", arg.includes("-") ? "previous" : "next"]);
                return;
            }

            const ws = workspaceById(parseInt(arg));
            if (ws)
                komorebic(["focus-monitor-workspace", String(ws.monitorIndex), String(ws.workspaceIndex)]);
        } else if (name === "movetoworkspace" || name === "movetoworkspacesilent") {
            const ws = workspaceById(parseInt(arg));
            if (ws)
                komorebic([name === "movetoworkspace" ? "move-to-monitor-workspace" : "send-to-monitor-workspace", String(ws.monitorIndex), String(ws.workspaceIndex)]);
        } else if (name === "killactive") {
            komorebic(["close"]);
        } else if (name === "togglefloating") {
            komorebic(["toggle-float"]);
        } else if (name === "fullscreen") {
            komorebic(["toggle-maximize"]);
        } else {
            console.warn(`Hyprland.dispatch has no komorebi equivalent for "${request}"`);
        }
    }

    function workspaceById(id: int): HyprlandWorkspace {
        return workspaces.values.find(w => w.id === id) ?? null;
    }

    function komorebic(args: list<string>): void {
        commandQueue.push(["komorebic"].concat(args));
        runNextCommand();
    }

    function runNextCommand(): void {
        if (commandRunner.running || commandQueue.length === 0)
            return;

        commandRunner.exec(commandQueue.shift());
    }

    function sameList(a: var, b: var): bool {
        if (a.length !== b.length)
            return false;

        for (let i = 0; i < a.length; i++) {
            if (a[i] !== b[i])
                return false;
        }

        return true;
    }

    function handleNotification(line: string): void {
        if (line.trim() === "")
            return;

        let message;
        try {
            message = JSON.parse(line);
        } catch (e) {
            console.warn("Could not parse komorebi notification:", e);
            return;
        }

        if (message.state)
            applyState(message.state);

        const type = message.event?.type ?? "";
        if (type !== "")
            rawEvent(eventComponent.createObject(root, {
                name: type.toLowerCase(),
                data: JSON.stringify(message.event.content ?? null)
            }));
    }

    function windowsOf(workspace: var): var {
        const windows = [];
        const containers = workspace.containers?.elements ?? [];

        for (let ci = 0; ci < containers.length; ci++) {
            const container = containers[ci];
            const inFocusedContainer = ci === workspace.containers.focused;
            const elements = container.windows?.elements ?? [];

            for (let wi = 0; wi < elements.length; wi++) {
                windows.push({
                    window: elements[wi],
                    floating: false,
                    focused: inFocusedContainer && wi === container.windows.focused
                });
            }
        }

        if (workspace.monocle_container) {
            const elements = workspace.monocle_container.windows?.elements ?? [];
            for (let wi = 0; wi < elements.length; wi++) {
                windows.push({
                    window: elements[wi],
                    floating: false,
                    focused: wi === workspace.monocle_container.windows.focused
                });
            }
        }

        if (workspace.maximized_window) {
            windows.push({
                window: workspace.maximized_window,
                floating: false,
                focused: true
            });
        }

        for (const window of workspace.floating_windows?.elements ?? []) {
            windows.push({
                window: window,
                floating: true,
                focused: false
            });
        }

        return windows;
    }

    function applyState(state: var): void {
        const seenMonitors = {};
        const seenWorkspaces = {};
        const seenToplevels = {};
        const monitorList = [];
        const workspaceList = [];
        const toplevelList = [];

        let nextWorkspaceId = 1;
        let newFocusedMonitor = null;
        let newFocusedWorkspace = null;
        let newActiveToplevel = null;

        const komorebiMonitors = state.monitors?.elements ?? [];

        for (let mi = 0; mi < komorebiMonitors.length; mi++) {
            const km = komorebiMonitors[mi];
            const monitorKey = String(km.id);
            const monitorFocused = mi === state.monitors.focused;

            let monitor = monitorsByKey[monitorKey];
            if (!monitor) {
                monitor = monitorComponent.createObject(root);
                monitorsByKey[monitorKey] = monitor;
            }
            seenMonitors[monitorKey] = true;
            monitorList.push(monitor);

            // komorebi stores width and height in the right and bottom fields.
            monitor.id = mi;
            monitor.name = km.name;
            monitor.description = km.device ?? "";
            monitor.serialNumber = km.serial_number_id ?? "";
            monitor.x = km.size.left;
            monitor.y = km.size.top;
            monitor.width = km.size.right;
            monitor.height = km.size.bottom;
            monitor.focused = monitorFocused;

            let activeWorkspace = null;
            const komorebiWorkspaces = km.workspaces?.elements ?? [];

            for (let wi = 0; wi < komorebiWorkspaces.length; wi++) {
                const kw = komorebiWorkspaces[wi];
                const workspaceKey = `${monitorKey}:${wi}`;
                const workspaceId = nextWorkspaceId++;
                const workspaceActive = wi === km.workspaces.focused;

                let workspace = workspacesByKey[workspaceKey];
                if (!workspace) {
                    workspace = workspaceComponent.createObject(root);
                    workspacesByKey[workspaceKey] = workspace;
                }
                seenWorkspaces[workspaceKey] = true;
                workspaceList.push(workspace);

                const workspaceName = kw.name ?? String(workspaceId);
                const workspaceToplevels = [];

                for (const entry of windowsOf(kw)) {
                    const kwin = entry.window;
                    const toplevelKey = String(kwin.hwnd);
                    const activated = monitorFocused && workspaceActive && entry.focused;

                    let toplevel = toplevelsByKey[toplevelKey];
                    if (!toplevel) {
                        toplevel = toplevelComponent.createObject(root);
                        toplevelsByKey[toplevelKey] = toplevel;
                    }
                    seenToplevels[toplevelKey] = true;
                    workspaceToplevels.push(toplevel);
                    toplevelList.push(toplevel);

                    const address = kwin.hwnd.toString(16);
                    const appClass = (kwin.exe ?? "").replace(/\.exe$/i, "");

                    toplevel.address = address;
                    toplevel.title = kwin.title ?? "";
                    toplevel.activated = activated;
                    toplevel.workspace = workspace;
                    toplevel.monitor = monitor;
                    toplevel.lastIpcObject = {
                        address: `0x${address}`,
                        class: appClass,
                        initialClass: appClass,
                        title: kwin.title ?? "",
                        initialTitle: kwin.title ?? "",
                        mapped: true,
                        hidden: false,
                        floating: entry.floating,
                        pinned: false,
                        fullscreen: 0,
                        xwayland: false,
                        pid: 0,
                        monitor: mi,
                        workspace: {
                            id: workspaceId,
                            name: workspaceName
                        },
                        at: [kwin.rect?.left ?? 0, kwin.rect?.top ?? 0],
                        size: [kwin.rect?.right ?? 0, kwin.rect?.bottom ?? 0],
                        tags: []
                    };

                    if (activated)
                        newActiveToplevel = toplevel;
                }

                const hasFullscreen = !!kw.maximized_window || !!kw.monocle_container;

                workspace.id = workspaceId;
                workspace.name = workspaceName;
                workspace.monitorIndex = mi;
                workspace.workspaceIndex = wi;
                workspace.active = workspaceActive;
                workspace.focused = workspaceActive && monitorFocused;
                workspace.hasFullscreen = hasFullscreen;
                workspace.monitor = monitor;
                workspace.lastIpcObject = {
                    id: workspaceId,
                    name: workspaceName,
                    monitor: km.name,
                    monitorID: mi,
                    windows: workspaceToplevels.length,
                    hasfullscreen: hasFullscreen
                };

                if (!sameList(workspace.toplevels.values, workspaceToplevels))
                    workspace.toplevels.values = workspaceToplevels;

                if (workspaceActive)
                    activeWorkspace = workspace;
            }

            monitor.activeWorkspace = activeWorkspace;
            monitor.lastIpcObject = {
                id: mi,
                name: km.name,
                description: km.device ?? "",
                focused: monitorFocused,
                x: km.size.left,
                y: km.size.top,
                width: km.size.right,
                height: km.size.bottom,
                scale: 1,
                activeWorkspace: {
                    id: activeWorkspace?.id ?? 0,
                    name: activeWorkspace?.name ?? ""
                },
                specialWorkspace: {
                    id: 0,
                    name: ""
                }
            };

            if (monitorFocused) {
                newFocusedMonitor = monitor;
                newFocusedWorkspace = activeWorkspace;
            }
        }

        if (!sameList(monitors.values, monitorList))
            monitors.values = monitorList;
        if (!sameList(workspaces.values, workspaceList))
            workspaces.values = workspaceList;
        if (!sameList(toplevels.values, toplevelList))
            toplevels.values = toplevelList;

        focusedMonitor = newFocusedMonitor;
        focusedWorkspace = newFocusedWorkspace;
        activeToplevel = newActiveToplevel;

        // Objects are only destroyed once nothing above refers to them.
        for (const [seen, byKey] of [[seenToplevels, toplevelsByKey], [seenWorkspaces, workspacesByKey], [seenMonitors, monitorsByKey]]) {
            for (const key of Object.keys(byKey)) {
                if (!seen[key]) {
                    byKey[key].destroy();
                    delete byKey[key];
                }
            }
        }

        ready = true;
    }

    Component {
        id: monitorComponent

        HyprlandMonitor {}
    }

    Component {
        id: workspaceComponent

        HyprlandWorkspace {}
    }

    Component {
        id: toplevelComponent

        HyprlandToplevel {}
    }

    Component {
        id: eventComponent

        HyprlandEvent {}
    }

    // The pipe komorebi writes notifications to, one JSON document per line.
    SocketServer {
        active: true
        path: root.pipeName

        handler: Socket {
            parser: SplitParser {
                onRead: line => root.handleNotification(line)
            }
        }
    }

    // Gives the pipe a moment to start listening before komorebi connects.
    Timer {
        interval: 300
        running: true
        onTriggered: {
            root.refresh();
            root.komorebic(["subscribe-pipe", root.pipeName]);
        }
    }

    Process {
        id: stateQuery

        command: ["komorebic", "state"]

        stdout: StdioCollector {
            onStreamFinished: {
                if (text.trim() === "")
                    return;

                try {
                    root.applyState(JSON.parse(text));
                } catch (e) {
                    console.warn("Could not parse komorebi state:", e);
                }
            }
        }
    }

    Process {
        id: commandRunner

        onRunningChanged: {
            if (!running)
                root.runNextCommand();
        }
    }
}
