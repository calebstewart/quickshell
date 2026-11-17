import QtQuick

// A komorebi monitor in the shape of a Hyprland monitor. Filled in by Hyprland.qml.
QtObject {
    property int id: 0
    property string name: ""
    property string description: ""
    // Used to match this monitor to a screen. Not part of the Hyprland shape.
    property string serialNumber: ""
    property int x: 0
    property int y: 0
    property int width: 0
    property int height: 0
    property real scale: 1
    property bool focused: false
    property HyprlandWorkspace activeWorkspace: null
    property var lastIpcObject: ({
            specialWorkspace: {
                name: ""
            },
            activeWorkspace: {
                id: 0,
                name: ""
            }
        })
}
