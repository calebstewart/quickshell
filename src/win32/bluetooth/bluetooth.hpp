#pragma once

#include <qlist.h>
#include <qmetatype.h>
#include <qobject.h>
#include <qqmlintegration.h>
#include <qstring.h>
#include <qthread.h>
#include <qtmetamacros.h>
#include <qtypes.h>

#include "../../core/doc.hpp"
#include "../../core/model.hpp"

// Quickshell.Bluetooth for Windows.
//
// Presents the Bluetooth radio as the default adapter and the paired devices
// as its devices, with whether each is connected. The radio can be switched on
// and off. Windows has no public way to connect, disconnect, pair or forget a
// device, or to scan, so those do nothing. The Windows Runtime calls block, so
// they run on a worker thread that hands plain snapshots to the UI thread.

namespace qs::win32::bluetooth {

///! Connection state of a BluetoothDevice.
namespace BluetoothDeviceState { // NOLINT
Q_NAMESPACE;
QML_ELEMENT;

enum Enum : quint8 {
	Disconnected = 0,
	Connected = 1,
	Disconnecting = 2,
	Connecting = 3,
};
Q_ENUM_NS(Enum);

} // namespace BluetoothDeviceState

///! Power state of a BluetoothAdapter.
namespace BluetoothAdapterState { // NOLINT
Q_NAMESPACE;
QML_ELEMENT;

enum Enum : quint8 {
	Disabled = 0,
	Enabled = 1,
	Enabling = 2,
	Disabling = 3,
	Blocked = 4,
};
Q_ENUM_NS(Enum);

} // namespace BluetoothAdapterState

struct DeviceSnapshot {
	QString id;
	QString name;
	QString address;
	bool connected = false;
};

struct BluetoothSnapshot {
	bool hasRadio = false;
	bool enabled = false;
	QString radioName;
	QList<DeviceSnapshot> devices;
};

// Lives on the worker thread and owns everything from the Windows Runtime.
class BluetoothWorker: public QObject {
	Q_OBJECT;

public:
	explicit BluetoothWorker();
	~BluetoothWorker() override;
	Q_DISABLE_COPY_MOVE(BluetoothWorker);

	void requestRefresh();

public slots:
	void start();
	void refresh();
	void setEnabled(bool enabled);

signals:
	void changed(const qs::win32::bluetooth::BluetoothSnapshot& snapshot);

private:
	struct Private;
	Private* d;
};

class BluetoothAdapter;

///! A paired Bluetooth device.
class BluetoothDevice: public QObject {
	Q_OBJECT;
	// clang-format off
	Q_PROPERTY(QString name READ name NOTIFY nameChanged);
	Q_PROPERTY(QString deviceName READ name NOTIFY nameChanged);
	Q_PROPERTY(QString address READ address CONSTANT);
	Q_PROPERTY(QString icon READ icon CONSTANT);
	Q_PROPERTY(bool connected READ connected WRITE setConnected NOTIFY connectedChanged);
	Q_PROPERTY(qs::win32::bluetooth::BluetoothDeviceState::Enum state READ state NOTIFY connectedChanged);
	/// Only paired devices are listed, so these are always true.
	Q_PROPERTY(bool paired READ yes CONSTANT);
	Q_PROPERTY(bool bonded READ yes CONSTANT);
	Q_PROPERTY(bool trusted READ yes CONSTANT);
	Q_PROPERTY(bool pairing READ no CONSTANT);
	Q_PROPERTY(bool blocked READ no CONSTANT);
	/// Battery levels are not available through this interface.
	Q_PROPERTY(bool batteryAvailable READ no CONSTANT);
	Q_PROPERTY(qreal battery READ battery CONSTANT);
	Q_PROPERTY(qs::win32::bluetooth::BluetoothAdapter* adapter READ adapter CONSTANT);
	// clang-format on
	QML_ELEMENT;
	QML_UNCREATABLE("BluetoothDevices can only be acquired from Bluetooth");

public:
	explicit BluetoothDevice(const DeviceSnapshot& snapshot, BluetoothAdapter* adapter);

	/// Not possible on Windows. Logs a warning.
	Q_INVOKABLE void connect();
	/// Not possible on Windows. Logs a warning.
	Q_INVOKABLE void disconnect();
	/// Not possible on Windows. Logs a warning.
	Q_INVOKABLE void pair();
	Q_INVOKABLE void cancelPair();
	/// Not possible on Windows. Logs a warning.
	Q_INVOKABLE void forget();

	void update(const DeviceSnapshot& snapshot);

	[[nodiscard]] QString id() const { return this->state_.id; }
	[[nodiscard]] QString name() const { return this->state_.name; }
	[[nodiscard]] QString address() const { return this->state_.address; }
	[[nodiscard]] QString icon() const { return "bluetooth"; }
	[[nodiscard]] bool connected() const { return this->state_.connected; }
	void setConnected(bool connected);
	[[nodiscard]] BluetoothDeviceState::Enum state() const {
		return this->state_.connected ? BluetoothDeviceState::Connected
		                              : BluetoothDeviceState::Disconnected;
	}
	[[nodiscard]] bool yes() const { return true; }
	[[nodiscard]] bool no() const { return false; }
	[[nodiscard]] qreal battery() const { return 0; }
	[[nodiscard]] BluetoothAdapter* adapter() const { return this->mAdapter; }

signals:
	void nameChanged();
	void connectedChanged();

private:
	DeviceSnapshot state_;
	BluetoothAdapter* mAdapter;
};

///! The Bluetooth radio.
class BluetoothAdapter: public QObject {
	Q_OBJECT;
	// clang-format off
	Q_PROPERTY(QString name READ name NOTIFY nameChanged);
	Q_PROPERTY(QString adapterId READ adapterId CONSTANT);
	/// Whether the radio is switched on. Can be assigned.
	Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged);
	Q_PROPERTY(qs::win32::bluetooth::BluetoothAdapterState::Enum state READ state NOTIFY enabledChanged);
	/// Scanning is not available on Windows, so this is always false.
	Q_PROPERTY(bool discovering READ no WRITE setDiscovering NOTIFY discoveringChanged);
	Q_PROPERTY(bool discoverable READ no WRITE setDiscovering NOTIFY discoveringChanged);
	Q_PROPERTY(bool pairable READ no WRITE setDiscovering NOTIFY discoveringChanged);
	QSDOC_TYPE_OVERRIDE(ObjectModel<qs::win32::bluetooth::BluetoothDevice>*);
	Q_PROPERTY(UntypedObjectModel* devices READ devices CONSTANT);
	// clang-format on
	QML_ELEMENT;
	QML_UNCREATABLE("BluetoothAdapters can only be acquired from Bluetooth");

public:
	explicit BluetoothAdapter(QObject* parent = nullptr): QObject(parent) {}

	[[nodiscard]] QString name() const { return this->mName; }
	[[nodiscard]] QString adapterId() const { return "hci0"; }
	[[nodiscard]] bool enabled() const { return this->mEnabled; }
	void setEnabled(bool enabled);
	[[nodiscard]] BluetoothAdapterState::Enum state() const {
		return this->mEnabled ? BluetoothAdapterState::Enabled : BluetoothAdapterState::Disabled;
	}
	[[nodiscard]] bool no() const { return false; }
	void setDiscovering(bool discovering);
	[[nodiscard]] ObjectModel<BluetoothDevice>* devices() { return &this->mDevices; }

signals:
	void nameChanged();
	void enabledChanged();
	void discoveringChanged();

private:
	QString mName;
	bool mEnabled = false;
	ObjectModel<BluetoothDevice> mDevices {this};

	friend class BluetoothHost;
};

class BluetoothHost: public QObject {
	Q_OBJECT;

public:
	static BluetoothHost* instance();

	[[nodiscard]] BluetoothAdapter* adapter() { return this->hasRadio ? &this->mAdapter : nullptr; }
	[[nodiscard]] ObjectModel<BluetoothAdapter>* adapters() { return &this->mAdapters; }
	[[nodiscard]] ObjectModel<BluetoothDevice>* devices() { return this->mAdapter.devices(); }
	void setEnabled(bool enabled);

signals:
	void adapterChanged();

private slots:
	void onChanged(const qs::win32::bluetooth::BluetoothSnapshot& snapshot);

private:
	explicit BluetoothHost();

	QThread thread;
	BluetoothWorker* worker = nullptr;
	BluetoothAdapter mAdapter {this};
	ObjectModel<BluetoothAdapter> mAdapters {this};
	bool hasRadio = false;
};

///! Bluetooth.
class Bluetooth: public QObject {
	Q_OBJECT;
	// clang-format off
	/// The Bluetooth radio, or null if the machine has none.
	Q_PROPERTY(qs::win32::bluetooth::BluetoothAdapter* defaultAdapter READ defaultAdapter NOTIFY defaultAdapterChanged);
	QSDOC_TYPE_OVERRIDE(ObjectModel<qs::win32::bluetooth::BluetoothAdapter>*);
	Q_PROPERTY(UntypedObjectModel* adapters READ adapters CONSTANT);
	/// The paired devices.
	QSDOC_TYPE_OVERRIDE(ObjectModel<qs::win32::bluetooth::BluetoothDevice>*);
	Q_PROPERTY(UntypedObjectModel* devices READ devices CONSTANT);
	// clang-format on
	QML_ELEMENT;
	QML_SINGLETON;

public:
	explicit Bluetooth(QObject* parent = nullptr);

	[[nodiscard]] BluetoothAdapter* defaultAdapter() const {
		return BluetoothHost::instance()->adapter();
	} // NOLINT
	[[nodiscard]] ObjectModel<BluetoothAdapter>* adapters() const {
		return BluetoothHost::instance()->adapters();
	} // NOLINT
	[[nodiscard]] ObjectModel<BluetoothDevice>* devices() const {
		return BluetoothHost::instance()->devices();
	} // NOLINT

signals:
	void defaultAdapterChanged();
};

} // namespace qs::win32::bluetooth

Q_DECLARE_METATYPE(qs::win32::bluetooth::BluetoothSnapshot);
