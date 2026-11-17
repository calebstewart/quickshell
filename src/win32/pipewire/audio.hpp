#pragma once

#include <qcontainerfwd.h>
#include <qlist.h>
#include <qobject.h>
#include <qqmlintegration.h>
#include <qstring.h>
#include <qtmetamacros.h>
#include <qtypes.h>
#include <qvariant.h>

#include "../../core/doc.hpp"
#include "../../core/model.hpp"

// Quickshell.Services.Pipewire for Windows.
//
// Presents Core Audio endpoints in the shape of PipeWire nodes: every active
// playback device is a sink node and every active recording device a source
// node, each with a volume and a mute switch. There are no stream nodes yet,
// so per application volume is not available.

struct IMMDevice;
struct IMMDeviceEnumerator;
struct IAudioEndpointVolume;

namespace qs::win32::audio {

class PwNode;
class AudioHost;
class VolumeCallback;
class DeviceCallback;

///! Audio controls of a node.
class PwNodeAudio: public QObject {
	Q_OBJECT;
	// clang-format off
	Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY mutedChanged);
	/// The endpoint's master volume, from 0 to 1.
	Q_PROPERTY(qreal volume READ volume WRITE setVolume NOTIFY volumesChanged);
	Q_PROPERTY(QList<qreal> volumes READ volumes NOTIFY volumesChanged);
	// clang-format on
	QML_NAMED_ELEMENT(PwNodeAudio);
	QML_UNCREATABLE("PwNodeAudio can only be acquired from a PwNode");

public:
	explicit PwNodeAudio(IMMDevice* device, QObject* parent);
	~PwNodeAudio() override;
	Q_DISABLE_COPY_MOVE(PwNodeAudio);

	[[nodiscard]] bool muted() const { return this->mMuted; }
	void setMuted(bool muted);

	[[nodiscard]] qreal volume() const { return this->mVolume; }
	void setVolume(qreal volume);

	[[nodiscard]] QList<qreal> volumes() const { return {this->mVolume}; }

	// Called with what the device reports, from any source of change.
	void update(qreal volume, bool muted);

signals:
	void mutedChanged();
	void volumesChanged();

private:
	IAudioEndpointVolume* endpoint = nullptr;
	VolumeCallback* callback = nullptr;
	qreal mVolume = 0;
	bool mMuted = false;
};

///! An audio device.
class PwNode: public QObject {
	Q_OBJECT;
	// clang-format off
	Q_PROPERTY(quint32 id READ id CONSTANT);
	/// The endpoint's device id.
	Q_PROPERTY(QString name READ name CONSTANT);
	/// The device's friendly name, such as "Speakers (USB Audio)".
	Q_PROPERTY(QString description READ description CONSTANT);
	Q_PROPERTY(QString nickname READ nickname CONSTANT);
	Q_PROPERTY(bool isSink READ isSink CONSTANT);
	Q_PROPERTY(bool isStream READ isStream CONSTANT);
	Q_PROPERTY(bool ready READ ready CONSTANT);
	Q_PROPERTY(qint32 type READ type CONSTANT);
	Q_PROPERTY(QVariantMap properties READ properties CONSTANT);
	Q_PROPERTY(qs::win32::audio::PwNodeAudio* audio READ audio CONSTANT);
	// clang-format on
	QML_NAMED_ELEMENT(PwNode);
	QML_UNCREATABLE("PwNodes cannot be created directly");

public:
	explicit PwNode(IMMDevice* device, bool sink, QObject* parent);

	[[nodiscard]] quint32 id() const { return this->mId; }
	[[nodiscard]] QString name() const { return this->mName; }
	[[nodiscard]] QString description() const { return this->mDescription; }
	[[nodiscard]] QString nickname() const { return this->mNickname; }
	[[nodiscard]] bool isSink() const { return this->mSink; }
	[[nodiscard]] bool isStream() const { return false; }
	[[nodiscard]] bool ready() const { return true; }
	[[nodiscard]] qint32 type() const { return 0; }
	[[nodiscard]] QVariantMap properties() const { return this->mProperties; }
	[[nodiscard]] PwNodeAudio* audio() const { return this->mAudio; }

private:
	quint32 mId = 0;
	QString mName;
	QString mDescription;
	QString mNickname;
	bool mSink = false;
	QVariantMap mProperties;
	PwNodeAudio* mAudio = nullptr;
};

///! Stand-in for PipeWire's object tracker.
/// Core Audio endpoints are always bound, so tracking them does nothing.
class PwObjectTracker: public QObject {
	Q_OBJECT;
	Q_PROPERTY(QList<QObject*> objects READ objects WRITE setObjects NOTIFY objectsChanged);
	QML_ELEMENT;

public:
	explicit PwObjectTracker(QObject* parent = nullptr): QObject(parent) {}

	[[nodiscard]] QList<QObject*> objects() const { return this->mObjects; }
	void setObjects(const QList<QObject*>& objects);

signals:
	void objectsChanged();

private:
	QList<QObject*> mObjects;
};

class AudioHost: public QObject {
	Q_OBJECT;

public:
	static AudioHost* instance();

	[[nodiscard]] ObjectModel<PwNode>* nodes() { return &this->mNodes; }
	[[nodiscard]] PwNode* defaultSink() const { return this->mDefaultSink; }
	[[nodiscard]] PwNode* defaultSource() const { return this->mDefaultSource; }

public slots:
	// Rebuilds the device list. Queued from Core Audio's notification thread.
	void refresh();

signals:
	void defaultSinkChanged();
	void defaultSourceChanged();

private:
	explicit AudioHost();

	[[nodiscard]] PwNode* nodeByName(const QString& name) const;
	void collect(bool sinks, QList<QString>& seen);
	[[nodiscard]] QString defaultDeviceName(bool sink) const;

	IMMDeviceEnumerator* enumerator = nullptr;
	DeviceCallback* callback = nullptr;
	ObjectModel<PwNode> mNodes {this};
	PwNode* mDefaultSink = nullptr;
	PwNode* mDefaultSource = nullptr;
};

///! Audio devices.
/// Core Audio endpoints, in the shape of the PipeWire singleton.
class Pipewire: public QObject {
	Q_OBJECT;
	// clang-format off
	/// Every active playback and recording device.
	QSDOC_TYPE_OVERRIDE(ObjectModel<qs::win32::audio::PwNode>*);
	Q_PROPERTY(UntypedObjectModel* nodes READ nodes CONSTANT);
	Q_PROPERTY(qs::win32::audio::PwNode* defaultAudioSink READ defaultAudioSink NOTIFY defaultAudioSinkChanged);
	Q_PROPERTY(qs::win32::audio::PwNode* defaultAudioSource READ defaultAudioSource NOTIFY defaultAudioSourceChanged);
	/// Changing the default device is not implemented on Windows yet. Assigning these only logs a warning.
	Q_PROPERTY(qs::win32::audio::PwNode* preferredDefaultAudioSink READ defaultAudioSink WRITE setPreferredDefaultAudioSink NOTIFY defaultAudioSinkChanged);
	Q_PROPERTY(qs::win32::audio::PwNode* preferredDefaultAudioSource READ defaultAudioSource WRITE setPreferredDefaultAudioSource NOTIFY defaultAudioSourceChanged);
	Q_PROPERTY(bool ready READ ready CONSTANT);
	// clang-format on
	QML_ELEMENT;
	QML_SINGLETON;

public:
	explicit Pipewire(QObject* parent = nullptr);

	[[nodiscard]] ObjectModel<PwNode>* nodes() { return AudioHost::instance()->nodes(); } // NOLINT
	[[nodiscard]] PwNode* defaultAudioSink() const { return AudioHost::instance()->defaultSink(); } // NOLINT
	[[nodiscard]] PwNode* defaultAudioSource() const { return AudioHost::instance()->defaultSource(); } // NOLINT
	[[nodiscard]] bool ready() const { return true; } // NOLINT

	void setPreferredDefaultAudioSink(PwNode* node);
	void setPreferredDefaultAudioSource(PwNode* node);

signals:
	void defaultAudioSinkChanged();
	void defaultAudioSourceChanged();
};

} // namespace qs::win32::audio
