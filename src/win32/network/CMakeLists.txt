# Quickshell.Networking on Windows, backed by the adapter list and the WLAN service.
qt_add_library(quickshell-win32-network STATIC
	network.cpp
)

qt_add_qml_module(quickshell-win32-network
	URI Quickshell.Networking
	VERSION 0.1
	DEPENDENCIES QtQuick
)

qs_add_module_deps_light(quickshell-win32-network quickshell-core)

install_qml_module(quickshell-win32-network)

target_link_libraries(quickshell-win32-network PRIVATE Qt::Quick iphlpapi wlanapi ws2_32 ole32)

qs_module_pch(quickshell-win32-network SET large)

# winsock2.h has to come before windows.h, which the precompiled header may
# already have brought in.
set_source_files_properties(network.cpp PROPERTIES SKIP_PRECOMPILE_HEADERS ON)

target_link_libraries(quickshell PRIVATE quickshell-win32-networkplugin)
