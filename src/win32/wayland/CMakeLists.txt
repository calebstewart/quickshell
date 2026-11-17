# Stand-ins for Quickshell.Wayland, so configurations written for Wayland load.
qt_add_library(quickshell-win32-wayland STATIC
	layershell.cpp
)

qt_add_qml_module(quickshell-win32-wayland
	URI Quickshell.Wayland
	VERSION 0.1
	DEPENDENCIES QtQuick
	QML_FILES
		ScreencopyView.qml
		WlSessionLock.qml
		WlSessionLockSurface.qml
		IdleInhibitor.qml
		IdleMonitor.qml
)

qs_add_module_deps_light(quickshell-win32-wayland quickshell-core quickshell-window)

install_qml_module(quickshell-win32-wayland)

target_link_libraries(quickshell-win32-wayland PRIVATE Qt::Quick)

qs_module_pch(quickshell-win32-wayland SET large)

target_link_libraries(quickshell PRIVATE quickshell-win32-waylandplugin)
