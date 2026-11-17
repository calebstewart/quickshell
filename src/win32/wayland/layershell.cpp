#include "layershell.hpp"

#include <qobject.h>
#include <qstring.h>

#include "../../window/panelinterface.hpp"

namespace qs::win32::wayland {

WlrLayershell::WlrLayershell(QObject* parent)
    : QObject(parent)
    , panel(qobject_cast<PanelWindowInterface*>(parent)) {
	if (this->panel) {
		QObject::connect(
		    this->panel,
		    &PanelWindowInterface::exclusionModeChanged,
		    this,
		    &WlrLayershell::exclusionModeChanged
		);
	}
}

void WlrLayershell::setLayer(WlrLayer::Enum layer) {
	if (layer == this->mLayer) return;
	this->mLayer = layer;

	if (this->panel) {
		this->panel->setAboveWindows(layer == WlrLayer::Top || layer == WlrLayer::Overlay);
	}

	emit this->layerChanged();
}

void WlrLayershell::setNamespace(const QString& ns) {
	if (ns == this->mNamespace) return;
	this->mNamespace = ns;
	emit this->namespaceChanged();
}

void WlrLayershell::setKeyboardFocus(WlrKeyboardFocus::Enum focus) {
	if (focus == this->mKeyboardFocus) return;
	this->mKeyboardFocus = focus;

	if (this->panel) this->panel->setFocusable(focus != WlrKeyboardFocus::None);

	emit this->keyboardFocusChanged();
}

ExclusionMode::Enum WlrLayershell::exclusionMode() const {
	return this->panel ? this->panel->exclusionMode() : ExclusionMode::Auto;
}

void WlrLayershell::setExclusionMode(ExclusionMode::Enum mode) {
	if (this->panel) this->panel->setExclusionMode(mode);
}

WlrLayershell* WlrLayershell::qmlAttachedProperties(QObject* object) {
	return new WlrLayershell(object);
}

} // namespace qs::win32::wayland
