#pragma once

#include <qbytearray.h>
#include <qcontainerfwd.h>
#include <qimage.h>
#include <qlist.h>
#include <qmetatype.h>
#include <qobject.h>
#include <qqmlintegration.h>
#include <qquickimageprovider.h>
#include <qstring.h>
#include <qthread.h>
#include <qtmetamacros.h>
#include <qtypes.h>
#include <qvariant.h>

#include "../../core/doc.hpp"
#include "../../core/imageprovider.hpp"
#include "../../core/model.hpp"

// Quickshell.Services.Mpris for Windows.
//
// Presents the system's media sessions (what the volume flyout and media keys
// control) in the shape of MPRIS players. The Windows Runtime calls block, so
// they run on a worker thread that hands plain snapshots to the UI thread.

namespace qs::win32::media {

///! Playback state of an MprisPlayer.
namespace MprisPlaybackState { // NOLINT
Q_NAMESPACE;
QML_ELEMENT;

enum Enum : quint8 {
	Stopped = 0,
	Playing = 1,
	Paused = 2,
};
Q_ENUM_NS(Enum);

} // namespace MprisPlaybackState

///! Loop state of an MprisPlayer.
namespace MprisLoopState { // NOLINT
Q_NAMESPACE;
QML_ELEMENT;

enum Enum : quint8 {
	None = 0,
	Track = 1,
	Playlist = 2,
};
Q_ENUM_NS(Enum);

} // namespace MprisLoopState

// Everything known about one media session at one moment. Plain data, so it
// can cross threads.
struct MediaSnapshot {
	QString id;
	QString title;
	QString artist;
	QString album;
	quint8 playbackState = 0;
	quint8 loopState = 0;
	bool shuffle = false;
	bool canPlay = false;
	bool canPause = false;
	bool canToggle = false;
	bool canNext = false;
	bool canPrevious = false;
	bool canSeek = false;
	bool canStop = false;
	bool shuffleSupported = false;
	bool loopSupported = false;
	// Seconds. The position is as of sampledAt, in milliseconds since the epoch.
	qreal position = 0;
	qreal length = 0;
	qreal rate = 1;
	qint64 sampledAt = 0;
	// Encoded image, empty if the session has no artwork. artKey changes with it.
	QByteArray art;
	QString artKey;
};

enum class MediaCommand : quint8 {
	Play,
	Pause,
	Stop,
	Toggle,
	Next,
	Previous,
	// Argument: absolute position in seconds.
	SetPosition,
	// Argument: 0 or 1.
	SetShuffle,
	// Argument: a MprisLoopState value.
	SetLoop,
};

// Lives on the worker thread and owns everything from the Windows Runtime.
class MediaWorker: public QObject {
	Q_OBJECT;

public:
	explicit MediaWorker();
	~MediaWorker() override;
	Q_DISABLE_COPY_MOVE(MediaWorker);

	// Asks for a refresh from any thread. Requests made while one is pending are merged.
	void requestRefresh();

public slots:
	void start();
	void refresh();
	void command(const QString& id, qs::win32::media::MediaCommand command, qreal argument);

signals:
	void sessionsChanged(const QList<qs::win32::media::MediaSnapshot>& sessions);

private:
	struct Private;
	Private* d;
};

class MprisPlayer;

class MediaArtHandle: public QsIndexedImageHandle {
public:
	explicit MediaArtHandle(MprisPlayer* player)
	    : QsIndexedImageHandle(QQmlImageProviderBase::Image)
	    , player(player) {}

	QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;

private:
	MprisPlayer* player;
};

///! A media session.
class MprisPlayer: public QObject {
	Q_OBJECT;
	// clang-format off
	/// The application that owns the session, without a trailing ".exe".
	Q_PROPERTY(QString identity READ identity CONSTANT);
	/// The session's application id as Windows reports it.
	Q_PROPERTY(QString dbusName READ id CONSTANT);
	Q_PROPERTY(QString desktopEntry READ identity CONSTANT);
	Q_PROPERTY(QString uniqueId READ id CONSTANT);
	Q_PROPERTY(QString trackTitle READ trackTitle NOTIFY trackTitleChanged);
	Q_PROPERTY(QString trackArtist READ trackArtist NOTIFY trackArtistChanged);
	Q_PROPERTY(QString trackAlbum READ trackAlbum NOTIFY trackAlbumChanged);
	/// Image source for the artwork, or an empty string if there is none.
	Q_PROPERTY(QString trackArtUrl READ trackArtUrl NOTIFY trackArtUrlChanged);
	Q_PROPERTY(QVariantMap metadata READ metadata NOTIFY metadataChanged);
	Q_PROPERTY(qs::win32::media::MprisPlaybackState::Enum playbackState READ playbackState WRITE setPlaybackState NOTIFY playbackStateChanged);
	Q_PROPERTY(bool isPlaying READ isPlaying WRITE setPlaying NOTIFY playbackStateChanged);
	Q_PROPERTY(bool canControl READ canControl NOTIFY capabilitiesChanged);
	Q_PROPERTY(bool canPlay READ canPlay NOTIFY capabilitiesChanged);
	Q_PROPERTY(bool canPause READ canPause NOTIFY capabilitiesChanged);
	Q_PROPERTY(bool canTogglePlaying READ canTogglePlaying NOTIFY capabilitiesChanged);
	Q_PROPERTY(bool canGoNext READ canGoNext NOTIFY capabilitiesChanged);
	Q_PROPERTY(bool canGoPrevious READ canGoPrevious NOTIFY capabilitiesChanged);
	Q_PROPERTY(bool canSeek READ canSeek NOTIFY capabilitiesChanged);
	Q_PROPERTY(bool canRaise READ no CONSTANT);
	Q_PROPERTY(bool canQuit READ no CONSTANT);
	Q_PROPERTY(bool positionSupported READ positionSupported NOTIFY lengthChanged);
	Q_PROPERTY(bool lengthSupported READ positionSupported NOTIFY lengthChanged);
	/// Position in seconds. It does not notify as playback advances: emit
	/// positionChanged() to make bindings read it again.
	Q_PROPERTY(qreal position READ position WRITE setPosition NOTIFY positionChanged);
	Q_PROPERTY(qreal length READ length NOTIFY lengthChanged);
	Q_PROPERTY(qreal rate READ rate NOTIFY rateChanged);
	/// Media sessions have no volume of their own.
	Q_PROPERTY(bool volumeSupported READ no CONSTANT);
	Q_PROPERTY(qreal volume READ volume CONSTANT);
	Q_PROPERTY(bool shuffleSupported READ shuffleSupported NOTIFY capabilitiesChanged);
	Q_PROPERTY(bool shuffle READ shuffle WRITE setShuffle NOTIFY shuffleChanged);
	Q_PROPERTY(bool loopSupported READ loopSupported NOTIFY capabilitiesChanged);
	Q_PROPERTY(qs::win32::media::MprisLoopState::Enum loopState READ loopState WRITE setLoopState NOTIFY loopStateChanged);
	// clang-format on
	QML_NAMED_ELEMENT(MprisPlayer);
	QML_UNCREATABLE("MprisPlayers can only be acquired from Mpris");

public:
	explicit MprisPlayer(const MediaSnapshot& snapshot, QObject* parent);

	Q_INVOKABLE void play();
	Q_INVOKABLE void pause();
	Q_INVOKABLE void stop();
	Q_INVOKABLE void togglePlaying();
	Q_INVOKABLE void next();
	Q_INVOKABLE void previous();
	/// Moves the position by an offset in seconds.
	Q_INVOKABLE void seek(qreal offset);
	/// Not available for media sessions.
	Q_INVOKABLE void raise();
	/// Not available for media sessions.
	Q_INVOKABLE void quit();

	void update(const MediaSnapshot& snapshot);

	[[nodiscard]] QString id() const { return this->state.id; }
	[[nodiscard]] QString identity() const { return this->mIdentity; }
	[[nodiscard]] QString trackTitle() const { return this->state.title; }
	[[nodiscard]] QString trackArtist() const { return this->state.artist; }
	[[nodiscard]] QString trackAlbum() const { return this->state.album; }
	[[nodiscard]] QString trackArtUrl() const;
	[[nodiscard]] QVariantMap metadata() const;

	[[nodiscard]] MprisPlaybackState::Enum playbackState() const {
		return static_cast<MprisPlaybackState::Enum>(this->state.playbackState);
	}
	void setPlaybackState(MprisPlaybackState::Enum playbackState);
	[[nodiscard]] bool isPlaying() const { return this->state.playbackState == MprisPlaybackState::Playing; }
	void setPlaying(bool playing);

	[[nodiscard]] bool canControl() const;
	[[nodiscard]] bool canPlay() const { return this->state.canPlay; }
	[[nodiscard]] bool canPause() const { return this->state.canPause; }
	[[nodiscard]] bool canTogglePlaying() const { return this->state.canToggle; }
	[[nodiscard]] bool canGoNext() const { return this->state.canNext; }
	[[nodiscard]] bool canGoPrevious() const { return this->state.canPrevious; }
	[[nodiscard]] bool canSeek() const { return this->state.canSeek; }
	[[nodiscard]] bool no() const { return false; }

	[[nodiscard]] bool positionSupported() const { return this->state.length > 0; }
	[[nodiscard]] qreal position() const;
	void setPosition(qreal position);
	[[nodiscard]] qreal length() const { return this->state.length; }
	[[nodiscard]] qreal rate() const { return this->state.rate; }
	[[nodiscard]] qreal volume() const { return 1; }

	[[nodiscard]] bool shuffleSupported() const { return this->state.shuffleSupported; }
	[[nodiscard]] bool shuffle() const { return this->state.shuffle; }
	void setShuffle(bool shuffle);
	[[nodiscard]] bool loopSupported() const { return this->state.loopSupported; }
	[[nodiscard]] MprisLoopState::Enum loopState() const {
		return static_cast<MprisLoopState::Enum>(this->state.loopState);
	}
	void setLoopState(MprisLoopState::Enum loopState);

signals:
	void trackTitleChanged();
	void trackArtistChanged();
	void trackAlbumChanged();
	void trackArtUrlChanged();
	void metadataChanged();
	void playbackStateChanged();
	void capabilitiesChanged();
	void positionChanged();
	void lengthChanged();
	void rateChanged();
	void shuffleChanged();
	void loopStateChanged();
	/// Emitted after the track's title or artist changed and the other
	/// properties have been updated.
	void postTrackChanged();
	QSDOC_HIDE void trackChanged();

private:
	void send(MediaCommand command, qreal argument = 0);

	MediaSnapshot state;
	QString mIdentity;
	QImage art;
	MediaArtHandle artHandle {this};

	friend class MediaArtHandle;
};

class MediaHost: public QObject {
	Q_OBJECT;

public:
	static MediaHost* instance();

	[[nodiscard]] ObjectModel<MprisPlayer>* players() { return &this->mPlayers; }
	void send(const QString& id, MediaCommand command, qreal argument);

private slots:
	void onSessionsChanged(const QList<qs::win32::media::MediaSnapshot>& sessions);

private:
	explicit MediaHost();

	QThread thread;
	MediaWorker* worker = nullptr;
	ObjectModel<MprisPlayer> mPlayers {this};
};

///! Media players.
/// The system's media sessions, in the shape of the Mpris singleton.
class Mpris: public QObject {
	Q_OBJECT;
	QSDOC_TYPE_OVERRIDE(ObjectModel<qs::win32::media::MprisPlayer>*);
	Q_PROPERTY(UntypedObjectModel* players READ players CONSTANT);
	QML_ELEMENT;
	QML_SINGLETON;

public:
	explicit Mpris(QObject* parent = nullptr): QObject(parent) {}

	[[nodiscard]] ObjectModel<MprisPlayer>* players() { return MediaHost::instance()->players(); } // NOLINT
};

} // namespace qs::win32::media

Q_DECLARE_METATYPE(qs::win32::media::MediaSnapshot);
Q_DECLARE_METATYPE(qs::win32::media::MediaCommand);
