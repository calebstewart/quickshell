#pragma once

#include <qlist.h>
#include <qobject.h>
#include <qqmlintegration.h>
#include <qstring.h>
#include <qtimer.h>
#include <qtmetamacros.h>
#include <qtypes.h>

#include "../../core/doc.hpp"
#include "../../core/model.hpp"

// Quickshell.Services.UPower for Windows.
//
// Windows reports one combined battery, which is presented as the display
// device. The values come from the system power status and are polled, as
// they change slowly. Power profiles are reported as balanced and cannot be
// changed yet.

namespace qs::win32::power {

///! Power state of a UPower device.
namespace UPowerDeviceState { // NOLINT
Q_NAMESPACE;
QML_ELEMENT;

enum Enum : quint8 {
	Unknown = 0,
	Charging = 1,
	Discharging = 2,
	Empty = 3,
	FullyCharged = 4,
	PendingCharge = 5,
	PendingDischarge = 6,
};
Q_ENUM_NS(Enum);

} // namespace UPowerDeviceState

///! Type of a UPower device.
namespace UPowerDeviceType { // NOLINT
Q_NAMESPACE;
QML_ELEMENT;

enum Enum : quint8 {
	Unknown = 0,
	LinePower = 1,
	Battery = 2,
};
Q_ENUM_NS(Enum);

} // namespace UPowerDeviceType

///! Power profile.
namespace PowerProfile { // NOLINT
Q_NAMESPACE;
QML_ELEMENT;

enum Enum : quint8 {
	PowerSaver = 0,
	Balanced = 1,
	Performance = 2,
};
Q_ENUM_NS(Enum);

} // namespace PowerProfile

///! Reason for performance degradation.
namespace PerformanceDegradationReason { // NOLINT
Q_NAMESPACE;
QML_ELEMENT;

enum Enum : quint8 {
	None = 0,
	LapDetected = 1,
	HighTemperature = 2,
};
Q_ENUM_NS(Enum);

} // namespace PerformanceDegradationReason

///! The system battery.
class UPowerDevice: public QObject {
	Q_OBJECT;
	// clang-format off
	Q_PROPERTY(qs::win32::power::UPowerDeviceType::Enum type READ type NOTIFY changed);
	Q_PROPERTY(bool powerSupply READ isPresent NOTIFY changed);
	/// Windows does not report energy in watt hours through this interface.
	Q_PROPERTY(qreal energy READ zero CONSTANT);
	Q_PROPERTY(qreal energyCapacity READ zero CONSTANT);
	Q_PROPERTY(qreal changeRate READ zero CONSTANT);
	/// Estimated seconds until empty, or 0 if unknown or not discharging.
	Q_PROPERTY(qreal timeToEmpty READ timeToEmpty NOTIFY changed);
	Q_PROPERTY(qreal timeToFull READ zero CONSTANT);
	/// Charge from 0 to 1.
	Q_PROPERTY(qreal percentage READ percentage NOTIFY changed);
	/// If the system has a battery.
	Q_PROPERTY(bool isPresent READ isPresent NOTIFY changed);
	Q_PROPERTY(qs::win32::power::UPowerDeviceState::Enum state READ state NOTIFY changed);
	Q_PROPERTY(qreal healthPercentage READ healthPercentage CONSTANT);
	Q_PROPERTY(bool healthSupported READ no CONSTANT);
	Q_PROPERTY(QString iconName READ empty CONSTANT);
	Q_PROPERTY(bool isLaptopBattery READ isPresent NOTIFY changed);
	Q_PROPERTY(QString nativePath READ empty CONSTANT);
	Q_PROPERTY(QString model READ empty CONSTANT);
	/// Always true: the power status is read synchronously.
	Q_PROPERTY(bool ready READ ready NOTIFY readyChanged);
	// clang-format on
	QML_ELEMENT;
	QML_UNCREATABLE("UPowerDevices can only be acquired from UPower");

public:
	explicit UPowerDevice(QObject* parent = nullptr): QObject(parent) {}

	[[nodiscard]] UPowerDeviceType::Enum type() const {
		return this->mPresent ? UPowerDeviceType::Battery : UPowerDeviceType::Unknown;
	}
	[[nodiscard]] bool isPresent() const { return this->mPresent; }
	[[nodiscard]] qreal percentage() const { return this->mPercentage; }
	[[nodiscard]] qreal timeToEmpty() const { return this->mTimeToEmpty; }
	[[nodiscard]] UPowerDeviceState::Enum state() const { return this->mState; }
	[[nodiscard]] qreal healthPercentage() const { return 100; }
	[[nodiscard]] qreal zero() const { return 0; }
	[[nodiscard]] bool no() const { return false; }
	[[nodiscard]] bool ready() const { return true; }
	[[nodiscard]] QString empty() const { return QString(); }

	// Returns true if anything changed.
	bool update(bool present, qreal percentage, qreal timeToEmpty, UPowerDeviceState::Enum state);

signals:
	void changed();
	// Never emitted. Declared so handlers written for UPower can connect.
	void readyChanged();

private:
	bool mPresent = false;
	qreal mPercentage = 1;
	qreal mTimeToEmpty = 0;
	UPowerDeviceState::Enum mState = UPowerDeviceState::Unknown;
};

class PowerHost: public QObject {
	Q_OBJECT;

public:
	static PowerHost* instance();

	[[nodiscard]] UPowerDevice* battery() { return &this->mBattery; }
	[[nodiscard]] ObjectModel<UPowerDevice>* devices() { return &this->mDevices; }
	[[nodiscard]] bool onBattery() const { return this->mOnBattery; }

signals:
	void onBatteryChanged();

private:
	explicit PowerHost();
	void poll();

	UPowerDevice mBattery {this};
	ObjectModel<UPowerDevice> mDevices {this};
	bool mOnBattery = false;
	bool listed = false;
	QTimer timer;
};

///! Power status.
class UPower: public QObject {
	Q_OBJECT;
	// clang-format off
	/// The system battery. Its isPresent is false on a machine without one.
	Q_PROPERTY(qs::win32::power::UPowerDevice* displayDevice READ displayDevice CONSTANT);
	QSDOC_TYPE_OVERRIDE(ObjectModel<qs::win32::power::UPowerDevice>*);
	Q_PROPERTY(UntypedObjectModel* devices READ devices CONSTANT);
	/// If the system is running on battery power.
	Q_PROPERTY(bool onBattery READ onBattery NOTIFY onBatteryChanged);
	// clang-format on
	QML_ELEMENT;
	QML_SINGLETON;

public:
	explicit UPower(QObject* parent = nullptr);

	[[nodiscard]] UPowerDevice* displayDevice() const { return PowerHost::instance()->battery(); } // NOLINT
	[[nodiscard]] ObjectModel<UPowerDevice>* devices() const { return PowerHost::instance()->devices(); } // NOLINT
	[[nodiscard]] bool onBattery() const { return PowerHost::instance()->onBattery(); } // NOLINT

signals:
	void onBatteryChanged();
};

///! Power profiles.
/// Stand-in: always reports the balanced profile. Assigning a profile only
/// logs a warning.
class PowerProfiles: public QObject {
	Q_OBJECT;
	// clang-format off
	Q_PROPERTY(qs::win32::power::PowerProfile::Enum profile READ profile WRITE setProfile NOTIFY profileChanged);
	Q_PROPERTY(bool hasPerformanceProfile READ hasPerformanceProfile CONSTANT);
	Q_PROPERTY(qs::win32::power::PerformanceDegradationReason::Enum degradationReason READ degradationReason CONSTANT);
	Q_PROPERTY(QList<QObject*> holds READ holds CONSTANT);
	// clang-format on
	QML_ELEMENT;
	QML_SINGLETON;

public:
	explicit PowerProfiles(QObject* parent = nullptr): QObject(parent) {}

	[[nodiscard]] PowerProfile::Enum profile() const { return PowerProfile::Balanced; } // NOLINT
	void setProfile(PowerProfile::Enum profile);
	[[nodiscard]] bool hasPerformanceProfile() const { return true; } // NOLINT
	[[nodiscard]] PerformanceDegradationReason::Enum degradationReason() const { // NOLINT
		return PerformanceDegradationReason::None;
	}
	[[nodiscard]] QList<QObject*> holds() const { return {}; } // NOLINT

signals:
	void profileChanged();
};

} // namespace qs::win32::power
