#pragma once

#include <qobject.h>
#include <qqmlintegration.h>
#include <qtmetamacros.h>
#include <qtypes.h>

#include "../core/qmlscreen.hpp"

namespace qs::win32 {

///! Brightness of a monitor.
/// Reads and sets a monitor's brightness over DDC/CI, the control channel
/// external monitors offer. A laptop's built in panel does not answer on it,
/// in which case @@available is false.
///
/// Talking to a monitor takes a noticeable fraction of a second, so it happens
/// on a worker thread. Assignments made while one is in progress are merged,
/// and only the latest value is sent.
class MonitorBrightness: public QObject {
	Q_OBJECT;
	// clang-format off
	/// The screen whose monitor is controlled.
	Q_PROPERTY(QuickshellScreenInfo* screen READ screen WRITE setScreen NOTIFY screenChanged);
	/// True once the monitor has answered. False for a monitor without DDC/CI.
	Q_PROPERTY(bool available READ available NOTIFY availableChanged);
	/// True once the first attempt to read the monitor has finished, whatever its outcome.
	Q_PROPERTY(bool ready READ ready NOTIFY readyChanged);
	/// Brightness from 0 to 1, within the range the monitor reports.
	Q_PROPERTY(qreal brightness READ brightness WRITE setBrightness NOTIFY brightnessChanged);
	// clang-format on
	QML_ELEMENT;

public:
	explicit MonitorBrightness(QObject* parent = nullptr): QObject(parent) {}

	/// Reads the brightness from the monitor again, for instance after it
	/// was changed with the monitor's own buttons.
	Q_INVOKABLE void refresh();

	[[nodiscard]] QuickshellScreenInfo* screen() const { return this->mScreen; }
	void setScreen(QuickshellScreenInfo* screen);

	[[nodiscard]] bool available() const { return this->mAvailable; }
	[[nodiscard]] bool ready() const { return this->mReady; }
	[[nodiscard]] qreal brightness() const { return this->mBrightness; }
	void setBrightness(qreal brightness);

signals:
	void screenChanged();
	void availableChanged();
	void readyChanged();
	void brightnessChanged();

private:
	void onRead(bool ok, qreal brightness);
	void onWritten();
	void sendPending();

	QuickshellScreenInfo* mScreen = nullptr;
	bool mAvailable = false;
	bool mReady = false;
	qreal mBrightness = 0;

	// The value waiting to be sent, or negative if there is none.
	qreal pending = -1;
	bool writing = false;
};

} // namespace qs::win32
