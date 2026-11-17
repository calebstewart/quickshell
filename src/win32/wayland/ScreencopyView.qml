import QtQuick

// Stand-in: there is no capture backend on Windows yet, so this shows nothing.
Item {
    property var captureSource: null
    property bool live: false
    property bool paintCursors: false
    property size constraintSize: Qt.size(0, 0)
    readonly property bool hasContent: false
    readonly property size sourceSize: Qt.size(0, 0)

    signal stopped

    function captureFrame(): void {}
}
