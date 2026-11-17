#pragma once

#include <qnamespace.h>
#include <qobject.h>
#include <qpointer.h>
#include <qproperty.h>
#include <qqmlintegration.h>
#include <qquickwindow.h>
#include <qregion.h>
#include <qscreen.h>
#include <qtclasshelpermacros.h>
#include <qtimer.h>
#include <qtmetamacros.h>

#include "../core/doc.hpp"
#include "../core/util.hpp"
#include "../window/panelinterface.hpp"
#include "../window/proxywindow.hpp"

class WinPanelStack;

class WinPanelEventFilter: public QObject {
	Q_OBJECT;

public:
	explicit WinPanelEventFilter(QObject* parent = nullptr): QObject(parent) {}

signals:
	void surfaceCreated();

protected:
	bool eventFilter(QObject* watched, QEvent* event) override;
};

// A PanelWindow backed by a frameless, always on top (or bottom) tool window.
//
// Exclusive zones are reserved by registering the window as an AppBar, the
// same mechanism the taskbar uses, which shrinks the monitor's work area.
//
// The mask is input only, as it is on Wayland: QWindow::setMask would map to
// SetWindowRgn, which also clips what is drawn. Windows has no per region
// input passthrough for a window like this, so the cursor is tracked and the
// whole window is switched between accepting and ignoring input.
class WinPanelWindow: public ProxyWindowBase {
	QSDOC_BASECLASS(PanelWindowInterface);
	Q_OBJECT;
	// clang-format off
	QSDOC_HIDE Q_PROPERTY(Anchors anchors READ anchors WRITE setAnchors NOTIFY anchorsChanged);
	QSDOC_HIDE Q_PROPERTY(qint32 exclusiveZone READ exclusiveZone WRITE setExclusiveZone NOTIFY exclusiveZoneChanged);
	QSDOC_HIDE Q_PROPERTY(ExclusionMode::Enum exclusionMode READ exclusionMode WRITE setExclusionMode NOTIFY exclusionModeChanged);
	QSDOC_HIDE Q_PROPERTY(Margins margins READ margins WRITE setMargins NOTIFY marginsChanged);
	QSDOC_HIDE Q_PROPERTY(bool aboveWindows READ aboveWindows WRITE setAboveWindows NOTIFY aboveWindowsChanged);
	QSDOC_HIDE Q_PROPERTY(bool focusable READ focusable WRITE setFocusable NOTIFY focusableChanged);
	// clang-format on
	QML_ELEMENT;

public:
	explicit WinPanelWindow(QObject* parent = nullptr);
	~WinPanelWindow() override;
	Q_DISABLE_COPY_MOVE(WinPanelWindow);

	void connectWindow() override;

	void trySetWidth(qint32 implicitWidth) override;
	void trySetHeight(qint32 implicitHeight) override;

	void setScreen(QuickshellScreenInfo* screen) override;

	[[nodiscard]] bool aboveWindows() const { return this->bAboveWindows; }
	void setAboveWindows(bool aboveWindows) { this->bAboveWindows = aboveWindows; }

	[[nodiscard]] Anchors anchors() const { return this->bAnchors; }
	void setAnchors(Anchors anchors) { this->bAnchors = anchors; }

	[[nodiscard]] qint32 exclusiveZone() const { return this->bExclusiveZone; }
	void setExclusiveZone(qint32 exclusiveZone) {
		Qt::beginPropertyUpdateGroup();
		this->bExclusiveZone = exclusiveZone;
		this->bExclusionMode = ExclusionMode::Normal;
		Qt::endPropertyUpdateGroup();
	}

	[[nodiscard]] ExclusionMode::Enum exclusionMode() const { return this->bExclusionMode; }
	void setExclusionMode(ExclusionMode::Enum exclusionMode) { this->bExclusionMode = exclusionMode; }

	[[nodiscard]] Margins margins() const { return this->bMargins; }
	void setMargins(Margins margins) { this->bMargins = margins; }

	[[nodiscard]] bool focusable() const { return this->bFocusable; }
	void setFocusable(bool focusable) { this->bFocusable = focusable; }

signals:
	QSDOC_HIDE void anchorsChanged();
	QSDOC_HIDE void exclusiveZoneChanged();
	QSDOC_HIDE void exclusionModeChanged();
	QSDOC_HIDE void marginsChanged();
	QSDOC_HIDE void aboveWindowsChanged();
	QSDOC_HIDE void focusableChanged();

protected slots:
	void onPolished() override;

private slots:
	void nativeInit();
	void onWindowVisibleChanged();
	void updateDimensionsSlot();
	void pollCursor();

private:
	void updateScreen();
	void updateAppBar(bool propagate = true);
	void updateAppBarCb() { this->updateAppBar(); }
	void removeAppBar();
	void updateAboveWindows();
	void updateFocusable();
	void updateDimensions(bool propagate = true);
	void updateDimensionsCb() { this->updateDimensions(); }
	void updateInputTracking();

	QPointer<QScreen> mTrackedScreen = nullptr;
	WinPanelEventFilter eventFilter;

	bool appBarRegistered = false;
	// The window that had the keyboard before this panel took it, as a HWND.
	quintptr previousForeground = 0;
	bool hasInputMask = false;
	QRegion inputMask;
	QTimer cursorTimer;

	// clang-format off
	Q_OBJECT_BINDABLE_PROPERTY_WITH_ARGS(WinPanelWindow, bool, bAboveWindows, true, &WinPanelWindow::aboveWindowsChanged);
	Q_OBJECT_BINDABLE_PROPERTY(WinPanelWindow, bool, bFocusable, &WinPanelWindow::focusableChanged);
	Q_OBJECT_BINDABLE_PROPERTY(WinPanelWindow, Anchors, bAnchors, &WinPanelWindow::anchorsChanged);
	Q_OBJECT_BINDABLE_PROPERTY(WinPanelWindow, Margins, bMargins, &WinPanelWindow::marginsChanged);
	Q_OBJECT_BINDABLE_PROPERTY(WinPanelWindow, qint32, bExclusiveZone, &WinPanelWindow::exclusiveZoneChanged);
	Q_OBJECT_BINDABLE_PROPERTY_WITH_ARGS(WinPanelWindow, ExclusionMode::Enum, bExclusionMode, ExclusionMode::Auto, &WinPanelWindow::exclusionModeChanged);
	Q_OBJECT_BINDABLE_PROPERTY(WinPanelWindow, qint32, bcExclusiveZone);
	Q_OBJECT_BINDABLE_PROPERTY(WinPanelWindow, Qt::Edge, bcExclusionEdge);

	QS_BINDING_SUBSCRIBE_METHOD(WinPanelWindow, bAboveWindows, updateAboveWindows, onValueChanged);
	QS_BINDING_SUBSCRIBE_METHOD(WinPanelWindow, bAnchors, updateDimensionsCb, onValueChanged);
	QS_BINDING_SUBSCRIBE_METHOD(WinPanelWindow, bMargins, updateDimensionsCb, onValueChanged);
	QS_BINDING_SUBSCRIBE_METHOD(WinPanelWindow, bcExclusiveZone, updateAppBarCb, onValueChanged);
	QS_BINDING_SUBSCRIBE_METHOD(WinPanelWindow, bFocusable, updateFocusable, onValueChanged);
	// clang-format on

	friend class WinPanelStack;
};

class WinPanelInterface: public PanelWindowInterface {
	Q_OBJECT;

public:
	explicit WinPanelInterface(QObject* parent = nullptr);

	void onReload(QObject* oldInstance) override;

	[[nodiscard]] ProxyWindowBase* proxyWindow() const override;

	// NOLINTBEGIN
	[[nodiscard]] Anchors anchors() const override;
	void setAnchors(Anchors anchors) override;

	[[nodiscard]] Margins margins() const override;
	void setMargins(Margins margins) override;

	[[nodiscard]] qint32 exclusiveZone() const override;
	void setExclusiveZone(qint32 exclusiveZone) override;

	[[nodiscard]] ExclusionMode::Enum exclusionMode() const override;
	void setExclusionMode(ExclusionMode::Enum exclusionMode) override;

	[[nodiscard]] bool aboveWindows() const override;
	void setAboveWindows(bool aboveWindows) override;

	[[nodiscard]] bool focusable() const override;
	void setFocusable(bool focusable) override;
	// NOLINTEND

private:
	WinPanelWindow* panel;
};
