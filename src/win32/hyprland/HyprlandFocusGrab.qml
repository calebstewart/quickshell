import QtQuick
import Quickshell.Win32

// A focus grab on Hyprland keeps input within a set of windows and is cleared
// when the user clicks elsewhere. Windows cannot keep input in, but it can be
// noticed when the user turns away: while active, cleared is emitted on a
// press outside this program's windows or when another program's window comes
// to the front. Unlike on Hyprland, that press still reaches what was clicked.
//
// The windows list is accepted and not used: every window of this program
// counts as inside.
QtObject {
    id: root

    property bool active: false
    property var windows: []

    signal cleared

    readonly property FocusGrabWatcher watcher: FocusGrabWatcher {
        active: root.active
        onDismissed: root.cleared()
    }
}
