import QtQuick

// Stand-in: never reports idle.
QtObject {
    property bool enabled: true
    property real timeout: 0
    property bool respectInhibitors: true
    readonly property bool isIdle: false
}
