import QtQuick

// Stand-in: never instantiated, as WlSessionLock never locks.
Item {
    property color color: "white"
    property var screen: null
    readonly property Item contentItem: this
}
