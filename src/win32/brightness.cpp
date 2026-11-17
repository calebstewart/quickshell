#include "brightness.hpp"
#include <algorithm>
#include <vector>

#include <qcoreapplication.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qobject.h>
#include <qobjectdefs.h>
#include <qpointer.h>
#include <qscreen.h>
#include <qthread.h>
#include <qtypes.h>
#include <windows.h>
// windows.h must come first
#include <highlevelmonitorconfigurationapi.h>
#include <lowlevelmonitorconfigurationapi.h>
#include <physicalmonitorenumerationapi.h>

#include "../core/logcat.hpp"
#include "../core/qmlscreen.hpp"

namespace qs::win32 {

namespace {
QS_LOGGING_CATEGORY(logBrightness, "quickshell.win32.brightness", QtWarningMsg);

// The object every DDC/CI call is queued on. Its thread is never stopped.
QObject* worker() {
	static QObject* context = [] {
		auto* thread = new QThread();
		thread->setObjectName("monitor brightness");
		thread->start();

		auto* object = new QObject();
		object->moveToThread(thread);
		return object;
	}();

	return context;
}

HMONITOR monitorOf(QuickshellScreenInfo* screen) {
	if (screen == nullptr || screen->screen == nullptr) return nullptr;

	auto* native = screen->screen->nativeInterface<QNativeInterface::QWindowsScreen>();
	return native != nullptr ? native->handle() : nullptr;
}

// Runs a function on the first physical monitor behind a display. A display
// normally has exactly one. Returns false if it has none or the function fails.
template <typename F>
bool withPhysicalMonitor(HMONITOR monitor, F&& function) {
	DWORD count = 0;
	if (!GetNumberOfPhysicalMonitorsFromHMONITOR(monitor, &count) || count == 0) return false;

	auto monitors = std::vector<PHYSICAL_MONITOR>(count);
	if (!GetPhysicalMonitorsFromHMONITOR(monitor, count, monitors.data())) return false;

	auto ok = function(monitors[0].hPhysicalMonitor);
	DestroyPhysicalMonitors(count, monitors.data());
	return ok;
}

// The VCP code for luminance, which is what brightness is on the wire.
constexpr BYTE VCP_LUMINANCE = 0x10;

// A monitor sometimes fails to answer a request it answers a moment later.
constexpr int ATTEMPTS = 3;

// Reads the brightness range and value. The high level call first asks the
// monitor for its capabilities, which some monitors answer badly, so the
// plain VCP request is tried as well.
bool readBrightness(HANDLE physical, DWORD& minimum, DWORD& current, DWORD& maximum) {
	for (auto attempt = 0; attempt < ATTEMPTS; attempt++) {
		if (GetMonitorBrightness(physical, &minimum, &current, &maximum) && maximum > minimum)
			return true;

		DWORD value = 0;
		DWORD limit = 0;
		if (GetVCPFeatureAndVCPFeatureReply(physical, VCP_LUMINANCE, nullptr, &value, &limit)
		    && limit > 0)
		{
			minimum = 0;
			current = value;
			maximum = limit;
			return true;
		}

		Sleep(60);
	}

	return false;
}

bool writeBrightness(HANDLE physical, DWORD value) {
	for (auto attempt = 0; attempt < ATTEMPTS; attempt++) {
		if (SetMonitorBrightness(physical, value)) return true;
		if (SetVCPFeature(physical, VCP_LUMINANCE, value)) return true;
		Sleep(60);
	}

	return false;
}

} // namespace

void MonitorBrightness::setScreen(QuickshellScreenInfo* screen) {
	if (screen == this->mScreen) return;
	this->mScreen = screen;
	emit this->screenChanged();
	this->refresh();
}

void MonitorBrightness::refresh() {
	auto* monitor = monitorOf(this->mScreen);

	if (monitor == nullptr) {
		this->onRead(false, 0);
		return;
	}

	auto self = QPointer<MonitorBrightness>(this);

	QMetaObject::invokeMethod(
	    worker(),
	    [self, monitor] {
		    qreal brightness = 0;

		    auto ok = withPhysicalMonitor(monitor, [&](HANDLE physical) {
			    DWORD minimum = 0;
			    DWORD current = 0;
			    DWORD maximum = 0;
			    if (!readBrightness(physical, minimum, current, maximum)) return false;

			    brightness =
			        static_cast<qreal>(current - minimum) / static_cast<qreal>(maximum - minimum);
			    return true;
		    });

		    QMetaObject::invokeMethod(
		        QCoreApplication::instance(),
		        [self, ok, brightness] {
			        if (self) self->onRead(ok, brightness);
		        },
		        Qt::QueuedConnection
		    );
	    },
	    Qt::QueuedConnection
	);
}

void MonitorBrightness::onRead(bool ok, qreal brightness) {
	if (ok != this->mAvailable) {
		this->mAvailable = ok;
		emit this->availableChanged();
	}

	// A value assigned in the meantime is newer than what the monitor reported.
	if (ok && this->pending < 0 && !this->writing
	    && !qFuzzyCompare(brightness + 1, this->mBrightness + 1))
	{
		this->mBrightness = brightness;
		emit this->brightnessChanged();
	}

	if (!this->mReady) {
		this->mReady = true;
		emit this->readyChanged();
	}

	qCDebug(logBrightness) << "Monitor brightness read" << ok << brightness;
}

void MonitorBrightness::setBrightness(qreal brightness) {
	brightness = std::clamp(brightness, 0.0, 1.0);
	if (qFuzzyCompare(brightness + 1, this->mBrightness + 1)) return;

	// Shown at once, and sent when the monitor is free.
	this->mBrightness = brightness;
	emit this->brightnessChanged();

	this->pending = brightness;
	if (!this->writing) this->sendPending();
}

void MonitorBrightness::sendPending() {
	auto* monitor = monitorOf(this->mScreen);

	if (monitor == nullptr || this->pending < 0) {
		this->pending = -1;
		return;
	}

	auto value = this->pending;
	this->pending = -1;
	this->writing = true;

	auto self = QPointer<MonitorBrightness>(this);

	QMetaObject::invokeMethod(
	    worker(),
	    [self, monitor, value] {
		    auto ok = withPhysicalMonitor(monitor, [&](HANDLE physical) {
			    DWORD minimum = 0;
			    DWORD current = 0;
			    DWORD maximum = 0;
			    if (!readBrightness(physical, minimum, current, maximum)) return false;

			    auto target =
			        minimum + static_cast<DWORD>(qRound(value * static_cast<qreal>(maximum - minimum)));
			    return writeBrightness(physical, target);
		    });

		    if (!ok)
			    qCDebug(logBrightness) << "Could not set monitor brightness, error" << GetLastError();

		    QMetaObject::invokeMethod(
		        QCoreApplication::instance(),
		        [self] {
			        if (self) self->onWritten();
		        },
		        Qt::QueuedConnection
		    );
	    },
	    Qt::QueuedConnection
	);
}

void MonitorBrightness::onWritten() {
	this->writing = false;
	if (this->pending >= 0) this->sendPending();
}

} // namespace qs::win32
