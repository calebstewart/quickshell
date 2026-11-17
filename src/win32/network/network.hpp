#pragma once

#include <qcontainerfwd.h>
#include <qlist.h>
#include <qobject.h>
#include <qqmlintegration.h>
#include <qstring.h>
#include <qstringlist.h>
#include <qtimer.h>
#include <qtmetamacros.h>
#include <qtypes.h>

#include "../../core/doc.hpp"
#include "../../core/model.hpp"

// Quickshell.Networking for Windows.
//
// Presents the machine's network adapters and the Wi-Fi networks in range,
// using the names of the Linux module where the two have the same idea: a
// device has a name, an address and a state, a Wi-Fi device has networks, a
// network has a signal strength and a security type. Each device also carries
// its IP configuration, which the Linux module leaves to NetworkManager
// settings objects.
//
// This is read only. Connecting to, disconnecting from and forgetting a
// network are not implemented and log a warning. The Wi-Fi radio can be
// switched and a scan requested.

namespace qs::win32::network {

///! Connection state.
class ConnectionState: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;

public:
	enum Enum : quint8 {
		Unknown = 0,
		Connecting = 1,
		Connected = 2,
		Disconnecting = 3,
		Disconnected = 4,
	};
	Q_ENUM(Enum);
};

///! Type of network device.
class DeviceType: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;

public:
	enum Enum : quint8 {
		None = 0,
		Wifi = 1,
		Wired = 2,
	};
	Q_ENUM(Enum);
};

///! Wi-Fi security type.
class WifiSecurityType: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;

public:
	enum Enum : quint8 {
		Wpa3SuiteB192 = 0,
		Sae = 1,
		Wpa2Eap = 2,
		Wpa2Psk = 3,
		WpaEap = 4,
		WpaPsk = 5,
		StaticWep = 6,
		DynamicWep = 7,
		Leap = 8,
		Owe = 9,
		Open = 10,
		Unknown = 11,
	};
	Q_ENUM(Enum);

	/// A short name for a security type, such as "WPA2" or "Open".
	Q_INVOKABLE static QString toString(qs::win32::network::WifiSecurityType::Enum type);
};

///! Overall connectivity.
class NetworkConnectivity: public QObject {
	Q_OBJECT;
	QML_ELEMENT;
	QML_SINGLETON;

public:
	enum Enum : quint8 {
		Unknown = 0,
		None = 1,
		Portal = 2,
		Limited = 3,
		Full = 4,
	};
	Q_ENUM(Enum);
};

class NetworkDevice;

// What is known about one Wi-Fi network at one moment.
struct WifiNetworkData {
	QString ssid;
	QString bssid;
	qreal signalStrength = 0;
	quint32 frequency = 0;
	WifiSecurityType::Enum security = WifiSecurityType::Unknown;
	bool connected = false;
	bool known = false;
};

///! A Wi-Fi network in range.
class WifiNetwork: public QObject {
	Q_OBJECT;
	// clang-format off
	/// The network's SSID.
	Q_PROPERTY(QString name READ name CONSTANT);
	Q_PROPERTY(qs::win32::network::NetworkDevice* device READ device CONSTANT);
	Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged);
	/// True if Windows has a saved profile for the network.
	Q_PROPERTY(bool known READ known NOTIFY knownChanged);
	Q_PROPERTY(qs::win32::network::ConnectionState::Enum state READ state NOTIFY connectedChanged);
	Q_PROPERTY(bool stateChanging READ no CONSTANT);
	/// Signal strength from 0 to 1.
	Q_PROPERTY(qreal signalStrength READ signalStrength NOTIFY signalStrengthChanged);
	Q_PROPERTY(qs::win32::network::WifiSecurityType::Enum security READ security NOTIFY securityChanged);
	/// Address of the strongest access point for the network. Not in the Linux module.
	Q_PROPERTY(QString bssid READ bssid NOTIFY bssidChanged);
	/// Frequency of that access point in MHz. Not in the Linux module.
	Q_PROPERTY(quint32 frequency READ frequency NOTIFY frequencyChanged);
	// clang-format on
	QML_ELEMENT;
	QML_UNCREATABLE("WifiNetworks can only be acquired from a WifiDevice");

public:
	explicit WifiNetwork(const WifiNetworkData& data, NetworkDevice* device);

	/// Not implemented on Windows yet. Logs a warning.
	Q_INVOKABLE void connect();
	/// Not implemented on Windows yet. Logs a warning.
	Q_INVOKABLE void connectWithPsk(const QString& psk);
	/// Not implemented on Windows yet. Logs a warning.
	Q_INVOKABLE void disconnect();
	/// Not implemented on Windows yet. Logs a warning.
	Q_INVOKABLE void forget();

	void update(const WifiNetworkData& data);

	[[nodiscard]] QString name() const { return this->data.ssid; }
	[[nodiscard]] NetworkDevice* device() const { return this->mDevice; }
	[[nodiscard]] bool connected() const { return this->data.connected; }
	[[nodiscard]] bool known() const { return this->data.known; }
	[[nodiscard]] ConnectionState::Enum state() const {
		return this->data.connected ? ConnectionState::Connected : ConnectionState::Disconnected;
	}
	[[nodiscard]] bool no() const { return false; }
	[[nodiscard]] qreal signalStrength() const { return this->data.signalStrength; }
	[[nodiscard]] WifiSecurityType::Enum security() const { return this->data.security; }
	[[nodiscard]] QString bssid() const { return this->data.bssid; }
	[[nodiscard]] quint32 frequency() const { return this->data.frequency; }

signals:
	void connectedChanged();
	void knownChanged();
	void signalStrengthChanged();
	void securityChanged();
	void bssidChanged();
	void frequencyChanged();

private:
	WifiNetworkData data;
	NetworkDevice* mDevice;
};

// What is known about one adapter at one moment.
struct DeviceData {
	// The adapter's GUID, which identifies it across refreshes.
	QString id;
	QString name;
	QString description;
	QString address;
	bool wifi = false;
	bool connected = false;
	quint32 linkSpeed = 0;
	QString ipAddress;
	QString subnet;
	QString gateway;
	QStringList dns;
	QList<WifiNetworkData> networks;
};

///! A network adapter.
class NetworkDevice: public QObject {
	Q_OBJECT;
	// clang-format off
	Q_PROPERTY(qs::win32::network::DeviceType::Enum type READ type CONSTANT);
	/// The adapter's name as Windows shows it, such as "Wi-Fi" or "Ethernet".
	Q_PROPERTY(QString name READ name NOTIFY nameChanged);
	/// The adapter's hardware description. Not in the Linux module.
	Q_PROPERTY(QString description READ description NOTIFY nameChanged);
	/// The Wi-Fi networks in range. Empty for a wired device.
	QSDOC_TYPE_OVERRIDE(ObjectModel<qs::win32::network::WifiNetwork>*);
	Q_PROPERTY(UntypedObjectModel* networks READ networks CONSTANT);
	/// The adapter's hardware address.
	Q_PROPERTY(QString address READ address NOTIFY addressChanged);
	Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged);
	Q_PROPERTY(qs::win32::network::ConnectionState::Enum state READ state NOTIFY connectedChanged);
	/// Link speed in megabits per second, or 0 if not connected.
	Q_PROPERTY(quint32 linkSpeed READ linkSpeed NOTIFY linkSpeedChanged);
	/// True if a cable is plugged in or the radio is associated.
	Q_PROPERTY(bool hasLink READ connected NOTIFY connectedChanged);
	// The IP configuration. Not in the Linux module.
	Q_PROPERTY(QString ipAddress READ ipAddress NOTIFY ipChanged);
	Q_PROPERTY(QString subnet READ subnet NOTIFY ipChanged);
	Q_PROPERTY(QString gateway READ gateway NOTIFY ipChanged);
	Q_PROPERTY(QStringList dns READ dns NOTIFY ipChanged);
	/// For a Wi-Fi device, keeps asking the adapter to scan while true.
	Q_PROPERTY(bool scannerEnabled READ scannerEnabled WRITE setScannerEnabled NOTIFY scannerEnabledChanged);
	// clang-format on
	QML_ELEMENT;
	QML_UNCREATABLE("NetworkDevices can only be acquired from Networking");

public:
	explicit NetworkDevice(const DeviceData& data, QObject* parent);

	/// Not implemented on Windows yet. Logs a warning.
	Q_INVOKABLE void disconnect();
	/// Asks a Wi-Fi adapter to look for networks now.
	Q_INVOKABLE void scan();

	void update(const DeviceData& data);

	[[nodiscard]] QString id() const { return this->data.id; }
	[[nodiscard]] DeviceType::Enum type() const { return this->data.wifi ? DeviceType::Wifi : DeviceType::Wired; }
	[[nodiscard]] QString name() const { return this->data.name; }
	[[nodiscard]] QString description() const { return this->data.description; }
	[[nodiscard]] ObjectModel<WifiNetwork>* networks() { return &this->mNetworks; }
	[[nodiscard]] QString address() const { return this->data.address; }
	[[nodiscard]] bool connected() const { return this->data.connected; }
	[[nodiscard]] ConnectionState::Enum state() const {
		return this->data.connected ? ConnectionState::Connected : ConnectionState::Disconnected;
	}
	[[nodiscard]] quint32 linkSpeed() const { return this->data.linkSpeed; }
	[[nodiscard]] QString ipAddress() const { return this->data.ipAddress; }
	[[nodiscard]] QString subnet() const { return this->data.subnet; }
	[[nodiscard]] QString gateway() const { return this->data.gateway; }
	[[nodiscard]] QStringList dns() const { return this->data.dns; }
	[[nodiscard]] bool scannerEnabled() const { return this->mScannerEnabled; }
	void setScannerEnabled(bool enabled);

signals:
	void nameChanged();
	void addressChanged();
	void connectedChanged();
	void linkSpeedChanged();
	void ipChanged();
	void scannerEnabledChanged();

private:
	DeviceData data;
	ObjectModel<WifiNetwork> mNetworks {this};
	bool mScannerEnabled = false;
};

class NetworkHost: public QObject {
	Q_OBJECT;

public:
	static NetworkHost* instance();

	[[nodiscard]] ObjectModel<NetworkDevice>* devices() { return &this->mDevices; }
	[[nodiscard]] bool wifiEnabled() const { return this->mWifiEnabled; }
	[[nodiscard]] bool wifiHardwareEnabled() const { return this->mWifiHardwareEnabled; }
	[[nodiscard]] bool anyConnected() const;
	void setWifiEnabled(bool enabled);
	void scan(const QString& deviceId);

	// Asks for a refresh from any thread. Requests made while one is pending are merged.
	void requestRefresh();

public slots:
	void refresh();

signals:
	void wifiEnabledChanged();
	void wifiHardwareEnabledChanged();
	void connectivityChanged();

private:
	explicit NetworkHost();

	struct Private;
	Private* d;

	ObjectModel<NetworkDevice> mDevices {this};
	bool mWifiEnabled = false;
	bool mWifiHardwareEnabled = false;
	bool lastConnected = false;
	QTimer timer;
	QTimer scanTimer;
};

///! Networking.
class Networking: public QObject {
	Q_OBJECT;
	// clang-format off
	/// The machine's physical network adapters.
	QSDOC_TYPE_OVERRIDE(ObjectModel<qs::win32::network::NetworkDevice>*);
	Q_PROPERTY(UntypedObjectModel* devices READ devices CONSTANT);
	/// Whether the Wi-Fi radio is switched on. Can be assigned.
	Q_PROPERTY(bool wifiEnabled READ wifiEnabled WRITE setWifiEnabled NOTIFY wifiEnabledChanged);
	Q_PROPERTY(bool wifiHardwareEnabled READ wifiHardwareEnabled NOTIFY wifiHardwareEnabledChanged);
	/// Full if any adapter is connected, otherwise None. Windows' own
	/// connectivity check is not consulted.
	Q_PROPERTY(qs::win32::network::NetworkConnectivity::Enum connectivity READ connectivity NOTIFY connectivityChanged);
	// clang-format on
	QML_ELEMENT;
	QML_SINGLETON;

public:
	explicit Networking(QObject* parent = nullptr);

	/// Reads the adapters and networks again now.
	Q_INVOKABLE void refresh();

	[[nodiscard]] ObjectModel<NetworkDevice>* devices() const { return NetworkHost::instance()->devices(); } // NOLINT
	[[nodiscard]] bool wifiEnabled() const { return NetworkHost::instance()->wifiEnabled(); } // NOLINT
	void setWifiEnabled(bool enabled);
	[[nodiscard]] bool wifiHardwareEnabled() const { return NetworkHost::instance()->wifiHardwareEnabled(); } // NOLINT
	[[nodiscard]] NetworkConnectivity::Enum connectivity() const { // NOLINT
		return NetworkHost::instance()->anyConnected() ? NetworkConnectivity::Full : NetworkConnectivity::None;
	}

signals:
	void wifiEnabledChanged();
	void wifiHardwareEnabledChanged();
	void connectivityChanged();
};

} // namespace qs::win32::network
