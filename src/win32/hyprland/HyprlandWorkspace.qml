import QtQuick

// A komorebi workspace in the shape of a Hyprland workspace. Filled in by Hyprland.qml.
QtObject {
    id: root

    property int id: 0
    property string name: ""
    property bool active: false
    property bool focused: false
    property bool urgent: false
    property bool hasFullscreen: false
    property HyprlandMonitor monitor: null
    property ObjectList toplevels: ObjectList {}
    property var lastIpcObject: ({
            windows: 0
        })

    // Where this workspace lives in komorebi, which addresses workspaces per monitor.
    property int monitorIndex: 0
    property int workspaceIndex: 0

    function activate(): void {
        Hyprland.dispatch(`workspace ${root.id}`);
    }
}
