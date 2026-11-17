#include "mpris.hpp"
#include <algorithm>

#include <qdatetime.h>
#include <qimage.h>
#include <qlist.h>
#include <qlogging.h>
#include <qmetatype.h>
#include <qobject.h>
#include <qsize.h>
#include <qstring.h>
#include <qthread.h>
#include <qtypes.h>
#include <qvariant.h>

namespace qs::win32::media {

QImage
MediaArtHandle::requestImage(const QString& /*unused*/, QSize* size, const QSize& requestedSize) {
	auto image = this->player->art;

	if (image.isNull()) {
		image = QImage(1, 1, QImage::Format_ARGB32_Premultiplied);
		image.fill(Qt::transparent);
	}

	if (requestedSize.isValid() && !requestedSize.isEmpty() && image.size() != requestedSize) {
		image = image.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
	}

	if (size != nullptr) *size = image.size();
	return image;
}

MprisPlayer::MprisPlayer(const MediaSnapshot& snapshot, QObject* parent)
    : QObject(parent)
    , state(snapshot)
    , mIdentity(snapshot.id) {
	if (this->mIdentity.endsWith(".exe", Qt::CaseInsensitive)) this->mIdentity.chop(4);

	if (!snapshot.art.isEmpty()) {
		this->art = QImage::fromData(snapshot.art);
		this->artHandle.imageChanged();
	}
}

QString MprisPlayer::trackArtUrl() const {
	return this->art.isNull() ? QString() : this->artHandle.url();
}

QVariantMap MprisPlayer::metadata() const {
	return {
	    {"xesam:title", this->state.title},
	    {"xesam:artist", QStringList {this->state.artist}},
	    {"xesam:album", this->state.album},
	    {"mpris:length", static_cast<qint64>(this->state.length * 1000000)},
	    {"mpris:artUrl", this->trackArtUrl()},
	};
}

bool MprisPlayer::canControl() const {
	return this->state.canPlay || this->state.canPause || this->state.canToggle || this->state.canNext
	    || this->state.canPrevious;
}

qreal MprisPlayer::position() const {
	auto position = this->state.position;

	// The session reports a position and when it was true, not a running clock.
	if (this->isPlaying() && this->state.sampledAt > 0) {
		auto elapsed = static_cast<qreal>(QDateTime::currentMSecsSinceEpoch() - this->state.sampledAt);
		position += elapsed / 1000.0 * this->state.rate;
	}

	if (this->state.length > 0) position = std::min(position, this->state.length);
	return std::max(position, 0.0);
}

void MprisPlayer::send(MediaCommand command, qreal argument) {
	MediaHost::instance()->send(this->state.id, command, argument);
}

void MprisPlayer::play() { this->send(MediaCommand::Play); }
void MprisPlayer::pause() { this->send(MediaCommand::Pause); }
void MprisPlayer::stop() { this->send(MediaCommand::Stop); }
void MprisPlayer::togglePlaying() { this->send(MediaCommand::Toggle); }
void MprisPlayer::next() { this->send(MediaCommand::Next); }
void MprisPlayer::previous() { this->send(MediaCommand::Previous); }
void MprisPlayer::seek(qreal offset) { this->setPosition(this->position() + offset); }
void MprisPlayer::raise() {}
void MprisPlayer::quit() {}

void MprisPlayer::setPosition(qreal position) {
	this->send(MediaCommand::SetPosition, std::max(position, 0.0));
}

void MprisPlayer::setPlaying(bool playing) {
	if (playing) this->play();
	else this->pause();
}

void MprisPlayer::setPlaybackState(MprisPlaybackState::Enum playbackState) {
	switch (playbackState) {
	case MprisPlaybackState::Playing: this->play(); break;
	case MprisPlaybackState::Paused: this->pause(); break;
	case MprisPlaybackState::Stopped: this->stop(); break;
	}
}

void MprisPlayer::setShuffle(bool shuffle) {
	this->send(MediaCommand::SetShuffle, shuffle ? 1 : 0);
}

void MprisPlayer::setLoopState(MprisLoopState::Enum loopState) {
	this->send(MediaCommand::SetLoop, static_cast<qreal>(loopState));
}

void MprisPlayer::update(const MediaSnapshot& snapshot) {
	auto old = this->state;
	this->state = snapshot;

	auto trackChanged = old.title != snapshot.title || old.artist != snapshot.artist;

	if (old.artKey != snapshot.artKey) {
		this->art = snapshot.art.isEmpty() ? QImage() : QImage::fromData(snapshot.art);
		this->artHandle.imageChanged();
		emit this->trackArtUrlChanged();
	}

	if (old.title != snapshot.title) emit this->trackTitleChanged();
	if (old.artist != snapshot.artist) emit this->trackArtistChanged();
	if (old.album != snapshot.album) emit this->trackAlbumChanged();

	if (trackChanged || old.album != snapshot.album || old.artKey != snapshot.artKey
	    || !qFuzzyCompare(old.length + 1, snapshot.length + 1))
	{
		emit this->metadataChanged();
	}

	if (old.playbackState != snapshot.playbackState) emit this->playbackStateChanged();

	// clang-format off
	if (old.canPlay != snapshot.canPlay || old.canPause != snapshot.canPause
	    || old.canToggle != snapshot.canToggle || old.canNext != snapshot.canNext
	    || old.canPrevious != snapshot.canPrevious || old.canSeek != snapshot.canSeek
	    || old.shuffleSupported != snapshot.shuffleSupported
	    || old.loopSupported != snapshot.loopSupported)
	{
		emit this->capabilitiesChanged();
	}
	// clang-format on

	if (!qFuzzyCompare(old.length + 1, snapshot.length + 1)) emit this->lengthChanged();
	if (!qFuzzyCompare(old.rate + 1, snapshot.rate + 1)) emit this->rateChanged();
	if (old.shuffle != snapshot.shuffle) emit this->shuffleChanged();
	if (old.loopState != snapshot.loopState) emit this->loopStateChanged();

	// The reported position moves with every update.
	emit this->positionChanged();

	if (trackChanged) {
		emit this->trackChanged();
		emit this->postTrackChanged();
	}
}

MediaHost* MediaHost::instance() {
	static auto* instance = new MediaHost(); // NOLINT
	return instance;
}

MediaHost::MediaHost() {
	qRegisterMetaType<MediaSnapshot>();
	qRegisterMetaType<QList<MediaSnapshot>>();
	qRegisterMetaType<MediaCommand>();

	this->worker = new MediaWorker();
	this->worker->moveToThread(&this->thread);

	QObject::connect(
	    this->worker,
	    &MediaWorker::sessionsChanged,
	    this,
	    &MediaHost::onSessionsChanged
	);

	QObject::connect(&this->thread, &QThread::started, this->worker, &MediaWorker::start);

	this->thread.setObjectName("media sessions");
	this->thread.start();
}

void MediaHost::send(const QString& id, MediaCommand command, qreal argument) {
	QMetaObject::invokeMethod(
	    this->worker,
	    [worker = this->worker, id, command, argument] { worker->command(id, command, argument); },
	    Qt::QueuedConnection
	);
}

void MediaHost::onSessionsChanged(const QList<MediaSnapshot>& sessions) {
	// Removed first, so a session that restarted under the same id is recreated.
	for (auto* player: QList<MprisPlayer*>(this->mPlayers.valueList())) {
		auto present = std::ranges::any_of(sessions, [&](const MediaSnapshot& snapshot) {
			return snapshot.id == player->id();
		});

		if (!present) {
			this->mPlayers.removeObject(player);
			player->deleteLater();
		}
	}

	for (const auto& snapshot: sessions) {
		MprisPlayer* existing = nullptr;

		for (auto* player: this->mPlayers.valueList()) {
			if (player->id() == snapshot.id) {
				existing = player;
				break;
			}
		}

		if (existing != nullptr) {
			existing->update(snapshot);
		} else {
			this->mPlayers.insertObject(new MprisPlayer(snapshot, this));
		}
	}
}

} // namespace qs::win32::media
