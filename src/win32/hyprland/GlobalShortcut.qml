import QtQuick

// Stand-in: no hotkey is registered, so pressed and released never fire.
QtObject {
    property string appid: "quickshell"
    property string name: ""
    property string description: ""
    property string triggerDescription: ""

    signal pressed
    signal released
}
