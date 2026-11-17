# Quickshell.Hyprland on Windows, backed by the komorebi tiling window manager.
set_source_files_properties(Hyprland.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(quickshell-win32-hyprland
	STATIC
	URI Quickshell.Hyprland
	VERSION 0.1
	DEPENDENCIES QtQuick
	QML_FILES
		Hyprland.qml
		HyprlandMonitor.qml
		HyprlandWorkspace.qml
		HyprlandToplevel.qml
		HyprlandEvent.qml
		HyprlandFocusGrab.qml
		GlobalShortcut.qml
		ObjectList.qml
)

qs_add_module_deps_light(quickshell-win32-hyprland quickshell-core quickshell-io quickshell-win32)

install_qml_module(quickshell-win32-hyprland)

target_link_libraries(quickshell-win32-hyprland PRIVATE Qt::Quick)

target_link_libraries(quickshell PRIVATE quickshell-win32-hyprlandplugin)
