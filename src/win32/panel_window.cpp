#include "panel_window.hpp"
#include <map>

#include <qevent.h>
#include <qlist.h>
#include <qnamespace.h>
#include <qobject.h>
#include <qpoint.h>
#include <qqmlengine.h>
#include <qquickwindow.h>
#include <qrect.h>
#include <qregion.h>
#include <qscreen.h>
#include <qtimer.h>
#include <qtmetamacros.h>
#include <qtypes.h>
#include <windows.h>
// windows.h must come first
#include <shellapi.h>

#include "../core/generation.hpp"
#include "../core/qmlscreen.hpp"
#include "../core/types.hpp"
#include "../window/panelinterface.hpp"
#include "../window/proxywindow.hpp"

namespace {

HWND nativeHandle(QWindow* window) {
	if (window == nullptr || window->handle() == nullptr) return nullptr;
	return reinterpret_cast<HWND>(window->winId()); // NOLINT
}

UINT appBarCallbackMessage() {
	static const UINT message = RegisterWindowMessageW(L"QuickshellAppBarCallback");
	return message;
}

bool anyMouseButtonDown() {
	return (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0
	    || (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0
	    || (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0;
}

// Makes a window the foreground window from a process that is not in the
// foreground, which Windows refuses to a plain SetForegroundWindow. Sharing
// input state with the foreground window's thread makes the request count as
// coming from it. That is not possible when that window belongs to a process
// with more privileges; an input event of our own, here a mouse move of
// nothing, is the other thing that earns the right.
void takeForeground(HWND hwnd) {
	auto* foreground = GetForegroundWindow();
	auto self = GetCurrentThreadId();
	auto other = foreground == nullptr ? 0 : GetWindowThreadProcessId(foreground, nullptr);

	auto attached = other != 0 && other != self && AttachThreadInput(self, other, TRUE) != 0;
	SetForegroundWindow(hwnd);
	if (attached) AttachThreadInput(self, other, FALSE);

	if (GetForegroundWindow() == hwnd) return;

	INPUT input = {};
	input.type = INPUT_MOUSE;
	input.mi.dwFlags = MOUSEEVENTF_MOVE;
	SendInput(1, &input, sizeof(input));
	SetForegroundWindow(hwnd);
}

bool isOurs(HWND window) {
	DWORD pid = 0;
	GetWindowThreadProcessId(window, &pid);
	return pid == GetCurrentProcessId();
}

// The desktop and the taskbar cover a monitor without being an application.
bool isShellWindow(HWND window) {
	if (window == GetShellWindow()) return true;

	wchar_t name[64] = {}; // NOLINT
	GetClassNameW(window, name, 64);
	auto className = QString::fromWCharArray(name);
	return className == "Progman" || className == "WorkerW" || className == "Shell_TrayWnd"
	    || className == "Shell_SecondaryTrayWnd";
}

bool coversMonitor(HWND window, HMONITOR monitor) {
	MONITORINFO info = {};
	info.cbSize = sizeof(info);
	RECT rect = {};
	if (GetMonitorInfoW(monitor, &info) == 0 || GetWindowRect(window, &rect) == 0) return false;

	const auto& screen = info.rcMonitor;
	return rect.left <= screen.left && rect.top <= screen.top && rect.right >= screen.right
	    && rect.bottom >= screen.bottom;
}

// Whether a fullscreen application is showing on a monitor: a game, or a
// video a browser has put fullscreen.
//
// The shell tells registered app bars about this, but only some panels are
// app bars, and the one that matters most, an overlay covering the screen, is
// not. So this watches the foreground window instead. A window that covers
// its whole monitor is remembered for that monitor until it stops doing so or
// another window comes forward on the same monitor. Turning to a different
// monitor leaves it be, as does one of our own panels taking the keyboard.
//
// A maximised window does not count: it fills the work area, which panels
// that reserve space keep smaller than the monitor.
bool monitorHasFullscreenWindow(HMONITOR monitor) {
	static std::map<HMONITOR, HWND> covering; // NOLINT

	auto* foreground = GetForegroundWindow();
	if (foreground != nullptr && !isOurs(foreground) && !isShellWindow(foreground)) {
		auto* on = MonitorFromWindow(foreground, MONITOR_DEFAULTTONULL);
		if (on != nullptr) {
			if (coversMonitor(foreground, on)) covering[on] = foreground;
			else covering.erase(on);
		}
	}

	auto entry = covering.find(monitor);
	if (entry == covering.end()) return false;

	auto* window = entry->second;
	auto stillThere = IsWindow(window) != 0 && IsWindowVisible(window) != 0 && IsIconic(window) == 0
	               && coversMonitor(window, monitor);
	if (!stillThere) covering.erase(entry);
	return stillThere;
}

} // namespace

class WinPanelStack {
public:
	static WinPanelStack* instance() {
		static WinPanelStack* stack = nullptr; // NOLINT

		if (stack == nullptr) {
			stack = new WinPanelStack();
		}

		return stack;
	}

	[[nodiscard]] const QList<WinPanelWindow*>& panels(WinPanelWindow* panel) {
		return this->mPanels[EngineGeneration::findObjectGeneration(panel)];
	}

	void addPanel(WinPanelWindow* panel) {
		panel->engineGeneration = EngineGeneration::findObjectGeneration(panel);
		auto& panels = this->mPanels[panel->engineGeneration];
		if (!panels.contains(panel)) {
			panels.push_back(panel);
		}
	}

	void removePanel(WinPanelWindow* panel) {
		if (!panel->engineGeneration) return;

		auto& panels = this->mPanels[panel->engineGeneration];
		if (panels.removeOne(panel)) {
			if (panels.isEmpty()) {
				this->mPanels.erase(panel->engineGeneration);
				return;
			}

			// from the bottom up, update all panels
			for (auto* other: this->mPanels[panel->engineGeneration]) {
				other->updateDimensions();
			}
		}
	}

	void updateLowerDimensions(WinPanelWindow* exclude) {
		if (!exclude->engineGeneration) return;
		auto& panels = this->mPanels[exclude->engineGeneration];

		// update all panels lower than the one we start from
		auto found = false;
		for (auto* panel: panels) {
			if (panel == exclude) found = true;
			else if (found) panel->updateDimensions(false);
		}
	}

private:
	std::map<EngineGeneration*, QList<WinPanelWindow*>> mPanels;
};

bool WinPanelEventFilter::eventFilter(QObject* watched, QEvent* event) {
	if (event->type() == QEvent::PlatformSurface) {
		auto* surfaceEvent = static_cast<QPlatformSurfaceEvent*>(event); // NOLINT

		if (surfaceEvent->surfaceEventType() == QPlatformSurfaceEvent::SurfaceCreated) {
			emit this->surfaceCreated();
		}
	}

	return this->QObject::eventFilter(watched, event);
}

WinPanelWindow::WinPanelWindow(QObject* parent): ProxyWindowBase(parent) {
	QObject::connect(
	    &this->eventFilter,
	    &WinPanelEventFilter::surfaceCreated,
	    this,
	    &WinPanelWindow::nativeInit
	);

	// Roughly one check per frame at 120Hz. See pollCursor().
	this->cursorTimer.setInterval(8);
	this->cursorTimer.setTimerType(Qt::PreciseTimer);
	QObject::connect(&this->cursorTimer, &QTimer::timeout, this, &WinPanelWindow::pollCursor);

	// See pollFullscreen(). Going fullscreen is not something a person does
	// many times a second.
	this->fullscreenTimer.setInterval(250);
	QObject::connect(&this->fullscreenTimer, &QTimer::timeout, this, &WinPanelWindow::pollFullscreen);

	this->bcExclusiveZone.setBinding([this]() -> qint32 {
		switch (this->bExclusionMode.value()) {
		case ExclusionMode::Ignore: return 0;
		case ExclusionMode::Normal: return this->bExclusiveZone;
		case ExclusionMode::Auto:
			auto edge = this->bcExclusionEdge.value();
			auto margins = this->bMargins.value();

			if (edge == Qt::TopEdge || edge == Qt::BottomEdge) {
				return this->bImplicitHeight + margins.top + margins.bottom;
			} else if (edge == Qt::LeftEdge || edge == Qt::RightEdge) {
				return this->bImplicitWidth + margins.left + margins.right;
			} else {
				return 0;
			}
		}

		return 0;
	});

	this->bcExclusionEdge.setBinding([this] { return this->bAnchors.value().exclusionEdge(); });
}

WinPanelWindow::~WinPanelWindow() {
	this->removeAppBar();
	WinPanelStack::instance()->removePanel(this);
}

void WinPanelWindow::connectWindow() {
	this->ProxyWindowBase::connectWindow();

	this->window->installEventFilter(&this->eventFilter);
	this->updateScreen();

	QObject::connect(
	    this->window,
	    &QQuickWindow::visibleChanged,
	    this,
	    &WinPanelWindow::onWindowVisibleChanged
	);

	// A frameless tool window has no taskbar button and is skipped by alt-tab.
	this->window->setFlag(Qt::FramelessWindowHint);
	this->window->setFlag(Qt::Tool);
	this->updateAboveWindows();
	this->updateFocusable();

	if (this->window->handle() != nullptr) {
		this->nativeInit();
		this->onWindowVisibleChanged();
	}
}

void WinPanelWindow::trySetWidth(qint32 implicitWidth) {
	// only update the actual size if not blocked by anchors
	if (!this->bAnchors.value().horizontalConstraint()) {
		this->ProxyWindowBase::trySetWidth(implicitWidth);
		this->updateDimensions();
	}
}

void WinPanelWindow::trySetHeight(qint32 implicitHeight) {
	// only update the actual size if not blocked by anchors
	if (!this->bAnchors.value().verticalConstraint()) {
		this->ProxyWindowBase::trySetHeight(implicitHeight);
		this->updateDimensions();
	}
}

void WinPanelWindow::setScreen(QuickshellScreenInfo* screen) {
	this->ProxyWindowBase::setScreen(screen);
	this->updateScreen();
}

void WinPanelWindow::nativeInit() {
	auto* hwnd = nativeHandle(this->window);
	if (hwnd == nullptr) return;

	// A window covering a whole monitor is taken for a fullscreen application:
	// the shell reports the user as busy, holds back notification banners and
	// keeps the taskbar behind it. This property tells the shell it is not one.
	SetPropW(hwnd, L"NonRudeHWND", reinterpret_cast<HANDLE>(static_cast<INT_PTR>(1))); // NOLINT

	this->updateDimensions();
	this->updateInputTracking();
}

void WinPanelWindow::onWindowVisibleChanged() {
	if (this->window->isVisible()) {
		WinPanelStack::instance()->addPanel(this);
		this->updateDimensions();
		this->fullscreenTimer.start();
	} else {
		this->removeAppBar();
		WinPanelStack::instance()->removePanel(this);
		this->fullscreenTimer.stop();
		this->belowFullscreen = false;
	}

	this->updateInputTracking();
}

void WinPanelWindow::updateScreen() {
	auto* newScreen =
	    this->mScreen ? this->mScreen : (this->window ? this->window->screen() : nullptr);

	if (newScreen == this->mTrackedScreen) return;

	if (this->mTrackedScreen != nullptr) {
		QObject::disconnect(this->mTrackedScreen, nullptr, this, nullptr);
	}

	this->mTrackedScreen = newScreen;

	if (this->mTrackedScreen != nullptr) {
		QObject::connect(
		    this->mTrackedScreen,
		    &QScreen::geometryChanged,
		    this,
		    &WinPanelWindow::updateDimensionsSlot
		);
	}

	this->updateDimensions();
}

void WinPanelWindow::updateDimensionsSlot() { this->updateDimensions(); }

void WinPanelWindow::updateDimensions(bool propagate) {
	if (nativeHandle(this->window) == nullptr || this->mTrackedScreen == nullptr) return;

	// The full screen rather than the available geometry: the latter already
	// excludes the space this panel reserves for itself.
	auto screenGeometry = this->mTrackedScreen->geometry();

	if (this->bExclusionMode != ExclusionMode::Ignore) {
		for (auto* panel: WinPanelStack::instance()->panels(this)) {
			// we only care about windows below us
			if (panel == this) break;

			// we only care about windows in the same layer
			if (panel->bAboveWindows != this->bAboveWindows) continue;

			if (panel->mTrackedScreen != this->mTrackedScreen) continue;

			auto edge = panel->bcExclusionEdge.value();
			auto exclusiveZone = panel->bcExclusiveZone.value();

			screenGeometry.adjust(
			    edge == Qt::LeftEdge ? exclusiveZone : 0,
			    edge == Qt::TopEdge ? exclusiveZone : 0,
			    edge == Qt::RightEdge ? -exclusiveZone : 0,
			    edge == Qt::BottomEdge ? -exclusiveZone : 0
			);
		}
	}

	auto geometry = QRect();

	auto anchors = this->bAnchors.value();
	auto margins = this->bMargins.value();

	if (anchors.horizontalConstraint()) {
		geometry.setX(screenGeometry.x() + margins.left);
		geometry.setWidth(screenGeometry.width() - margins.left - margins.right);
	} else {
		if (anchors.mLeft) {
			geometry.setX(screenGeometry.x() + margins.left);
		} else if (anchors.mRight) {
			geometry.setX(
			    screenGeometry.x() + screenGeometry.width() - this->implicitWidth() - margins.right
			);
		} else {
			geometry.setX(screenGeometry.x() + screenGeometry.width() / 2 - this->implicitWidth() / 2);
		}

		geometry.setWidth(this->implicitWidth());
	}

	if (anchors.verticalConstraint()) {
		geometry.setY(screenGeometry.y() + margins.top);
		geometry.setHeight(screenGeometry.height() - margins.top - margins.bottom);
	} else {
		if (anchors.mTop) {
			geometry.setY(screenGeometry.y() + margins.top);
		} else if (anchors.mBottom) {
			geometry.setY(
			    screenGeometry.y() + screenGeometry.height() - this->implicitHeight() - margins.bottom
			);
		} else {
			geometry.setY(screenGeometry.y() + screenGeometry.height() / 2 - this->implicitHeight() / 2);
		}

		geometry.setHeight(this->implicitHeight());
	}

	this->window->setScreen(this->mTrackedScreen);
	this->window->setGeometry(geometry);
	this->updateAppBar(propagate);
}

void WinPanelWindow::removeAppBar() {
	if (!this->appBarRegistered) return;
	this->appBarRegistered = false;

	auto* hwnd = nativeHandle(this->window);
	if (hwnd == nullptr) return;

	APPBARDATA data = {};
	data.cbSize = sizeof(APPBARDATA);
	data.hWnd = hwnd;
	SHAppBarMessage(ABM_REMOVE, &data);
}

void WinPanelWindow::updateAppBar(bool propagate) {
	auto* hwnd = nativeHandle(this->window);
	if (hwnd == nullptr) return;

	auto edge = this->bcExclusionEdge.value();
	auto exclusiveZone = this->bcExclusiveZone.value();

	auto wanted = this->window->isVisible() && edge != 0 && exclusiveZone > 0
	           && this->bExclusionMode != ExclusionMode::Ignore && this->mTrackedScreen != nullptr;

	if (!wanted) {
		this->removeAppBar();
		return;
	}

	APPBARDATA data = {};
	data.cbSize = sizeof(APPBARDATA);
	data.hWnd = hwnd;

	if (!this->appBarRegistered) {
		data.uCallbackMessage = appBarCallbackMessage();
		if (SHAppBarMessage(ABM_NEW, &data) == 0) return;
		this->appBarRegistered = true;
	}

	MONITORINFO monitor = {};
	monitor.cbSize = sizeof(MONITORINFO);
	if (!GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor)) return;

	// AppBar rectangles are in physical pixels.
	auto thickness = qRound(exclusiveZone * this->mTrackedScreen->devicePixelRatio());

	auto applyThickness = [&](RECT& rect) {
		switch (edge) {
		case Qt::LeftEdge: rect.right = rect.left + thickness; break;
		case Qt::RightEdge: rect.left = rect.right - thickness; break;
		case Qt::TopEdge: rect.bottom = rect.top + thickness; break;
		case Qt::BottomEdge: rect.top = rect.bottom - thickness; break;
		default: break;
		}
	};

	switch (edge) {
	case Qt::LeftEdge: data.uEdge = ABE_LEFT; break;
	case Qt::RightEdge: data.uEdge = ABE_RIGHT; break;
	case Qt::TopEdge: data.uEdge = ABE_TOP; break;
	case Qt::BottomEdge: data.uEdge = ABE_BOTTOM; break;
	default: break;
	}

	data.rc = monitor.rcMonitor;
	applyThickness(data.rc);

	// The shell moves the rectangle clear of other AppBars such as the taskbar,
	// after which the thickness has to be applied again.
	SHAppBarMessage(ABM_QUERYPOS, &data);
	applyThickness(data.rc);
	SHAppBarMessage(ABM_SETPOS, &data);

	if (propagate) WinPanelStack::instance()->updateLowerDimensions(this);
}

void WinPanelWindow::updateAboveWindows() {
	if (this->window == nullptr) return;

	auto above = this->bAboveWindows.value();
	this->window->setFlag(Qt::WindowStaysOnBottomHint, !above);
	this->window->setFlag(Qt::WindowStaysOnTopHint, above);
}

// A panel above other windows would also sit above a fullscreen application,
// where a layer shell compositor puts it below. While such an application
// covers this panel's monitor the panel goes to the bottom of the window
// order, and comes back when the application is gone.
//
// A panel that wants the keyboard is left alone, so a launcher opened by a
// key still shows over a game.
void WinPanelWindow::pollFullscreen() {
	auto* hwnd = nativeHandle(this->window);
	if (hwnd == nullptr) return;

	auto lower = this->bAboveWindows.value() && !this->bFocusable.value()
	          && monitorHasFullscreenWindow(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST));

	const auto flags = SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE;

	if (lower) {
		// Checked every time and not only on a change: Qt reapplies the
		// window's styles, this one included, when a flag changes.
		auto topmost = (GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
		if (topmost || !this->belowFullscreen) {
			SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, flags);
			SetWindowPos(hwnd, HWND_BOTTOM, 0, 0, 0, 0, flags);
		}
	} else if (this->belowFullscreen) {
		SetWindowPos(hwnd, this->bAboveWindows.value() ? HWND_TOPMOST : HWND_BOTTOM, 0, 0, 0, 0, flags);
	}

	this->belowFullscreen = lower;
}

void WinPanelWindow::updateFocusable() {
	if (this->window == nullptr) return;

	auto focusable = this->bFocusable.value();
	this->window->setFlag(Qt::WindowDoesNotAcceptFocus, !focusable);
	// Read by the windows platform plugin when the window is shown.
	this->window->setProperty("_q_showWithoutActivating", !focusable);

	// A panel is given focus so that something in it can be typed into, most
	// often because a key opened it, in which case nothing else would move the
	// keyboard there: a layer shell compositor does that itself. Give the
	// keyboard back to whoever had it once the panel stops wanting it.
	//
	// Deferred, as changing the flag above reapplies the window's styles.
	if (focusable) {
		QTimer::singleShot(0, this, [this] {
			if (!this->bFocusable.value()) return;
			auto* hwnd = nativeHandle(this->window);
			if (hwnd == nullptr || !this->window->isVisible()) return;

			auto* foreground = GetForegroundWindow();
			if (foreground != hwnd) {
				this->previousForeground = reinterpret_cast<quintptr>(foreground); // NOLINT
				takeForeground(hwnd);
			}

			this->window->requestActivate();
		});
	} else {
		auto* previous = reinterpret_cast<HWND>(this->previousForeground); // NOLINT
		this->previousForeground = 0;

		auto* hwnd = nativeHandle(this->window);
		if (previous != nullptr && hwnd != nullptr && GetForegroundWindow() == hwnd
		    && IsWindow(previous))
			SetForegroundWindow(previous);
	}
}

void WinPanelWindow::onPolished() {
	if (this->pendingPolish.inputMask) {
		this->hasInputMask = this->mMask != nullptr;

		if (this->hasInputMask) {
			this->inputMask = this->mMask->applyTo(QRect(0, 0, this->width(), this->height()));
		} else {
			this->inputMask = QRegion();
		}

		this->pendingPolish.inputMask = false;
		this->updateInputTracking();
	}

	emit this->polished();
}

void WinPanelWindow::updateInputTracking() {
	auto* hwnd = nativeHandle(this->window);
	auto track = hwnd != nullptr && this->hasInputMask && this->window->isVisible();

	if (track) {
		if (!this->cursorTimer.isActive()) this->cursorTimer.start();
		this->pollCursor();
		return;
	}

	this->cursorTimer.stop();

	if (hwnd != nullptr) {
		auto style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
		if ((style & WS_EX_TRANSPARENT) != 0) {
			SetWindowLongPtrW(hwnd, GWL_EXSTYLE, style & ~WS_EX_TRANSPARENT);
		}
	}
}

void WinPanelWindow::pollCursor() {
	auto* hwnd = nativeHandle(this->window);
	if (hwnd == nullptr) return;

	auto style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
	auto transparent = (style & WS_EX_TRANSPARENT) != 0;

	// Leave the window alone during a click or drag, so one that started on
	// the panel keeps being delivered to it after leaving the mask.
	if (anyMouseButtonDown() && (style & WS_EX_LAYERED) != 0) return;

	POINT cursor = {};
	if (!GetCursorPos(&cursor) || !ScreenToClient(hwnd, &cursor)) return;

	auto scale = this->window->devicePixelRatio();
	auto local = QPoint(qFloor(cursor.x / scale), qFloor(cursor.y / scale));
	auto wantTransparent = !this->inputMask.contains(local);

	// Qt rewrites the extended style when window flags change, so the layered
	// bit is checked on every poll rather than assumed.
	auto layered = (style & WS_EX_LAYERED) != 0;
	if (layered && transparent == wantTransparent) return;

	auto newStyle = style | WS_EX_LAYERED;
	if (wantTransparent) newStyle |= WS_EX_TRANSPARENT;
	else newStyle &= ~WS_EX_TRANSPARENT;

	SetWindowLongPtrW(hwnd, GWL_EXSTYLE, newStyle);

	// A layered window is not presented until its attributes are set.
	if (!layered) SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
}

// WinPanelInterface

WinPanelInterface::WinPanelInterface(QObject* parent)
    : PanelWindowInterface(parent)
    , panel(new WinPanelWindow(this)) {
	this->connectSignals();

	// clang-format off
	QObject::connect(this->panel, &WinPanelWindow::anchorsChanged, this, &WinPanelInterface::anchorsChanged);
	QObject::connect(this->panel, &WinPanelWindow::marginsChanged, this, &WinPanelInterface::marginsChanged);
	QObject::connect(this->panel, &WinPanelWindow::exclusiveZoneChanged, this, &WinPanelInterface::exclusiveZoneChanged);
	QObject::connect(this->panel, &WinPanelWindow::exclusionModeChanged, this, &WinPanelInterface::exclusionModeChanged);
	QObject::connect(this->panel, &WinPanelWindow::aboveWindowsChanged, this, &WinPanelInterface::aboveWindowsChanged);
	QObject::connect(this->panel, &WinPanelWindow::focusableChanged, this, &WinPanelInterface::focusableChanged);
	// clang-format on
}

void WinPanelInterface::onReload(QObject* oldInstance) {
	QQmlEngine::setContextForObject(this->panel, QQmlEngine::contextForObject(this));

	auto* old = qobject_cast<WinPanelInterface*>(oldInstance);
	this->panel->reload(old != nullptr ? old->panel : nullptr);
}

ProxyWindowBase* WinPanelInterface::proxyWindow() const { return this->panel; }

// NOLINTBEGIN
#define proxyPair(type, get, set)                                                                  \
	type WinPanelInterface::get() const { return this->panel->get(); }                               \
	void WinPanelInterface::set(type value) { this->panel->set(value); }

proxyPair(Anchors, anchors, setAnchors);
proxyPair(Margins, margins, setMargins);
proxyPair(qint32, exclusiveZone, setExclusiveZone);
proxyPair(ExclusionMode::Enum, exclusionMode, setExclusionMode);
proxyPair(bool, focusable, setFocusable);
proxyPair(bool, aboveWindows, setAboveWindows);

#undef proxyPair
// NOLINTEND
