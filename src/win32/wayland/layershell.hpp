#pragma once

#include <qobject.h>
#include <qpointer.h>
#include <qqmlintegration.h>
#include <qstring.h>
#include <qtmetamacros.h>
#include <qtypes.h>

#include "../../window/panelinterface.hpp"

// Windows stand-ins for the parts of Quickshell.Wayland that configurations
// written for Wayland name directly. They let such a configuration load, and
// map what has an equivalent onto the Windows panel backend.

namespace qs::win32::wayland {

///! WlrLayershell layer.
namespace WlrLayer { // NOLINT
Q_NAMESPACE;
QML_ELEMENT;

enum Enum : quint8 {
	Background = 0,
	Bottom = 1,
	Top = 2,
	Overlay = 3,
};
Q_ENUM_NS(Enum);

} // namespace WlrLayer

///! WlrLayershell keyboard focus mode
namespace WlrKeyboardFocus { // NOLINT
Q_NAMESPACE;
QML_ELEMENT;

enum Enum : quint8 {
	None = 0,
	Exclusive = 1,
	OnDemand = 2,
};
Q_ENUM_NS(Enum);

} // namespace WlrKeyboardFocus

// Attached to a PanelWindow as `WlrLayershell.<property>`. Layers above normal
// windows map to aboveWindows, and any keyboard focus mode to focusable. The
// namespace has no meaning on Windows and is only stored.
class WlrLayershell: public QObject {
	Q_OBJECT;
	// clang-format off
	Q_PROPERTY(qs::win32::wayland::WlrLayer::Enum layer READ layer WRITE setLayer NOTIFY layerChanged);
	Q_PROPERTY(QString namespace READ ns WRITE setNamespace NOTIFY namespaceChanged);
	Q_PROPERTY(qs::win32::wayland::WlrKeyboardFocus::Enum keyboardFocus READ keyboardFocus WRITE setKeyboardFocus NOTIFY keyboardFocusChanged);
	Q_PROPERTY(ExclusionMode::Enum exclusionMode READ exclusionMode WRITE setExclusionMode NOTIFY exclusionModeChanged);
	// clang-format on
	QML_ELEMENT;
	QML_UNCREATABLE("WlrLayershell is only available as an attached object on Windows.");
	QML_ATTACHED(WlrLayershell);

public:
	explicit WlrLayershell(QObject* parent = nullptr);

	[[nodiscard]] WlrLayer::Enum layer() const { return this->mLayer; }
	void setLayer(WlrLayer::Enum layer);

	[[nodiscard]] QString ns() const { return this->mNamespace; }
	void setNamespace(const QString& ns);

	[[nodiscard]] WlrKeyboardFocus::Enum keyboardFocus() const { return this->mKeyboardFocus; }
	void setKeyboardFocus(WlrKeyboardFocus::Enum focus);

	[[nodiscard]] ExclusionMode::Enum exclusionMode() const;
	void setExclusionMode(ExclusionMode::Enum mode);

	static WlrLayershell* qmlAttachedProperties(QObject* object);

signals:
	void layerChanged();
	void namespaceChanged();
	void keyboardFocusChanged();
	void exclusionModeChanged();

private:
	QPointer<PanelWindowInterface> panel;
	WlrLayer::Enum mLayer = WlrLayer::Top;
	WlrKeyboardFocus::Enum mKeyboardFocus = WlrKeyboardFocus::None;
	QString mNamespace = "quickshell";
};

} // namespace qs::win32::wayland
