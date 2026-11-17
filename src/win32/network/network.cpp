// winsock2.h has to come before windows.h, which Qt's headers may bring in.
#include <winsock2.h>
#include <ws2tcpip.h>
// then the rest of the Windows headers
#include <windows.h>
#include <iphlpapi.h>
#include <objbase.h>
#include <wlanapi.h>

#include <algorithm>
#include <atomic>
#include <map>
#include <vector>

#include <qlist.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qobject.h>
#include <qobjectdefs.h>
#include <qstring.h>
#include <qstringlist.h>
#include <qtimer.h>
#include <qtypes.h>

#include "../../core/logcat.hpp"
#include "network.hpp"

namespace qs::win32::network {

namespace {
QS_LOGGING_CATEGORY(logNetwork, "quickshell.win32.network", QtWarningMsg);

// Adapters that are not hardware the user would recognise: virtual switches,
// tunnels, and Bluetooth's network profile.
const char* const IGNORED_DESCRIPTIONS[] = {
    "Hyper-V", "Virtual", "Bluetooth", "VPN", "TAP-", "Loopback", "WAN Miniport", "Wintun", "WireGuard",
};

QString formatGuid(const GUID& guid) {
	WCHAR text[64] = {}; // NOLINT
	StringFromGUID2(guid, text, 64);
	return QString::fromWCharArray(text).toUpper();
}

QString formatMac(const BYTE* bytes, ULONG length) {
	QStringList parts;
	for (ULONG i = 0; i < length; i++) {
		parts.append(QString::number(bytes[i], 16).rightJustified(2, '0').toUpper()); // NOLINT
	}

	return parts.join(':');
}

QString formatIpv4(const SOCKADDR* address) {
	if (address == nullptr || address->sa_family != AF_INET) return QString();

	char text[INET_ADDRSTRLEN] = {}; // NOLINT
	auto* in = reinterpret_cast<const sockaddr_in*>(address); // NOLINT
	if (inet_ntop(AF_INET, &in->sin_addr, text, sizeof(text)) == nullptr) return QString();
	return QString::fromLatin1(text);
}

QString prefixToMask(quint8 prefix) {
	if (prefix > 32) return QString();
	quint32 mask = prefix == 0 ? 0 : 0xffffffffU << (32 - prefix);

	return QString("%1.%2.%3.%4")
	    .arg((mask >> 24) & 0xff)
	    .arg((mask >> 16) & 0xff)
	    .arg((mask >> 8) & 0xff)
	    .arg(mask & 0xff);
}

QString ssidOf(const DOT11_SSID& ssid) {
	return QString::fromUtf8(
	    reinterpret_cast<const char*>(ssid.ucSSID), // NOLINT
	    static_cast<qsizetype>(std::min<ULONG>(ssid.uSSIDLength, DOT11_SSID_MAX_LENGTH))
	);
}

WifiSecurityType::Enum securityOf(bool secured, DOT11_AUTH_ALGORITHM auth) {
	if (!secured) return WifiSecurityType::Open;

	// Numeric values, as the newer names are missing from some headers.
	switch (static_cast<int>(auth)) {
	case 1: return WifiSecurityType::StaticWep; // open system with a cipher
	case 2: return WifiSecurityType::StaticWep; // shared key
	case 3: return WifiSecurityType::WpaEap;
	case 4: return WifiSecurityType::WpaPsk;
	case 6: return WifiSecurityType::Wpa2Eap;
	case 7: return WifiSecurityType::Wpa2Psk;
	case 8: return WifiSecurityType::Wpa3SuiteB192; // WPA3 enterprise 192 bit
	case 9: return WifiSecurityType::Sae;
	case 10: return WifiSecurityType::Owe;
	case 11: return WifiSecurityType::Wpa2Eap; // WPA3 enterprise
	default: return WifiSecurityType::Unknown;
	}
}

} // namespace

QString WifiSecurityType::toString(WifiSecurityType::Enum type) {
	switch (type) {
	case Wpa3SuiteB192: return "WPA3 Enterprise";
	case Sae: return "WPA3";
	case Wpa2Eap: return "WPA2 Enterprise";
	case Wpa2Psk: return "WPA2";
	case WpaEap: return "WPA Enterprise";
	case WpaPsk: return "WPA";
	case StaticWep:
	case DynamicWep: return "WEP";
	case Leap: return "LEAP";
	case Owe: return "OWE";
	case Open: return "Open";
	default: return QString();
	}
}

WifiNetwork::WifiNetwork(const WifiNetworkData& data, NetworkDevice* device)
    : QObject(device)
    , data(data)
    , mDevice(device) {}

void WifiNetwork::update(const WifiNetworkData& data) {
	auto old = this->data;
	this->data = data;

	if (old.connected != data.connected) emit this->connectedChanged();
	if (old.known != data.known) emit this->knownChanged();
	if (!qFuzzyCompare(old.signalStrength + 1, data.signalStrength + 1)) emit this->signalStrengthChanged();
	if (old.security != data.security) emit this->securityChanged();
	if (old.bssid != data.bssid) emit this->bssidChanged();
	if (old.frequency != data.frequency) emit this->frequencyChanged();
}

void WifiNetwork::connect() {
	qCWarning(logNetwork) << "Connecting to a Wi-Fi network is not implemented on Windows yet.";
}

void WifiNetwork::connectWithPsk(const QString& /*unused*/) {
	qCWarning(logNetwork) << "Connecting to a Wi-Fi network is not implemented on Windows yet.";
}

void WifiNetwork::disconnect() {
	qCWarning(logNetwork) << "Disconnecting from a Wi-Fi network is not implemented on Windows yet.";
}

void WifiNetwork::forget() {
	qCWarning(logNetwork) << "Forgetting a Wi-Fi network is not implemented on Windows yet.";
}

NetworkDevice::NetworkDevice(const DeviceData& data, QObject* parent): QObject(parent) {
	this->data.id = data.id;
	this->data.wifi = data.wifi;
	this->update(data);
}

void NetworkDevice::update(const DeviceData& data) {
	auto old = this->data;
	this->data = data;

	if (old.name != data.name || old.description != data.description) emit this->nameChanged();
	if (old.address != data.address) emit this->addressChanged();
	if (old.connected != data.connected) emit this->connectedChanged();
	if (old.linkSpeed != data.linkSpeed) emit this->linkSpeedChanged();

	if (old.ipAddress != data.ipAddress || old.subnet != data.subnet || old.gateway != data.gateway
	    || old.dns != data.dns)
	{
		emit this->ipChanged();
	}

	// Networks are kept by SSID, so a binding to one survives a refresh.
	auto existing = this->mNetworks.valueList();
	QList<WifiNetwork*> networks;

	for (const auto& entry: data.networks) {
		WifiNetwork* network = nullptr;

		for (auto* candidate: existing) {
			if (candidate->name() == entry.ssid) {
				network = candidate;
				break;
			}
		}

		if (network != nullptr) network->update(entry);
		else network = new WifiNetwork(entry, this);

		networks.append(network);
	}

	this->mNetworks.diffUpdate(networks);

	for (auto* candidate: existing) {
		if (!networks.contains(candidate)) candidate->deleteLater();
	}
}

void NetworkDevice::disconnect() {
	qCWarning(logNetwork) << "Disconnecting a network device is not implemented on Windows yet.";
}

void NetworkDevice::scan() {
	if (this->data.wifi) NetworkHost::instance()->scan(this->data.id);
}

void NetworkDevice::setScannerEnabled(bool enabled) {
	if (enabled == this->mScannerEnabled) return;
	this->mScannerEnabled = enabled;
	emit this->scannerEnabledChanged();
	if (enabled) this->scan();
}

struct NetworkHost::Private {
	HANDLE wlan = nullptr;
	std::atomic<bool> refreshPending = false;
};

NetworkHost* NetworkHost::instance() {
	static auto* instance = new NetworkHost(); // NOLINT
	return instance;
}

NetworkHost::NetworkHost(): d(new Private()) {
	DWORD version = 0;
	if (WlanOpenHandle(2, nullptr, &version, &this->d->wlan) != ERROR_SUCCESS) {
		// The WLAN service is not running, as on a machine without Wi-Fi.
		this->d->wlan = nullptr;
	} else {
		// Called on a thread of the WLAN service, so it only asks for a refresh.
		WlanRegisterNotification(
		    this->d->wlan,
		    WLAN_NOTIFICATION_SOURCE_ACM | WLAN_NOTIFICATION_SOURCE_MSM,
		    TRUE,
		    [](PWLAN_NOTIFICATION_DATA /*unused*/, PVOID context) {
			    static_cast<NetworkHost*>(context)->requestRefresh();
		    },
		    this,
		    nullptr,
		    nullptr
		);
	}

	// Wired adapters have no change notification here, so everything is also
	// read again periodically.
	QObject::connect(&this->timer, &QTimer::timeout, this, &NetworkHost::refresh);
	this->timer.start(5000);

	QObject::connect(&this->scanTimer, &QTimer::timeout, this, [this] {
		for (auto* device: this->mDevices.valueList()) {
			if (device->scannerEnabled()) device->scan();
		}
	});
	this->scanTimer.start(15000);

	this->refresh();
}

void NetworkHost::requestRefresh() {
	if (this->d->refreshPending.exchange(true)) return;
	QMetaObject::invokeMethod(this, &NetworkHost::refresh, Qt::QueuedConnection);
}

bool NetworkHost::anyConnected() const {
	return std::ranges::any_of(this->mDevices.valueList(), [](NetworkDevice* device) {
		return device->connected();
	});
}

void NetworkHost::scan(const QString& deviceId) {
	if (this->d->wlan == nullptr) return;

	GUID guid = {};
	if (FAILED(CLSIDFromString(reinterpret_cast<LPCOLESTR>(deviceId.utf16()), &guid))) return; // NOLINT
	WlanScan(this->d->wlan, &guid, nullptr, nullptr, nullptr);
}

void NetworkHost::setWifiEnabled(bool enabled) {
	if (this->d->wlan == nullptr) return;

	PWLAN_INTERFACE_INFO_LIST interfaces = nullptr;
	if (WlanEnumInterfaces(this->d->wlan, nullptr, &interfaces) != ERROR_SUCCESS) return;

	for (DWORD i = 0; i < interfaces->dwNumberOfItems; i++) {
		WLAN_PHY_RADIO_STATE state = {};
		state.dwPhyIndex = 0;
		state.dot11SoftwareRadioState = enabled ? dot11_radio_state_on : dot11_radio_state_off;
		state.dot11HardwareRadioState = dot11_radio_state_on;

		auto result = WlanSetInterface(
		    this->d->wlan,
		    &interfaces->InterfaceInfo[i].InterfaceGuid, // NOLINT
		    wlan_intf_opcode_radio_state,
		    sizeof(state),
		    &state,
		    nullptr
		);

		if (result != ERROR_SUCCESS) {
			qCWarning(logNetwork) << "Could not switch the Wi-Fi radio, error" << result;
		}
	}

	WlanFreeMemory(interfaces);
	this->requestRefresh();
}

void NetworkHost::refresh() {
	this->d->refreshPending = false;

	QList<DeviceData> found;

	// Adapters and their IP configuration.
	ULONG size = 32 * 1024;
	std::vector<BYTE> buffer;
	ULONG result = ERROR_BUFFER_OVERFLOW;

	for (auto attempt = 0; attempt < 3 && result == ERROR_BUFFER_OVERFLOW; attempt++) {
		buffer.resize(size);
		result = GetAdaptersAddresses(
		    AF_INET,
		    GAA_FLAG_INCLUDE_GATEWAYS | GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST,
		    nullptr,
		    reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.data()), // NOLINT
		    &size
		);
	}

	if (result == ERROR_SUCCESS) {
		auto* adapter = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.data()); // NOLINT

		for (; adapter != nullptr; adapter = adapter->Next) {
			if (adapter->IfType != IF_TYPE_ETHERNET_CSMACD && adapter->IfType != IF_TYPE_IEEE80211) continue;

			auto description = QString::fromWCharArray(adapter->Description);
			auto ignored = std::ranges::any_of(IGNORED_DESCRIPTIONS, [&](const char* word) {
				return description.contains(QLatin1String(word), Qt::CaseInsensitive);
			});
			if (ignored) continue;

			DeviceData device;
			device.id = QString::fromLatin1(adapter->AdapterName).toUpper();
			device.name = QString::fromWCharArray(adapter->FriendlyName);
			device.description = description;
			device.address = formatMac(adapter->PhysicalAddress, adapter->PhysicalAddressLength); // NOLINT
			device.wifi = adapter->IfType == IF_TYPE_IEEE80211;
			device.connected = adapter->OperStatus == IfOperStatusUp;

			// Reported as all ones when not known.
			if (device.connected && adapter->TransmitLinkSpeed < 1000000000000ULL) {
				device.linkSpeed = static_cast<quint32>(adapter->TransmitLinkSpeed / 1000000ULL);
			}

			// A disconnected adapter still carries a self assigned address, which
			// would read as if it were configured.
			for (auto* unicast = device.connected ? adapter->FirstUnicastAddress : nullptr; unicast != nullptr; unicast = unicast->Next) {
				auto ip = formatIpv4(unicast->Address.lpSockaddr);
				if (ip.isEmpty()) continue;

				device.ipAddress = ip;
				device.subnet = prefixToMask(unicast->OnLinkPrefixLength);
				break;
			}

			for (auto* gateway = device.connected ? adapter->FirstGatewayAddress : nullptr; gateway != nullptr; gateway = gateway->Next) {
				auto ip = formatIpv4(gateway->Address.lpSockaddr);
				if (ip.isEmpty()) continue;

				device.gateway = ip;
				break;
			}

			for (auto* dns = device.connected ? adapter->FirstDnsServerAddress : nullptr; dns != nullptr; dns = dns->Next) {
				auto ip = formatIpv4(dns->Address.lpSockaddr);
				if (!ip.isEmpty()) device.dns.append(ip);
			}

			found.append(device);
		}
	} else {
		qCWarning(logNetwork) << "Could not list network adapters, error" << result;
	}

	// Wi-Fi networks and the radio state.
	auto wifiEnabled = false;
	auto wifiHardwareEnabled = false;
	QStringList wlanInterfaces;

	PWLAN_INTERFACE_INFO_LIST interfaces = nullptr;
	if (this->d->wlan != nullptr && WlanEnumInterfaces(this->d->wlan, nullptr, &interfaces) == ERROR_SUCCESS) {
		for (DWORD i = 0; i < interfaces->dwNumberOfItems; i++) {
			auto& info = interfaces->InterfaceInfo[i]; // NOLINT
			auto id = formatGuid(info.InterfaceGuid);
			wlanInterfaces.append(id);

			DWORD dataSize = 0;
			PWLAN_RADIO_STATE radio = nullptr;
			if (WlanQueryInterface(
			        this->d->wlan,
			        &info.InterfaceGuid,
			        wlan_intf_opcode_radio_state,
			        nullptr,
			        &dataSize,
			        reinterpret_cast<PVOID*>(&radio), // NOLINT
			        nullptr
			    )
			    == ERROR_SUCCESS)
			{
				for (DWORD phy = 0; phy < radio->dwNumberOfPhys; phy++) {
					auto& state = radio->PhyRadioState[phy]; // NOLINT
					if (state.dot11SoftwareRadioState == dot11_radio_state_on) wifiEnabled = true;
					if (state.dot11HardwareRadioState == dot11_radio_state_on) wifiHardwareEnabled = true;
				}

				WlanFreeMemory(radio);
			}

			DeviceData* device = nullptr;
			for (auto& candidate: found) {
				if (candidate.id == id) device = &candidate;
			}
			if (device == nullptr) continue;

			// The network the adapter is associated with, if any.
			QString connectedSsid;
			QString connectedBssid;
			qreal connectedSignal = -1;
			PWLAN_CONNECTION_ATTRIBUTES connection = nullptr;
			if (info.isState == wlan_interface_state_connected
			    && WlanQueryInterface(
			           this->d->wlan,
			           &info.InterfaceGuid,
			           wlan_intf_opcode_current_connection,
			           nullptr,
			           &dataSize,
			           reinterpret_cast<PVOID*>(&connection), // NOLINT
			           nullptr
			       )
			           == ERROR_SUCCESS)
			{
				connectedSsid = ssidOf(connection->wlanAssociationAttributes.dot11Ssid);
				connectedBssid = formatMac(connection->wlanAssociationAttributes.dot11Bssid, 6);
				connectedSignal = static_cast<qreal>(connection->wlanAssociationAttributes.wlanSignalQuality) / 100.0;
				WlanFreeMemory(connection);
			}

			// Listed once per saved profile and once without, so merged by SSID.
			std::map<QString, WifiNetworkData> networks;

			PWLAN_AVAILABLE_NETWORK_LIST available = nullptr;
			if (WlanGetAvailableNetworkList(this->d->wlan, &info.InterfaceGuid, 0, nullptr, &available)
			    == ERROR_SUCCESS)
			{
				for (DWORD n = 0; n < available->dwNumberOfItems; n++) {
					auto& entry = available->Network[n]; // NOLINT
					auto ssid = ssidOf(entry.dot11Ssid);
					if (ssid.isEmpty()) continue; // hidden

					auto& network = networks[ssid];
					network.ssid = ssid;
					network.signalStrength =
					    std::max(network.signalStrength, static_cast<qreal>(entry.wlanSignalQuality) / 100.0);
					network.security = securityOf(entry.bSecurityEnabled != FALSE, entry.dot11DefaultAuthAlgorithm);
					if ((entry.dwFlags & WLAN_AVAILABLE_NETWORK_HAS_PROFILE) != 0) network.known = true;
					if ((entry.dwFlags & WLAN_AVAILABLE_NETWORK_CONNECTED) != 0) network.connected = true;
				}

				WlanFreeMemory(available);
			}

			// The access points behind each network, for an address and a frequency.
			PWLAN_BSS_LIST bssList = nullptr;
			auto bssResult = WlanGetNetworkBssList(
			        this->d->wlan,
			        &info.InterfaceGuid,
			        nullptr,
			        dot11_BSS_type_any,
			        FALSE,
			        nullptr,
			        &bssList
			    );

			if (bssResult != ERROR_SUCCESS) {
				qCDebug(logNetwork) << "Could not list access points, error" << bssResult;
			} else {
				std::map<QString, ULONG> best;

				for (DWORD n = 0; n < bssList->dwNumberOfItems; n++) {
					auto& entry = bssList->wlanBssEntries[n]; // NOLINT
					auto ssid = ssidOf(entry.dot11Ssid);
					auto found = networks.find(ssid);
					if (found == networks.end()) continue;

					auto bssid = formatMac(entry.dot11Bssid, 6);
					auto associated = ssid == connectedSsid && bssid == connectedBssid;

					// The access point in use wins, otherwise the strongest.
					auto quality = associated ? 1000 : entry.uLinkQuality;
					if (best.contains(ssid) && best[ssid] >= quality) continue;

					best[ssid] = quality;
					found->second.bssid = bssid;
					found->second.frequency = entry.ulChCenterFrequency / 1000;
				}

				WlanFreeMemory(bssList);
			}

			for (auto& [ssid, network]: networks) {
				if (ssid == connectedSsid) {
					network.connected = true;
					// The list above is from the last scan. The association is current.
					if (connectedSignal >= 0) network.signalStrength = connectedSignal;
				}

				device->networks.append(network);
			}

			std::ranges::sort(device->networks, [](const WifiNetworkData& a, const WifiNetworkData& b) {
				if (a.connected != b.connected) return a.connected;
				return a.signalStrength > b.signalStrength;
			});
		}

		WlanFreeMemory(interfaces);
	}

	// A Wi-Fi adapter the WLAN service does not list is a virtual one, such as
	// those Windows creates for Wi-Fi Direct.
	found.removeIf([&](const DeviceData& device) { return device.wifi && !wlanInterfaces.contains(device.id); });

	// Devices are kept by adapter id, so a binding to one survives a refresh.
	auto existing = this->mDevices.valueList();
	QList<NetworkDevice*> devices;

	for (const auto& entry: found) {
		NetworkDevice* device = nullptr;

		for (auto* candidate: existing) {
			if (candidate->id() == entry.id) {
				device = candidate;
				break;
			}
		}

		if (device != nullptr) device->update(entry);
		else device = new NetworkDevice(entry, this);

		devices.append(device);
	}

	this->mDevices.diffUpdate(devices);

	for (auto* candidate: existing) {
		if (!devices.contains(candidate)) candidate->deleteLater();
	}

	if (wifiEnabled != this->mWifiEnabled) {
		this->mWifiEnabled = wifiEnabled;
		emit this->wifiEnabledChanged();
	}

	if (wifiHardwareEnabled != this->mWifiHardwareEnabled) {
		this->mWifiHardwareEnabled = wifiHardwareEnabled;
		emit this->wifiHardwareEnabledChanged();
	}

	auto connected = this->anyConnected();
	if (connected != this->lastConnected) {
		this->lastConnected = connected;
		emit this->connectivityChanged();
	}
}

Networking::Networking(QObject* parent): QObject(parent) {
	auto* host = NetworkHost::instance();

	// clang-format off
	QObject::connect(host, &NetworkHost::wifiEnabledChanged, this, &Networking::wifiEnabledChanged);
	QObject::connect(host, &NetworkHost::wifiHardwareEnabledChanged, this, &Networking::wifiHardwareEnabledChanged);
	QObject::connect(host, &NetworkHost::connectivityChanged, this, &Networking::connectivityChanged);
	// clang-format on
}

void Networking::refresh() { NetworkHost::instance()->refresh(); }

void Networking::setWifiEnabled(bool enabled) {
	if (enabled == this->wifiEnabled()) return;
	NetworkHost::instance()->setWifiEnabled(enabled);
}

} // namespace qs::win32::network
