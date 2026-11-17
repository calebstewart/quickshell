#include "power.hpp"

#include <qlogging.h>
#include <qloggingcategory.h>
#include <qobject.h>
#include <qtimer.h>
#include <qtypes.h>
#include <windows.h>

#include "../../core/logcat.hpp"

namespace qs::win32::power {

namespace {
QS_LOGGING_CATEGORY(logPower, "quickshell.win32.power", QtWarningMsg);

// Values of SYSTEM_POWER_STATUS that mean "not known" or "no battery".
constexpr BYTE FLAG_CHARGING = 8;
constexpr BYTE FLAG_NO_BATTERY = 128;
constexpr BYTE BYTE_UNKNOWN = 255;
constexpr DWORD LIFETIME_UNKNOWN = 0xffffffff;
constexpr BYTE LINE_OFFLINE = 0;
constexpr BYTE LINE_ONLINE = 1;

} // namespace

bool UPowerDevice::update(
    bool present,
    qreal percentage,
    qreal timeToEmpty,
    UPowerDeviceState::Enum state
) {
	// clang-format off
	auto same = present == this->mPresent
	         && qFuzzyCompare(percentage + 1, this->mPercentage + 1)
	         && qFuzzyCompare(timeToEmpty + 1, this->mTimeToEmpty + 1)
	         && state == this->mState;
	// clang-format on

	if (same) return false;

	this->mPresent = present;
	this->mPercentage = percentage;
	this->mTimeToEmpty = timeToEmpty;
	this->mState = state;
	emit this->changed();
	return true;
}

PowerHost* PowerHost::instance() {
	static auto* instance = new PowerHost(); // NOLINT
	return instance;
}

PowerHost::PowerHost() {
	this->poll();

	// The charge level moves slowly, but plugging in should show promptly.
	QObject::connect(&this->timer, &QTimer::timeout, this, &PowerHost::poll);
	this->timer.start(5000);
}

void PowerHost::poll() {
	SYSTEM_POWER_STATUS status = {};
	if (!GetSystemPowerStatus(&status)) {
		qCWarning(logPower) << "Could not read the system power status, error" << GetLastError();
		return;
	}

	auto present = status.BatteryFlag != BYTE_UNKNOWN && (status.BatteryFlag & FLAG_NO_BATTERY) == 0;
	auto charging = present && (status.BatteryFlag & FLAG_CHARGING) != 0;
	auto onBattery = present && status.ACLineStatus == LINE_OFFLINE;

	qreal percentage = 1;
	if (present && status.BatteryLifePercent != BYTE_UNKNOWN) {
		percentage = static_cast<qreal>(status.BatteryLifePercent) / 100.0;
	}

	qreal timeToEmpty = 0;
	if (onBattery && status.BatteryLifeTime != LIFETIME_UNKNOWN) {
		timeToEmpty = static_cast<qreal>(status.BatteryLifeTime);
	}

	auto state = UPowerDeviceState::Unknown;
	if (present) {
		if (charging) state = UPowerDeviceState::Charging;
		else if (onBattery) state = percentage <= 0 ? UPowerDeviceState::Empty : UPowerDeviceState::Discharging;
		else if (status.ACLineStatus == LINE_ONLINE) {
			// Plugged in and not charging: full, or held below full by the firmware.
			state = percentage >= 0.99 ? UPowerDeviceState::FullyCharged : UPowerDeviceState::PendingCharge;
		}
	}

	this->mBattery.update(present, percentage, timeToEmpty, state);

	if (present != this->listed) {
		if (present) this->mDevices.insertObject(&this->mBattery);
		else this->mDevices.removeObject(&this->mBattery);
		this->listed = present;
	}

	if (onBattery != this->mOnBattery) {
		this->mOnBattery = onBattery;
		emit this->onBatteryChanged();
	}
}

UPower::UPower(QObject* parent): QObject(parent) {
	QObject::connect(
	    PowerHost::instance(),
	    &PowerHost::onBatteryChanged,
	    this,
	    &UPower::onBatteryChanged
	);
}

void PowerProfiles::setProfile(PowerProfile::Enum profile) {
	if (profile == this->profile()) return;
	qCWarning(logPower) << "Changing the power profile is not implemented on Windows.";
}

} // namespace qs::win32::power
