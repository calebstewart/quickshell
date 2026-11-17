# Quickshell.Services.UPower on Windows, backed by the system power status.
qt_add_library(quickshell-win32-upower STATIC
	power.cpp
)

qt_add_qml_module(quickshell-win32-upower
	URI Quickshell.Services.UPower
	VERSION 0.1
	DEPENDENCIES QtQuick
)

qs_add_module_deps_light(quickshell-win32-upower quickshell-core)

install_qml_module(quickshell-win32-upower)

target_link_libraries(quickshell-win32-upower PRIVATE Qt::Quick)

qs_module_pch(quickshell-win32-upower SET large)

target_link_libraries(quickshell PRIVATE quickshell-win32-upowerplugin)
