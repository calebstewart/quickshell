# Quickshell.Services.Notifications on Windows, mirroring the system's toasts.
qt_add_library(quickshell-win32-notifications STATIC
	notifications.cpp
	worker.cpp
)

qt_add_qml_module(quickshell-win32-notifications
	URI Quickshell.Services.Notifications
	VERSION 0.1
	DEPENDENCIES QtQuick
)

qs_add_module_deps_light(quickshell-win32-notifications quickshell-core)

install_qml_module(quickshell-win32-notifications)

# The Windows Runtime projection is header only. These are what it calls into.
target_link_libraries(quickshell-win32-notifications PRIVATE Qt::Quick runtimeobject ole32 oleaut32)

qs_module_pch(quickshell-win32-notifications SET large)

# The Windows Runtime headers have to come before Qt's, which the precompiled
# header would put first.
set_source_files_properties(worker.cpp PROPERTIES SKIP_PRECOMPILE_HEADERS ON)

target_link_libraries(quickshell PRIVATE quickshell-win32-notificationsplugin)
