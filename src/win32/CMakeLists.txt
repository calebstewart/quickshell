qt_add_library(quickshell-win32 STATIC
	panel_window.cpp
)

qt_add_qml_module(quickshell-win32
	URI Quickshell.Win32
	VERSION 0.1
	DEPENDENCIES QtQuick
)

qs_add_module_deps_light(quickshell-win32 quickshell-core quickshell-window)

install_qml_module(quickshell-win32)

add_library(quickshell-win32-init OBJECT init.cpp)

target_link_libraries(quickshell-win32 PRIVATE Qt::Quick shell32 user32)
target_link_libraries(quickshell-win32-init PRIVATE Qt::Quick Qt::Qml)

qs_module_pch(quickshell-win32 SET large)

target_link_libraries(quickshell PRIVATE quickshell-win32plugin quickshell-win32-init)

add_subdirectory(wayland)
add_subdirectory(hyprland)
add_subdirectory(systemtray)
add_subdirectory(pipewire)
add_subdirectory(mpris)
add_subdirectory(upower)
add_subdirectory(bluetooth)
add_subdirectory(notifications)
