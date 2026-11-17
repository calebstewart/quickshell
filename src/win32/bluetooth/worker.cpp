// The Windows Runtime headers come first, before Qt defines its keyword macros.
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Devices.Radios.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>

#include <atomic>

#include <qlist.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qobject.h>
#include <qobjectdefs.h>
#include <qstring.h>
#include <qtimer.h>

#include "../../core/logcat.hpp"
#include "bluetooth.hpp"

namespace qs::win32::bluetooth {

namespace {
QS_LOGGING_CATEGORY(logBluetoothWorker, "quickshell.win32.bluetooth", QtWarningMsg);

using winrt::Windows::Devices::Enumeration::DeviceInformation;
using winrt::Windows::Devices::Radios::Radio;
using winrt::Windows::Devices::Radios::RadioAccessStatus;
using winrt::Windows::Devices::Radios::RadioKind;
using winrt::Windows::Devices::Radios::RadioState;

QString toQString(const winrt::hstring& string) {
	return QString::fromWCharArray(string.c_str(), static_cast<qsizetype>(string.size()));
}

} // namespace

struct BluetoothWorker::Private {
	Radio radio {nullptr};
	Radio::StateChanged_revoker stateRevoker;
	std::atomic<bool> refreshPending = false;
};

BluetoothWorker::BluetoothWorker(): d(new Private()) {}
BluetoothWorker::~BluetoothWorker() { delete this->d; }

void BluetoothWorker::requestRefresh() {
	if (this->d->refreshPending.exchange(true)) return;
	QMetaObject::invokeMethod(this, &BluetoothWorker::refresh, Qt::QueuedConnection);
}

void BluetoothWorker::start() {
	try {
		winrt::init_apartment(winrt::apartment_type::multi_threaded);

		for (const auto& radio: Radio::GetRadiosAsync().get()) {
			if (radio.Kind() != RadioKind::Bluetooth) continue;

			this->d->radio = radio;
			this->d->stateRevoker = radio.StateChanged(
			    winrt::auto_revoke,
			    [this](const auto& /*unused*/, const auto& /*unused*/) { this->requestRefresh(); }
			);
			break;
		}
	} catch (const winrt::hresult_error& error) {
		qCWarning(logBluetoothWorker) << "Could not access the Bluetooth radio:" << toQString(error.message());
	}

	// Whether a device is connected has no change event that covers both
	// classic and low energy devices, so the list is read again periodically.
	auto* timer = new QTimer(this);
	QObject::connect(timer, &QTimer::timeout, this, &BluetoothWorker::refresh);
	timer->start(10000);

	this->refresh();
}

void BluetoothWorker::refresh() {
	this->d->refreshPending = false;

	BluetoothSnapshot snapshot;

	try {
		if (this->d->radio) {
			snapshot.hasRadio = true;
			snapshot.enabled = this->d->radio.State() == RadioState::On;
			snapshot.radioName = toQString(this->d->radio.Name());
		}

		auto properties = winrt::single_threaded_vector<winrt::hstring>({
		    L"System.Devices.Aep.IsConnected",
		    L"System.Devices.Aep.DeviceAddress",
		});

		// Classic and low energy devices are listed separately, and a device
		// that supports both appears in each list.
		auto selectors = {
		    winrt::Windows::Devices::Bluetooth::BluetoothDevice::GetDeviceSelectorFromPairingState(true),
		    winrt::Windows::Devices::Bluetooth::BluetoothLEDevice::GetDeviceSelectorFromPairingState(true),
		};

		for (const auto& selector: selectors) {
			for (const auto& info: DeviceInformation::FindAllAsync(selector, properties).get()) {
				DeviceSnapshot device;
				device.id = toQString(info.Id());
				device.name = toQString(info.Name());

				auto values = info.Properties();
				device.connected = winrt::unbox_value_or<bool>(
				    values.TryLookup(L"System.Devices.Aep.IsConnected"),
				    false
				);
				device.address = toQString(winrt::unbox_value_or<winrt::hstring>(
				    values.TryLookup(L"System.Devices.Aep.DeviceAddress"),
				    L""
				));

				DeviceSnapshot* existing = nullptr;
				for (auto& known: snapshot.devices) {
					if (!device.address.isEmpty() && known.address == device.address) existing = &known;
				}

				if (existing != nullptr) existing->connected = existing->connected || device.connected;
				else snapshot.devices.append(device);
			}
		}
	} catch (const winrt::hresult_error& error) {
		qCWarning(logBluetoothWorker) << "Could not list Bluetooth devices:" << toQString(error.message());
	}

	qCDebug(logBluetoothWorker) << "Bluetooth radio present" << snapshot.hasRadio << "on" << snapshot.enabled
	                            << "paired devices" << snapshot.devices.size();

	emit this->changed(snapshot);
}

void BluetoothWorker::setEnabled(bool enabled) {
	if (!this->d->radio) return;

	try {
		if (Radio::RequestAccessAsync().get() != RadioAccessStatus::Allowed) {
			qCWarning(logBluetoothWorker) << "Windows did not allow changing the Bluetooth radio state.";
			return;
		}

		this->d->radio.SetStateAsync(enabled ? RadioState::On : RadioState::Off).get();
	} catch (const winrt::hresult_error& error) {
		qCWarning(logBluetoothWorker) << "Could not switch the Bluetooth radio:" << toQString(error.message());
	}

	this->refresh();
}

} // namespace qs::win32::bluetooth
