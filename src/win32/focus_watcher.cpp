#include "focus_watcher.hpp"

#include <qobject.h>
#include <qtimer.h>
#include <windows.h>

namespace qs::win32 {

namespace {

bool anyMouseButtonDown() {
	return (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0 || (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0
	    || (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0;
}

bool isOurs(HWND window) {
	if (window == nullptr) return false;

	DWORD pid = 0;
	GetWindowThreadProcessId(GetAncestor(window, GA_ROOT), &pid);
	return pid == GetCurrentProcessId();
}

} // namespace

FocusGrabWatcher::FocusGrabWatcher(QObject* parent): QObject(parent) {
	// There is no event for "a button was pressed somewhere else" short of a
	// system wide mouse hook, which would sit in the path of every mouse
	// message. Polling at about the frame rate is cheap and only runs while a
	// grab is active.
	this->timer.setInterval(15);
	this->timer.setTimerType(Qt::PreciseTimer);
	QObject::connect(&this->timer, &QTimer::timeout, this, &FocusGrabWatcher::poll);
}

void FocusGrabWatcher::setActive(bool active) {
	if (active == this->mActive) return;
	this->mActive = active;

	if (active) {
		// Whatever is the case now is the starting point. The press that
		// opened the panel may still be held, and another program may be in
		// front already.
		this->buttonWasDown = anyMouseButtonDown();
		this->lastForeground = GetForegroundWindow();
		this->timer.start();
	} else {
		this->timer.stop();
	}

	emit this->activeChanged();
}

void FocusGrabWatcher::poll() {
	auto dismiss = false;

	auto buttonDown = anyMouseButtonDown();
	if (buttonDown && !this->buttonWasDown) {
		POINT cursor = {};
		if (GetCursorPos(&cursor) && !isOurs(WindowFromPoint(cursor))) dismiss = true;
	}
	this->buttonWasDown = buttonDown;

	auto* foreground = GetForegroundWindow();
	if (foreground != this->lastForeground) {
		this->lastForeground = foreground;
		if (foreground != nullptr && !isOurs(foreground)) dismiss = true;
	}

	if (dismiss) emit this->dismissed();
}

} // namespace qs::win32
