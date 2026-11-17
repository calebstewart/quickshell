// clang-format off
// The Windows Runtime headers come first, before Qt defines its keyword macros.
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Media.h>
#include <winrt/Windows.Storage.Streams.h>
// clang-format on

#include <atomic>
#include <chrono>
#include <map>

#include <qbytearray.h>
#include <qdatetime.h>
#include <qlist.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qobject.h>
#include <qobjectdefs.h>
#include <qstring.h>
#include <qtypes.h>

#include "../../core/logcat.hpp"
#include "mpris.hpp"

namespace qs::win32::media {

namespace {
QS_LOGGING_CATEGORY(logMedia, "quickshell.win32.media", QtWarningMsg);

using Manager = winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager;
using Session = winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSession;
using PlaybackStatus =
    winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionPlaybackStatus;
using RepeatMode = winrt::Windows::Media::MediaPlaybackAutoRepeatMode;

// Artwork larger than this is not worth carrying around.
constexpr quint64 MAX_ART_BYTES = 8 * 1024 * 1024;

QString toQString(const winrt::hstring& string) {
	return QString::fromWCharArray(string.c_str(), static_cast<qsizetype>(string.size()));
}

qreal toSeconds(winrt::Windows::Foundation::TimeSpan span) {
	return std::chrono::duration<qreal>(span).count();
}

struct SessionEntry {
	Session session {nullptr};
	Session::MediaPropertiesChanged_revoker mediaRevoker;
	Session::PlaybackInfoChanged_revoker playbackRevoker;
	Session::TimelinePropertiesChanged_revoker timelineRevoker;
	// Artwork is only read again when the track it belongs to changes.
	QString artKey;
	QByteArray art;
};

} // namespace

struct MediaWorker::Private {
	Manager manager {nullptr};
	Manager::SessionsChanged_revoker sessionsRevoker;
	std::map<QString, SessionEntry> sessions;
	std::atomic<bool> refreshPending = false;
	std::atomic<bool> sessionListStale = true;
};

MediaWorker::MediaWorker(): d(new Private()) {}
MediaWorker::~MediaWorker() { delete this->d; }

void MediaWorker::requestRefresh() {
	if (this->d->refreshPending.exchange(true)) return;
	QMetaObject::invokeMethod(this, &MediaWorker::refresh, Qt::QueuedConnection);
}

void MediaWorker::start() {
	try {
		winrt::init_apartment(winrt::apartment_type::multi_threaded);
		this->d->manager = Manager::RequestAsync().get();

		// Event handlers run on thread pool threads and only ask for a refresh.
		this->d->sessionsRevoker = this->d->manager.SessionsChanged(
		    winrt::auto_revoke,
		    [this](const auto& /*unused*/, const auto& /*unused*/) {
			    this->d->sessionListStale = true;
			    this->requestRefresh();
		    }
		);
	} catch (const winrt::hresult_error& error) {
		qCWarning(logMedia) << "Could not access the system media sessions:"
		                    << toQString(error.message());
		return;
	}

	this->refresh();
}

void MediaWorker::refresh() {
	this->d->refreshPending = false;
	if (!this->d->manager) return;

	if (this->d->sessionListStale.exchange(false)) {
		std::map<QString, SessionEntry> sessions;

		try {
			for (const auto& session: this->d->manager.GetSessions()) {
				auto id = toQString(session.SourceAppUserModelId());
				if (id.isEmpty() || sessions.contains(id)) continue;

				auto& entry = sessions[id];
				entry.session = session;

				// Keeps the artwork if the application simply reopened its session.
				if (auto old = this->d->sessions.find(id); old != this->d->sessions.end()) {
					entry.artKey = old->second.artKey;
					entry.art = old->second.art;
				}

				auto changed = [this](const auto& /*unused*/, const auto& /*unused*/) {
					this->requestRefresh();
				};

				entry.mediaRevoker = session.MediaPropertiesChanged(winrt::auto_revoke, changed);
				entry.playbackRevoker = session.PlaybackInfoChanged(winrt::auto_revoke, changed);
				entry.timelineRevoker = session.TimelinePropertiesChanged(winrt::auto_revoke, changed);
			}
		} catch (const winrt::hresult_error& error) {
			qCWarning(logMedia) << "Could not list media sessions:" << toQString(error.message());
		}

		this->d->sessions = std::move(sessions);
	}

	qCDebug(logMedia) << "Reading" << this->d->sessions.size() << "media session(s)";

	QList<MediaSnapshot> snapshots;

	for (auto& [id, entry]: this->d->sessions) {
		MediaSnapshot snapshot;
		snapshot.id = id;
		snapshot.sampledAt = QDateTime::currentMSecsSinceEpoch();

		// A session can go away between being listed and being read.
		try {
			auto info = entry.session.GetPlaybackInfo();

			switch (info.PlaybackStatus()) {
			case PlaybackStatus::Playing: snapshot.playbackState = MprisPlaybackState::Playing; break;
			case PlaybackStatus::Paused: snapshot.playbackState = MprisPlaybackState::Paused; break;
			default: snapshot.playbackState = MprisPlaybackState::Stopped; break;
			}

			auto controls = info.Controls();
			snapshot.canPlay = controls.IsPlayEnabled();
			snapshot.canPause = controls.IsPauseEnabled();
			snapshot.canToggle = controls.IsPlayPauseToggleEnabled();
			snapshot.canNext = controls.IsNextEnabled();
			snapshot.canPrevious = controls.IsPreviousEnabled();
			snapshot.canSeek = controls.IsPlaybackPositionEnabled();
			snapshot.canStop = controls.IsStopEnabled();
			snapshot.shuffleSupported = controls.IsShuffleEnabled();
			snapshot.loopSupported = controls.IsRepeatEnabled();

			if (auto shuffle = info.IsShuffleActive()) snapshot.shuffle = shuffle.Value();
			if (auto rate = info.PlaybackRate()) snapshot.rate = rate.Value();

			if (auto repeat = info.AutoRepeatMode()) {
				switch (repeat.Value()) {
				case RepeatMode::Track: snapshot.loopState = MprisLoopState::Track; break;
				case RepeatMode::List: snapshot.loopState = MprisLoopState::Playlist; break;
				default: snapshot.loopState = MprisLoopState::None; break;
				}
			}

			auto timeline = entry.session.GetTimelineProperties();
			auto start = toSeconds(timeline.StartTime());
			snapshot.length = std::max(toSeconds(timeline.EndTime()) - start, 0.0);
			snapshot.position = std::max(toSeconds(timeline.Position()) - start, 0.0);

			// The position was true at LastUpdatedTime, which may be a while ago.
			auto updated = timeline.LastUpdatedTime();
			if (updated.time_since_epoch().count() > 0) {
				auto age = winrt::clock::now() - updated;
				auto ageMs = std::chrono::duration_cast<std::chrono::milliseconds>(age).count();
				if (ageMs >= 0) snapshot.sampledAt -= ageMs;
			}

			auto properties = entry.session.TryGetMediaPropertiesAsync().get();
			snapshot.title = toQString(properties.Title());
			snapshot.artist = toQString(properties.Artist());
			snapshot.album = toQString(properties.AlbumTitle());

			auto artKey = snapshot.title % u'\n' % snapshot.artist % u'\n' % snapshot.album;
			if (artKey != entry.artKey) {
				entry.artKey = artKey;
				entry.art.clear();

				if (auto thumbnail = properties.Thumbnail()) {
					auto stream = thumbnail.OpenReadAsync().get();
					auto size = stream.Size();

					if (size > 0 && size <= MAX_ART_BYTES) {
						winrt::Windows::Storage::Streams::Buffer buffer(static_cast<uint32_t>(size));
						auto read = stream
						                .ReadAsync(
						                    buffer,
						                    static_cast<uint32_t>(size),
						                    winrt::Windows::Storage::Streams::InputStreamOptions::None
						                )
						                .get();

						entry.art = QByteArray(
						    reinterpret_cast<const char*>(read.data()), // NOLINT
						    static_cast<qsizetype>(read.Length())
						);
					}
				}
			}
		} catch (const winrt::hresult_error& error) {
			qCDebug(logMedia) << "Could not read media session" << id << toQString(error.message());
		}

		snapshot.art = entry.art;
		snapshot.artKey = entry.art.isEmpty() ? QString() : entry.artKey;
		snapshots.append(snapshot);
	}

	emit this->sessionsChanged(snapshots);
}

void MediaWorker::command(const QString& id, MediaCommand command, qreal argument) {
	auto entry = this->d->sessions.find(id);
	if (entry == this->d->sessions.end()) return;

	auto& session = entry->second.session;

	// The requests complete on their own. The session reports the outcome
	// through its change events.
	try {
		switch (command) {
		case MediaCommand::Play: session.TryPlayAsync(); break;
		case MediaCommand::Pause: session.TryPauseAsync(); break;
		case MediaCommand::Stop: session.TryStopAsync(); break;
		case MediaCommand::Toggle: session.TryTogglePlayPauseAsync(); break;
		case MediaCommand::Next: session.TrySkipNextAsync(); break;
		case MediaCommand::Previous: session.TrySkipPreviousAsync(); break;
		case MediaCommand::SetPosition: {
			// In 100 nanosecond units from the start of the timeline.
			auto start = session.GetTimelineProperties().StartTime().count();
			session.TryChangePlaybackPositionAsync(start + static_cast<int64_t>(argument * 10000000.0));
			break;
		}
		case MediaCommand::SetShuffle: session.TryChangeShuffleActiveAsync(argument > 0.5); break;
		case MediaCommand::SetLoop: {
			auto mode = RepeatMode::None;
			if (static_cast<int>(argument) == MprisLoopState::Track) mode = RepeatMode::Track;
			else if (static_cast<int>(argument) == MprisLoopState::Playlist) mode = RepeatMode::List;
			session.TryChangeAutoRepeatModeAsync(mode);
			break;
		}
		}
	} catch (const winrt::hresult_error& error) {
		qCWarning(logMedia) << "Media command failed for" << id << toQString(error.message());
	}
}

} // namespace qs::win32::media
