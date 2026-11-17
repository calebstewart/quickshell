#include "notifications.hpp"

#include <qimage.h>
#include <qlist.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qmetatype.h>
#include <qobject.h>
#include <qsize.h>
#include <qstring.h>
#include <qthread.h>
#include <qtypes.h>

#include "../../core/logcat.hpp"

namespace qs::win32::notifications {

namespace {
QS_LOGGING_CATEGORY(logNotifications, "quickshell.win32.notifications", QtWarningMsg);
}

QImage NotificationImageHandle::requestImage(
    const QString& /*unused*/,
    QSize* size,
    const QSize& requestedSize
) {
	auto image = this->notification->logo;

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

Notification::Notification(const ToastSnapshot& toast, QObject* parent)
    : QObject(parent)
    , toast(toast) {
	if (!toast.logo.isEmpty()) {
		this->logo = QImage::fromData(toast.logo);
		this->imageHandle.imageChanged();
	}
}

QString Notification::image() const {
	return this->logo.isNull() ? QString() : this->imageHandle.url();
}

void Notification::setTracked(bool tracked) {
	if (tracked == this->mTracked) return;

	if (tracked) {
		if (this->isClosed) return;
		this->mTracked = true;
		NotificationHost::instance()->track(this);
		emit this->trackedChanged();
	} else {
		this->close(NotificationCloseReason::Dismissed);
	}
}

void Notification::close(NotificationCloseReason::Enum reason) {
	if (this->isClosed) return;
	this->isClosed = true;

	emit this->closed(reason);

	if (this->mTracked) {
		this->mTracked = false;
		NotificationHost::instance()->untrack(this);
		emit this->trackedChanged();
	}

	this->deleteLater();
}

void Notification::expire() { this->close(NotificationCloseReason::Expired); }

void Notification::dismiss() {
	if (this->isClosed) return;
	NotificationHost::instance()->removeFromSystem(this->toast.id);
	this->close(NotificationCloseReason::Dismissed);
}

void Notification::sendInlineReply(const QString& /*unused*/) {
	qCWarning(logNotifications) << "Replying to a notification is not possible on Windows.";
}

NotificationHost* NotificationHost::instance() {
	static auto* instance = new NotificationHost(); // NOLINT
	return instance;
}

NotificationHost::NotificationHost() {
	qRegisterMetaType<ToastSnapshot>();

	this->worker = new NotificationWorker();
	this->worker->moveToThread(&this->thread);

	// clang-format off
	QObject::connect(this->worker, &NotificationWorker::added, this, &NotificationHost::onAdded);
	QObject::connect(this->worker, &NotificationWorker::removed, this, &NotificationHost::onRemoved);
	QObject::connect(this->worker, &NotificationWorker::accessChanged, this, &NotificationHost::onAccessChanged);
	QObject::connect(&this->thread, &QThread::started, this->worker, &NotificationWorker::start);
	// clang-format on

	this->thread.setObjectName("notifications");
	this->thread.start();
}

void NotificationHost::addServer(NotificationServer* server) { this->servers.append(server); }
void NotificationHost::removeServer(NotificationServer* server) { this->servers.removeOne(server); }

void NotificationHost::track(Notification* notification) {
	if (!this->mTracked.valueList().contains(notification)) this->mTracked.insertObject(notification);
}

void NotificationHost::untrack(Notification* notification) {
	this->mTracked.removeObject(notification);
}

void NotificationHost::removeFromSystem(quint32 id) {
	QMetaObject::invokeMethod(
	    this->worker,
	    [worker = this->worker, id] { worker->remove(id); },
	    Qt::QueuedConnection
	);
}

void NotificationHost::onAdded(const ToastSnapshot& toast) {
	// Nothing is listening, so there is nobody to show it.
	if (this->servers.isEmpty()) return;

	auto* notification = new Notification(toast, this);

	for (auto* server: QList<NotificationServer*>(this->servers)) {
		emit server->notification(notification);
	}

	// As with the Linux server, a notification nobody chose to track is dropped.
	if (!notification->isTracked()) notification->deleteLater();
}

void NotificationHost::onRemoved(quint32 id) {
	for (auto* notification: QList<Notification*>(this->mTracked.valueList())) {
		if (notification->id() == id) notification->close(NotificationCloseReason::CloseRequested);
	}
}

void NotificationHost::onAccessChanged(bool allowed) {
	if (allowed == this->mAccess) return;
	this->mAccess = allowed;

	if (!allowed) {
		qCWarning(
		    logNotifications
		) << "Windows does not allow this program to read notifications. Allow it under Settings > "
		     "Privacy & security > Notifications.";
	}

	emit this->accessChanged();
}

NotificationServer::NotificationServer(QObject* parent): QObject(parent) {
	auto* host = NotificationHost::instance();
	host->addServer(this);

	QObject::connect(
	    host,
	    &NotificationHost::accessChanged,
	    this,
	    &NotificationServer::hasAccessChanged
	);
}

NotificationServer::~NotificationServer() { NotificationHost::instance()->removeServer(this); }

} // namespace qs::win32::notifications
