# Quickshell.Services.Pipewire on Windows, backed by Core Audio.
qt_add_library(quickshell-win32-pipewire STATIC
	audio.cpp
)

qt_add_qml_module(quickshell-win32-pipewire
	URI Quickshell.Services.Pipewire
	VERSION 0.1
	DEPENDENCIES QtQuick
)

qs_add_module_deps_light(quickshell-win32-pipewire quickshell-core)

install_qml_module(quickshell-win32-pipewire)

target_link_libraries(quickshell-win32-pipewire PRIVATE Qt::Quick ole32)

qs_module_pch(quickshell-win32-pipewire SET large)

target_link_libraries(quickshell PRIVATE quickshell-win32-pipewireplugin)
