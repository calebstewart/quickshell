import QtQuick

// Stand-in: authentication is never available.
QtObject {
	property string config: "login"
	property string configDirectory: ""
	property string user: ""
	property bool active: false
	property string message: ""
	property bool messageIsError: false
	property bool responseRequired: false
	property bool responseVisible: false

	signal completed(int result)
	signal error(int error)
	signal pamMessage

	function start(): bool {
		return false;
	}

	function abort(): void {}
	function respond(response: string): void {}
}
