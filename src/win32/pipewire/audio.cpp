#include "audio.hpp"
#include <algorithm>
#include <atomic>

#include <qlist.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qobject.h>
#include <qobjectdefs.h>
#include <qstring.h>
#include <qtypes.h>
#include <qvariant.h>
#include <windows.h>
// windows.h must come first
#include <endpointvolume.h>
#include <mmdeviceapi.h>
#include <propidl.h>

#include "../../core/logcat.hpp"

namespace qs::win32::audio {

namespace {
QS_LOGGING_CATEGORY(logAudio, "quickshell.win32.audio", QtWarningMsg);

// PKEY_Device_FriendlyName and PKEY_Device_DeviceDesc, spelled out so no
// translation unit has to define the property key GUIDs.
const PROPERTYKEY KEY_FRIENDLY_NAME = {
    {0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}},
    14,
};

const PROPERTYKEY KEY_DEVICE_DESCRIPTION = {
    {0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}},
    2,
};

QString deviceId(IMMDevice* device) {
	LPWSTR id = nullptr;
	if (FAILED(device->GetId(&id)) || id == nullptr) return QString();

	auto result = QString::fromWCharArray(id);
	CoTaskMemFree(id);
	return result;
}

QString stringProperty(IPropertyStore* store, const PROPERTYKEY& key) {
	PROPVARIANT value;
	PropVariantInit(&value);

	QString result;
	if (SUCCEEDED(store->GetValue(key, &value)) && value.vt == VT_LPWSTR && value.pwszVal != nullptr) {
		result = QString::fromWCharArray(value.pwszVal);
	}

	PropVariantClear(&value);
	return result;
}

quint32 nextNodeId() {
	static quint32 id = 0;
	return ++id;
}

} // namespace

// Core Audio calls these on its own threads, so they only queue work for the
// thread the objects live on.

class VolumeCallback: public IAudioEndpointVolumeCallback {
public:
	explicit VolumeCallback(PwNodeAudio* audio): audio(audio) {}
	virtual ~VolumeCallback() = default;

	ULONG STDMETHODCALLTYPE AddRef() override { return ++this->references; }

	ULONG STDMETHODCALLTYPE Release() override {
		auto remaining = --this->references;
		if (remaining == 0) delete this;
		return remaining;
	}

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
		if (IsEqualIID(iid, IID_IUnknown) || IsEqualIID(iid, __uuidof(IAudioEndpointVolumeCallback))) {
			*object = static_cast<IAudioEndpointVolumeCallback*>(this);
			this->AddRef();
			return S_OK;
		}

		*object = nullptr;
		return E_NOINTERFACE;
	}

	HRESULT STDMETHODCALLTYPE OnNotify(PAUDIO_VOLUME_NOTIFICATION_DATA data) override {
		if (data == nullptr) return E_POINTER;

		auto volume = static_cast<qreal>(data->fMasterVolume);
		qCDebug(logAudio) << "Volume notification" << volume;
		auto muted = data->bMuted != FALSE;
		auto* target = this->audio;

		// Dropped if the node is gone by the time it would run. The node
		// unregisters this callback before it is destroyed.
		QMetaObject::invokeMethod(
		    target,
		    [target, volume, muted] { target->update(volume, muted); },
		    Qt::QueuedConnection
		);

		return S_OK;
	}

private:
	PwNodeAudio* audio;
	std::atomic<ULONG> references = 1;
};

class DeviceCallback: public IMMNotificationClient {
public:
	explicit DeviceCallback(AudioHost* host): host(host) {}
	virtual ~DeviceCallback() = default;

	ULONG STDMETHODCALLTYPE AddRef() override { return ++this->references; }

	ULONG STDMETHODCALLTYPE Release() override {
		auto remaining = --this->references;
		if (remaining == 0) delete this;
		return remaining;
	}

	HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
		if (IsEqualIID(iid, IID_IUnknown) || IsEqualIID(iid, __uuidof(IMMNotificationClient))) {
			*object = static_cast<IMMNotificationClient*>(this);
			this->AddRef();
			return S_OK;
		}

		*object = nullptr;
		return E_NOINTERFACE;
	}

	// NOLINTBEGIN
	HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD) override { return this->changed(); }
	HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override { return this->changed(); }
	HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override { return this->changed(); }
	HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow, ERole, LPCWSTR) override { return this->changed(); }
	HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override { return S_OK; }
	// NOLINTEND

private:
	HRESULT changed() {
		QMetaObject::invokeMethod(this->host, &AudioHost::refresh, Qt::QueuedConnection);
		return S_OK;
	}

	AudioHost* host;
	std::atomic<ULONG> references = 1;
};

PwNodeAudio::PwNodeAudio(IMMDevice* device, QObject* parent): QObject(parent) {
	void* endpoint = nullptr;
	auto result = device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr, &endpoint);

	if (FAILED(result) || endpoint == nullptr) {
		qCWarning(logAudio) << "Could not open volume control of an audio device, error" << Qt::hex
		                    << static_cast<quint32>(result);
		return;
	}

	this->endpoint = static_cast<IAudioEndpointVolume*>(endpoint);

	float volume = 0;
	BOOL muted = FALSE;
	this->endpoint->GetMasterVolumeLevelScalar(&volume);
	this->endpoint->GetMute(&muted);
	this->mVolume = static_cast<qreal>(volume);
	this->mMuted = muted != FALSE;

	this->callback = new VolumeCallback(this);
	auto registered = this->endpoint->RegisterControlChangeNotify(this->callback);
	if (FAILED(registered)) {
		qCWarning(logAudio) << "Could not subscribe to volume changes, error" << Qt::hex
		                    << static_cast<quint32>(registered);
	}
}

PwNodeAudio::~PwNodeAudio() {
	if (this->endpoint != nullptr) {
		// Returns once no notification is in progress, so the callback cannot
		// refer to this object afterwards.
		if (this->callback != nullptr) this->endpoint->UnregisterControlChangeNotify(this->callback);
		this->endpoint->Release();
	}

	if (this->callback != nullptr) this->callback->Release();
}

void PwNodeAudio::setMuted(bool muted) {
	if (this->endpoint == nullptr) return;
	auto result = this->endpoint->SetMute(muted ? TRUE : FALSE, nullptr);
	if (FAILED(result)) {
		qCWarning(logAudio) << "Could not set mute, error" << Qt::hex << static_cast<quint32>(result);
		return;
	}

	this->update(this->mVolume, muted);
}

void PwNodeAudio::setVolume(qreal volume) {
	if (this->endpoint == nullptr) return;

	// Core Audio has no amplification above full volume.
	auto clamped = std::clamp(volume, 0.0, 1.0);
	auto result = this->endpoint->SetMasterVolumeLevelScalar(static_cast<float>(clamped), nullptr);
	if (FAILED(result)) {
		qCWarning(logAudio) << "Could not set volume, error" << Qt::hex << static_cast<quint32>(result);
		return;
	}

	this->update(clamped, this->mMuted);
}

void PwNodeAudio::update(qreal volume, bool muted) {
	if (muted != this->mMuted) {
		this->mMuted = muted;
		emit this->mutedChanged();
	}

	if (!qFuzzyCompare(volume + 1, this->mVolume + 1)) {
		this->mVolume = volume;
		emit this->volumesChanged();
	}
}

PwNode::PwNode(IMMDevice* device, bool sink, QObject* parent)
    : QObject(parent)
    , mId(nextNodeId())
    , mName(deviceId(device))
    , mSink(sink) {
	IPropertyStore* store = nullptr;
	if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &store)) && store != nullptr) {
		this->mDescription = stringProperty(store, KEY_FRIENDLY_NAME);
		this->mNickname = stringProperty(store, KEY_DEVICE_DESCRIPTION);
		store->Release();
	}

	if (this->mDescription.isEmpty()) this->mDescription = this->mName;

	this->mProperties = {
	    {"node.name", this->mName},
	    {"node.description", this->mDescription},
	    {"node.nick", this->mNickname},
	    {"media.class", sink ? "Audio/Sink" : "Audio/Source"},
	};

	this->mAudio = new PwNodeAudio(device, this);
}

void PwObjectTracker::setObjects(const QList<QObject*>& objects) {
	if (objects == this->mObjects) return;
	this->mObjects = objects;
	emit this->objectsChanged();
}

AudioHost* AudioHost::instance() {
	static auto* instance = new AudioHost(); // NOLINT
	return instance;
}

AudioHost::AudioHost() {
	// Qt has already initialised COM on the thread that owns the application.
	void* enumerator = nullptr;
	auto result = CoCreateInstance(
	    __uuidof(MMDeviceEnumerator),
	    nullptr,
	    CLSCTX_ALL,
	    __uuidof(IMMDeviceEnumerator),
	    &enumerator
	);

	if (FAILED(result) || enumerator == nullptr) {
		qCWarning(logAudio) << "Could not create the audio device enumerator, error" << Qt::hex
		                    << static_cast<quint32>(result);
		return;
	}

	this->enumerator = static_cast<IMMDeviceEnumerator*>(enumerator);
	this->callback = new DeviceCallback(this);
	this->enumerator->RegisterEndpointNotificationCallback(this->callback);

	this->refresh();
}

PwNode* AudioHost::nodeByName(const QString& name) const {
	if (name.isEmpty()) return nullptr;

	for (auto* node: this->mNodes.valueList()) {
		if (node->name() == name) return node;
	}

	return nullptr;
}

void AudioHost::collect(bool sinks, QList<QString>& seen) {
	IMMDeviceCollection* devices = nullptr;
	auto flow = sinks ? eRender : eCapture;

	if (FAILED(this->enumerator->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &devices))
	    || devices == nullptr)
	{
		return;
	}

	UINT count = 0;
	devices->GetCount(&count);

	for (UINT i = 0; i < count; i++) {
		IMMDevice* device = nullptr;
		if (FAILED(devices->Item(i, &device)) || device == nullptr) continue;

		auto name = deviceId(device);
		seen.append(name);

		if (this->nodeByName(name) == nullptr) {
			auto* node = new PwNode(device, sinks, this);
			this->mNodes.insertObject(node);
			qCDebug(logAudio) << "Audio device added:" << node->description();
		}

		device->Release();
	}

	devices->Release();
}

QString AudioHost::defaultDeviceName(bool sink) const {
	IMMDevice* device = nullptr;
	auto flow = sink ? eRender : eCapture;

	// Fails when there is no device of that kind.
	if (FAILED(this->enumerator->GetDefaultAudioEndpoint(flow, eMultimedia, &device))
	    || device == nullptr)
	{
		return QString();
	}

	auto name = deviceId(device);
	device->Release();
	return name;
}

void AudioHost::refresh() {
	if (this->enumerator == nullptr) return;

	QList<QString> seen;
	this->collect(true, seen);
	this->collect(false, seen);

	auto* newSink = this->nodeByName(this->defaultDeviceName(true));
	auto* newSource = this->nodeByName(this->defaultDeviceName(false));

	// Defaults are switched before removed devices are deleted, so nothing is
	// left pointing at a node that is about to go away.
	if (newSink != this->mDefaultSink) {
		this->mDefaultSink = newSink;
		emit this->defaultSinkChanged();
	}

	if (newSource != this->mDefaultSource) {
		this->mDefaultSource = newSource;
		emit this->defaultSourceChanged();
	}

	for (auto* node: QList<PwNode*>(this->mNodes.valueList())) {
		if (!seen.contains(node->name())) {
			qCDebug(logAudio) << "Audio device removed:" << node->description();
			this->mNodes.removeObject(node);
			node->deleteLater();
		}
	}
}

Pipewire::Pipewire(QObject* parent): QObject(parent) {
	auto* host = AudioHost::instance();

	// clang-format off
	QObject::connect(host, &AudioHost::defaultSinkChanged, this, &Pipewire::defaultAudioSinkChanged);
	QObject::connect(host, &AudioHost::defaultSourceChanged, this, &Pipewire::defaultAudioSourceChanged);
	// clang-format on
}

void Pipewire::setPreferredDefaultAudioSink(PwNode* node) {
	if (node == this->defaultAudioSink()) return;
	qCWarning(logAudio) << "Changing the default playback device is not implemented on Windows.";
}

void Pipewire::setPreferredDefaultAudioSource(PwNode* node) {
	if (node == this->defaultAudioSource()) return;
	qCWarning(logAudio) << "Changing the default recording device is not implemented on Windows.";
}

} // namespace qs::win32::audio
