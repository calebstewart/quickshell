#include "bluetooth.hpp"
#include <algorithm>

#include <qlist.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qmetatype.h>
#include <qobject.h>
#include <qstring.h>
#include <qthread.h>

#include "../../core/logcat.hpp"

namespace qs::win32::bluetooth {

namespace {
QS_LOGGING_CATEGORY(logBluetooth, "quickshell.win32.bluetooth", QtWarningMsg);
}

BluetoothDevice::BluetoothDevice(const DeviceSnapshot& snapshot, BluetoothAdapter* adapter)
    : QObject(adapter)
    , state_(snapshot)
    , mAdapter(adapter) {}

void BluetoothDevice::update(const DeviceSnapshot& snapshot) {
	auto old = this->state_;
	this->state_ = snapshot;

	if (old.name != snapshot.name) emit this->nameChanged();
	if (old.connected != snapshot.connected) emit this->connectedChanged();
}

void BluetoothDevice::setConnected(bool connected) {
	if (connected) this->connect();
	else this->disconnect();
}

void BluetoothDevice::connect() {
	qCWarning(logBluetooth) << "Connecting a Bluetooth device is not possible on Windows.";
}

void BluetoothDevice::disconnect() {
	qCWarning(logBluetooth) << "Disconnecting a Bluetooth device is not possible on Windows.";
}

void BluetoothDevice::pair() {
	qCWarning(logBluetooth) << "Pairing a Bluetooth device is not possible on Windows.";
}

void BluetoothDevice::cancelPair() {}

void BluetoothDevice::forget() {
	qCWarning(logBluetooth) << "Forgetting a Bluetooth device is not possible on Windows.";
}

void BluetoothAdapter::setEnabled(bool enabled) {
	if (enabled == this->mEnabled) return;
	BluetoothHost::instance()->setEnabled(enabled);
}

void BluetoothAdapter::setDiscovering(bool discovering) {
	if (discovering) qCWarning(logBluetooth) << "Scanning for Bluetooth devices is not possible on Windows.";
}

BluetoothHost* BluetoothHost::instance() {
	static auto* instance = new BluetoothHost(); // NOLINT
	return instance;
}

BluetoothHost::BluetoothHost() {
	qRegisterMetaType<BluetoothSnapshot>();

	this->worker = new BluetoothWorker();
	this->worker->moveToThread(&this->thread);

	QObject::connect(this->worker, &BluetoothWorker::changed, this, &BluetoothHost::onChanged);
	QObject::connect(&this->thread, &QThread::started, this->worker, &BluetoothWorker::start);

	this->thread.setObjectName("bluetooth");
	this->thread.start();
}

void BluetoothHost::setEnabled(bool enabled) {
	QMetaObject::invokeMethod(
	    this->worker,
	    [worker = this->worker, enabled] { worker->setEnabled(enabled); },
	    Qt::QueuedConnection
	);
}

void BluetoothHost::onChanged(const BluetoothSnapshot& snapshot) {
	auto& devices = this->mAdapter.mDevices;

	for (auto* device: QList<BluetoothDevice*>(devices.valueList())) {
		auto present = std::ranges::any_of(snapshot.devices, [&](const DeviceSnapshot& entry) {
			return entry.id == device->id();
		});

		if (!present) {
			devices.removeObject(device);
			device->deleteLater();
		}
	}

	for (const auto& entry: snapshot.devices) {
		BluetoothDevice* existing = nullptr;

		for (auto* device: devices.valueList()) {
			if (device->id() == entry.id) {
				existing = device;
				break;
			}
		}

		if (existing != nullptr) existing->update(entry);
		else devices.insertObject(new BluetoothDevice(entry, &this->mAdapter));
	}

	if (snapshot.radioName != this->mAdapter.mName) {
		this->mAdapter.mName = snapshot.radioName;
		emit this->mAdapter.nameChanged();
	}

	if (snapshot.enabled != this->mAdapter.mEnabled) {
		this->mAdapter.mEnabled = snapshot.enabled;
		emit this->mAdapter.enabledChanged();
	}

	if (snapshot.hasRadio != this->hasRadio) {
		this->hasRadio = snapshot.hasRadio;

		if (this->hasRadio) this->mAdapters.insertObject(&this->mAdapter);
		else this->mAdapters.removeObject(&this->mAdapter);

		emit this->adapterChanged();
	}
}

Bluetooth::Bluetooth(QObject* parent): QObject(parent) {
	QObject::connect(
	    BluetoothHost::instance(),
	    &BluetoothHost::adapterChanged,
	    this,
	    &Bluetooth::defaultAdapterChanged
	);
}

} // namespace qs::win32::bluetooth
