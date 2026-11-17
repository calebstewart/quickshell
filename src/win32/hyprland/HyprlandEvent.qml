import QtQuick

// An event from komorebi, named after its notification type in lower case.
// The data is the notification content as JSON, so parse() returns nothing.
QtObject {
    property string name: ""
    property string data: ""

    function parse(argumentCount: int): list<string> {
        return [];
    }
}
