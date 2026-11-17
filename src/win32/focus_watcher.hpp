#pragma once

#include <qobject.h>
#include <qqmlintegration.h>
#include <qtimer.h>
#include <qtmetamacros.h>
#include <windows.h>

namespace qs::win32 {

///! Notices when the user turns to something outside the shell.
/// While @@active, emits @@dismissed when a mouse button is pressed over a
/// window that is not one of this program's, or when another program's window
/// becomes the foreground window. This is what a focus grab does on Wayland,
/// minus swallowing the click: Windows has no way for a program to take it.
///
/// A panel that ignores input outside its mask does not count as being under
/// the cursor there, so a press beside a panel is a press outside.
class FocusGrabWatcher: public QObject {
	Q_OBJECT;
	Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged);
	QML_ELEMENT;

public:
	explicit FocusGrabWatcher(QObject* parent = nullptr);

	[[nodiscard]] bool active() const { return this->mActive; }
	void setActive(bool active);

signals:
	void activeChanged();
	/// Sent once for each press outside, or change of foreground window, while active.
	void dismissed();

private:
	void poll();

	bool mActive = false;
	bool buttonWasDown = false;
	HWND lastForeground = nullptr;
	QTimer timer;
};

} // namespace qs::win32
