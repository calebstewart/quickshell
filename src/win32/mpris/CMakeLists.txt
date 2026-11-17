# Quickshell.Services.Mpris on Windows, backed by the system media sessions.
qt_add_library(quickshell-win32-mpris STATIC
	mpris.cpp
	worker.cpp
)

qt_add_qml_module(quickshell-win32-mpris
	URI Quickshell.Services.Mpris
	VERSION 0.1
	DEPENDENCIES QtQuick
)

qs_add_module_deps_light(quickshell-win32-mpris quickshell-core)

install_qml_module(quickshell-win32-mpris)

# The Windows Runtime projection is header only. These are what it calls into.
target_link_libraries(quickshell-win32-mpris PRIVATE Qt::Quick runtimeobject ole32 oleaut32)

qs_module_pch(quickshell-win32-mpris SET large)

# The Windows Runtime headers have to come before Qt's, which the precompiled
# header would put first.
set_source_files_properties(worker.cpp PROPERTIES SKIP_PRECOMPILE_HEADERS ON)

target_link_libraries(quickshell PRIVATE quickshell-win32-mprisplugin)
