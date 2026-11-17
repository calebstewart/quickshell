# Stand-ins for Quickshell.Services.Pam, so configurations with a lock screen
# load. Windows does not let a program authenticate a user this way, so
# authentication never starts.
qt_add_qml_module(quickshell-win32-pam
	STATIC
	URI Quickshell.Services.Pam
	VERSION 0.1
	DEPENDENCIES QtQuick
	QML_FILES
		PamContext.qml
		PamResult.qml
		PamError.qml
)

install_qml_module(quickshell-win32-pam)

target_link_libraries(quickshell-win32-pam PRIVATE Qt::Quick)

target_link_libraries(quickshell PRIVATE quickshell-win32-pamplugin)
