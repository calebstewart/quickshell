# Quickshell.Bluetooth on Windows, backed by the Bluetooth radio and paired devices.
qt_add_library(quickshell-win32-bluetooth STATIC
	bluetooth.cpp
	worker.cpp
)

qt_add_qml_module(quickshell-win32-bluetooth
	URI Quickshell.Bluetooth
	VERSION 0.1
	DEPENDENCIES QtQuick
)

qs_add_module_deps_light(quickshell-win32-bluetooth quickshell-core)

install_qml_module(quickshell-win32-bluetooth)

# The Windows Runtime projection is header only. These are what it calls into.
target_link_libraries(quickshell-win32-bluetooth PRIVATE Qt::Quick runtimeobject ole32 oleaut32)

qs_module_pch(quickshell-win32-bluetooth SET large)

# The Windows Runtime headers have to come before Qt's, which the precompiled
# header would put first.
set_source_files_properties(worker.cpp PROPERTIES SKIP_PRECOMPILE_HEADERS ON)

target_link_libraries(quickshell PRIVATE quickshell-win32-bluetoothplugin)
