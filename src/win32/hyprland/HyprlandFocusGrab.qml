import QtQuick

// Stand-in: there is no focus grab on Windows, so this never becomes active.
QtObject {
    property bool active: false
    property var windows: []

    signal cleared
}
