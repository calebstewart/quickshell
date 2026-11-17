qt_add_library(quickshell-widgets STATIC
	cliprect.cpp
	wrapper.cpp
	marginwrapper.cpp
)

qt_add_qml_module(quickshell-widgets
	URI Quickshell.Widgets
	VERSION 0.1
	QML_FILES
		IconImage.qml
		ClippingRectangle.qml
		WrapperItem.qml
		WrapperMouseArea.qml
		WrapperRectangle.qml
		ClippingWrapperRectangle.qml
		ClippingWrapperRectangleInternal.qml
)

# Qt Quick renders through Direct3D on Windows, which needs the HLSL variant.
# PRECOMPILE is left out there, as it would need the fxc compiler.
if (WIN32)
	set(CLIPRECT_SHADER_OPTIONS NOMSL BATCHABLE OPTIMIZED QUIET)
else()
	set(CLIPRECT_SHADER_OPTIONS NOHLSL NOMSL BATCHABLE PRECOMPILE OPTIMIZED QUIET)
endif()

qt6_add_shaders(quickshell-widgets "widgets-cliprect"
	${CLIPRECT_SHADER_OPTIONS}
	PREFIX "/Quickshell/Widgets"
	FILES shaders/cliprect.frag
	OUTPUTS shaders/cliprect.frag.qsb
)

qt6_add_shaders(quickshell-widgets "widgets-cliprect-ub"
	${CLIPRECT_SHADER_OPTIONS}
	PREFIX "/Quickshell/Widgets"
	FILES shaders/cliprect.frag
	OUTPUTS shaders/cliprect-ub.frag.qsb
	DEFINES CONTENT_UNDER_BORDER
)

install_qml_module(quickshell-widgets)

qs_module_pch(quickshell-widgets)

target_link_libraries(quickshell-widgets PRIVATE Qt::Quick)
target_link_libraries(quickshell PRIVATE quickshell-widgetsplugin)
