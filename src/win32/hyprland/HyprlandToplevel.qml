import QtQuick

// A window managed by komorebi in the shape of a Hyprland toplevel. Filled in by Hyprland.qml.
QtObject {
    // The window handle in hexadecimal, without a prefix, as Hyprland formats addresses.
    property string address: ""
    property string title: ""
    property bool activated: false
    property bool urgent: false
    property HyprlandWorkspace workspace: null
    property HyprlandMonitor monitor: null
    // There is no Wayland toplevel to capture or manage on Windows.
    property var wayland: null
    property var lastIpcObject: ({})
}
