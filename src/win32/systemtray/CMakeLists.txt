# Quickshell.Services.SystemTray on Windows, backed by the notification area.
qt_add_library(quickshell-win32-systemtray STATIC
	tray.cpp
)

qt_add_qml_module(quickshell-win32-systemtray
	URI Quickshell.Services.SystemTray
	VERSION 0.1
	DEPENDENCIES QtQuick
)

qs_add_module_deps_light(quickshell-win32-systemtray quickshell-core)

install_qml_module(quickshell-win32-systemtray)

target_link_libraries(quickshell-win32-systemtray PRIVATE Qt::Quick user32 shell32)

qs_module_pch(quickshell-win32-systemtray SET large)

target_link_libraries(quickshell PRIVATE quickshell-win32-systemtrayplugin)
