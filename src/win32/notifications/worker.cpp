// clang-format off
// The Windows Runtime headers come first, before Qt defines its keyword macros.
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.UI.Notifications.Management.h>
#include <winrt/Windows.UI.Notifications.h>
// clang-format on

#include <set>

#include <qbytearray.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qobject.h>
#include <qstring.h>
#include <qstringlist.h>
#include <qtimer.h>
#include <qtypes.h>

#include "../../core/logcat.hpp"
#include "notifications.hpp"

namespace qs::win32::notifications {

namespace {
QS_LOGGING_CATEGORY(logNotificationWorker, "quickshell.win32.notifications", QtWarningMsg);

using winrt::Windows::UI::Notifications::KnownNotificationBindings;
using winrt::Windows::UI::Notifications::NotificationKinds;
using winrt::Windows::UI::Notifications::UserNotification;
using winrt::Windows::UI::Notifications::Management::UserNotificationListener;
using winrt::Windows::UI::Notifications::Management::UserNotificationListenerAccessStatus;

// A logo larger than this is not worth carrying around.
constexpr quint64 MAX_LOGO_BYTES = 2 * 1024 * 1024;

// How many polls pass between checks of whether access has been granted.
constexpr int ACCESS_CHECK_INTERVAL = 5;

QString toQString(const winrt::hstring& string) {
	return QString::fromWCharArray(string.c_str(), static_cast<qsizetype>(string.size()));
}

ToastSnapshot snapshotOf(const UserNotification& notification) {
	ToastSnapshot toast;
	toast.id = notification.Id();

	// Each part can be missing or fail on its own, for instance for a toast
	// from a program that has since been uninstalled.
	try {
		if (auto app = notification.AppInfo()) {
			toast.appId = toQString(app.AppUserModelId());

			auto display = app.DisplayInfo();
			toast.appName = toQString(display.DisplayName());

			if (auto logo = display.GetLogo({64, 64})) {
				auto stream = logo.OpenReadAsync().get();
				auto size = stream.Size();

				if (size > 0 && size <= MAX_LOGO_BYTES) {
					winrt::Windows::Storage::Streams::Buffer buffer(static_cast<uint32_t>(size));
					auto read = stream
					                .ReadAsync(
					                    buffer,
					                    static_cast<uint32_t>(size),
					                    winrt::Windows::Storage::Streams::InputStreamOptions::None
					                )
					                .get();

					toast.logo = QByteArray(
					    reinterpret_cast<const char*>(read.data()), // NOLINT
					    static_cast<qsizetype>(read.Length())
					);
				}
			}
		}
	} catch (const winrt::hresult_error& error) {
		qCDebug(logNotificationWorker)
		    << "Could not read a notification's application:" << toQString(error.message());
	}

	try {
		auto binding =
		    notification.Notification().Visual().GetBinding(KnownNotificationBindings::ToastGeneric());

		if (binding) {
			// The first line of text is the title and the rest is the body.
			QStringList body;
			auto first = true;

			for (const auto& element: binding.GetTextElements()) {
				auto text = toQString(element.Text());

				if (first) {
					toast.title = text;
					first = false;
				} else if (!text.isEmpty()) {
					body.append(text);
				}
			}

			toast.body = body.join('\n');
		}
	} catch (const winrt::hresult_error& error) {
		qCDebug(logNotificationWorker)
		    << "Could not read a notification's text:" << toQString(error.message());
	}

	if (toast.appName.isEmpty()) toast.appName = toast.appId;
	return toast;
}

} // namespace

struct NotificationWorker::Private {
	UserNotificationListener listener {nullptr};
	std::set<quint32> known;
	// Toasts already in the notification centre at startup are not announced.
	bool firstPoll = true;
	bool allowed = false;
	bool accessReported = false;
	int pollsUntilAccessCheck = 0;
};

NotificationWorker::NotificationWorker(): d(new Private()) {}
NotificationWorker::~NotificationWorker() { delete this->d; }

void NotificationWorker::start() {
	try {
		winrt::init_apartment(winrt::apartment_type::multi_threaded);
		this->d->listener = UserNotificationListener::Current();
	} catch (const winrt::hresult_error& error) {
		qCWarning(logNotificationWorker)
		    << "Could not reach the notification listener:" << toQString(error.message());
	}

	// The listener's change event is only delivered to packaged applications,
	// so the list is polled.
	auto* timer = new QTimer(this);
	QObject::connect(timer, &QTimer::timeout, this, &NotificationWorker::poll);
	timer->start(1000);

	this->poll();
}

void NotificationWorker::poll() {
	if (!this->d->listener) {
		if (!this->d->accessReported) {
			this->d->accessReported = true;
			emit this->accessChanged(false);
		}

		return;
	}

	// Access can be granted or withdrawn in Settings while this runs.
	if (this->d->pollsUntilAccessCheck-- <= 0) {
		this->d->pollsUntilAccessCheck = ACCESS_CHECK_INTERVAL;
		auto allowed = false;

		try {
			auto status = this->d->listener.GetAccessStatus();

			// Asks the system for access the first time. For a program that has
			// been refused before, this returns the refusal without asking again.
			if (status == UserNotificationListenerAccessStatus::Unspecified && !this->d->accessReported) {
				status = this->d->listener.RequestAccessAsync().get();
			}

			allowed = status == UserNotificationListenerAccessStatus::Allowed;
			qCDebug(logNotificationWorker) << "Notification access status" << static_cast<int>(status);
		} catch (const winrt::hresult_error& error) {
			qCDebug(logNotificationWorker)
			    << "Could not check notification access:" << toQString(error.message());
		}

		if (allowed != this->d->allowed || !this->d->accessReported) {
			this->d->allowed = allowed;
			this->d->accessReported = true;
			emit this->accessChanged(allowed);
		}
	}

	if (!this->d->allowed) return;

	std::set<quint32> current;

	try {
		for (const auto& notification:
		     this->d->listener.GetNotificationsAsync(NotificationKinds::Toast).get())
		{
			auto id = notification.Id();
			current.insert(id);

			if (!this->d->known.contains(id) && !this->d->firstPoll)
				emit this->added(snapshotOf(notification));
		}
	} catch (const winrt::hresult_error& error) {
		qCDebug(logNotificationWorker) << "Could not list notifications:" << toQString(error.message());
		return;
	}

	for (auto id: this->d->known) {
		if (!current.contains(id)) emit this->removed(id);
	}

	this->d->known = std::move(current);
	this->d->firstPoll = false;
}

void NotificationWorker::remove(quint32 id) {
	if (!this->d->listener) return;

	try {
		this->d->listener.RemoveNotification(id);
	} catch (const winrt::hresult_error& error) {
		qCDebug(logNotificationWorker)
		    << "Could not remove notification" << id << toQString(error.message());
	}

	// Already gone as far as the configuration is concerned.
	this->d->known.erase(id);
}

} // namespace qs::win32::notifications
