import QtQuick

// Stand-in: Windows has no way for an application to provide the session lock,
// so the lock never becomes active and no surfaces are created.
QtObject {
    property bool locked: false
    readonly property bool secure: false
    default property Component surface
}
