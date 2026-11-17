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

// Quickshell.Services.Notifications for Windows.
//
// On Linux a NotificationServer is the notification daemon: applications send
// their notifications to it. Windows has no such role. Applications post
// toasts to the system, which shows them itself, and a program can only
// listen in, once the user has allowed it under Settings > Privacy & security
// > Notifications.
//
// So this mirrors the system's toasts: each new one is handed to the
// configuration as a Notification, and dismissing it removes it from the
// notification centre. The system still shows its own banner unless Do Not
// Disturb is on. A toast's buttons cannot be triggered from outside the
// system, so notifications have no actions.

namespace qs::win32::notifications {

///! The urgency level of a Notification.
namespace NotificationUrgency { // NOLINT
Q_NAMESPACE;
QML_ELEMENT;

enum Enum : quint8 {
	Low = 0,
	Normal = 1,
	Critical = 2,
};
Q_ENUM_NS(Enum);

} // namespace NotificationUrgency

///! The reason a Notification was closed.
namespace NotificationCloseReason { // NOLINT
Q_NAMESPACE;
QML_ELEMENT;

enum Enum : quint8 {
	/// The notification timed out.
	Expired = 1,
	/// The notification was dismissed from the configuration.
	Dismissed = 2,
	/// The notification went away on the system's side.
	CloseRequested = 3,
};
Q_ENUM_NS(Enum);

} // namespace NotificationCloseReason

// One toast as the system reports it. Plain data, so it can cross threads.
struct ToastSnapshot {
	quint32 id = 0;
	QString appName;
	QString appId;
	QString title;
	QString body;
	// Encoded image of the application's logo, or empty.
	QByteArray logo;
};

// Lives on the worker thread and owns everything from the Windows Runtime.
class NotificationWorker: public QObject {
	Q_OBJECT;

public:
	explicit NotificationWorker();
	~NotificationWorker() override;
	Q_DISABLE_COPY_MOVE(NotificationWorker);

public slots:
	void start();
	void poll();
	void remove(quint32 id);

signals:
	void added(const qs::win32::notifications::ToastSnapshot& toast);
	void removed(quint32 id);
	// Whether the system lets this program read notifications.
	void accessChanged(bool allowed);

private:
	struct Private;
	Private* d;
};

class Notification;

class NotificationImageHandle: public QsIndexedImageHandle {
public:
	explicit NotificationImageHandle(Notification* notification)
	    : QsIndexedImageHandle(QQmlImageProviderBase::Image)
	    , notification(notification) {}

	QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;

private:
	Notification* notification;
};

///! A notification mirrored from the system.
class Notification: public QObject {
	Q_OBJECT;
	// clang-format off
	Q_PROPERTY(quint32 id READ id CONSTANT);
	/// Set to true from the server's notification signal to keep the
	/// notification. One that is not tracked is discarded straight away.
	Q_PROPERTY(bool tracked READ isTracked WRITE setTracked NOTIFY trackedChanged);
	Q_PROPERTY(bool lastGeneration READ no CONSTANT);
	/// The system does not say how long a toast is shown, so this is 0.
	Q_PROPERTY(qreal expireTimeout READ expireTimeout NOTIFY expireTimeoutChanged);
	Q_PROPERTY(QString appName READ appName NOTIFY appNameChanged);
	/// Always empty: there are no icon names on Windows. The application's
	/// logo is available as @@image.
	Q_PROPERTY(QString appIcon READ appIcon NOTIFY appIconChanged);
	Q_PROPERTY(QString summary READ summary NOTIFY summaryChanged);
	Q_PROPERTY(QString body READ body NOTIFY bodyChanged);
	/// Image source for the application's logo, or an empty string.
	Q_PROPERTY(QString image READ image NOTIFY imageChanged);
	/// The application's id as the system reports it.
	Q_PROPERTY(QString desktopEntry READ desktopEntry NOTIFY desktopEntryChanged);
	Q_PROPERTY(qs::win32::notifications::NotificationUrgency::Enum urgency READ urgency NOTIFY urgencyChanged);
	/// Always empty: a toast's buttons cannot be triggered from outside the system.
	Q_PROPERTY(QList<QObject*> actions READ actions NOTIFY actionsChanged);
	Q_PROPERTY(bool hasActionIcons READ no NOTIFY hasActionIconsChanged);
	Q_PROPERTY(bool resident READ no NOTIFY residentChanged);
	Q_PROPERTY(bool transient READ no NOTIFY transientChanged);
	Q_PROPERTY(bool hasInlineReply READ no NOTIFY hasInlineReplyChanged);
	Q_PROPERTY(QString inlineReplyPlaceholder READ empty NOTIFY inlineReplyPlaceholderChanged);
	Q_PROPERTY(QVariantMap hints READ hints NOTIFY hintsChanged);
	// clang-format on
	QML_ELEMENT;
	QML_UNCREATABLE("Notifications must be acquired from a NotificationServer");

public:
	explicit Notification(const ToastSnapshot& toast, QObject* parent);

	/// Stops tracking the notification. It stays in the system's notification centre.
	Q_INVOKABLE void expire();
	/// Stops tracking the notification and removes it from the system's notification centre.
	Q_INVOKABLE void dismiss();
	/// Not available. Does nothing.
	Q_INVOKABLE void sendInlineReply(const QString& replyText);

	[[nodiscard]] quint32 id() const { return this->toast.id; }
	[[nodiscard]] bool isTracked() const { return this->mTracked; }
	void setTracked(bool tracked);
	[[nodiscard]] qreal expireTimeout() const { return 0; }
	[[nodiscard]] QString appName() const { return this->toast.appName; }
	[[nodiscard]] QString appIcon() const { return QString(); }
	[[nodiscard]] QString summary() const { return this->toast.title; }
	[[nodiscard]] QString body() const { return this->toast.body; }
	[[nodiscard]] QString image() const;
	[[nodiscard]] QString desktopEntry() const { return this->toast.appId; }
	[[nodiscard]] NotificationUrgency::Enum urgency() const { return NotificationUrgency::Normal; }
	[[nodiscard]] QList<QObject*> actions() const { return {}; }
	[[nodiscard]] QVariantMap hints() const { return {}; }
	[[nodiscard]] bool no() const { return false; }
	[[nodiscard]] QString empty() const { return QString(); }

	// Ends the notification: emits closed, and stops tracking it.
	void close(NotificationCloseReason::Enum reason);

signals:
	/// Sent when the notification has been closed, however that came about.
	void closed(qs::win32::notifications::NotificationCloseReason::Enum reason);
	void trackedChanged();
	// The properties below never change after a notification arrives. The
	// signals exist so handlers written for the Linux server can connect.
	void expireTimeoutChanged();
	void appNameChanged();
	void appIconChanged();
	void summaryChanged();
	void bodyChanged();
	void imageChanged();
	void desktopEntryChanged();
	void urgencyChanged();
	void actionsChanged();
	void hasActionIconsChanged();
	void residentChanged();
	void transientChanged();
	void hasInlineReplyChanged();
	void inlineReplyPlaceholderChanged();
	void hintsChanged();

private:
	ToastSnapshot toast;
	bool mTracked = false;
	bool isClosed = false;
	QImage logo;
	NotificationImageHandle imageHandle {this};

	friend class NotificationImageHandle;
};

class NotificationServer;

class NotificationHost: public QObject {
	Q_OBJECT;

public:
	static NotificationHost* instance();

	[[nodiscard]] ObjectModel<Notification>* tracked() { return &this->mTracked; }
	[[nodiscard]] bool hasAccess() const { return this->mAccess; }

	void addServer(NotificationServer* server);
	void removeServer(NotificationServer* server);

	// Called by a notification as its tracked state changes.
	void track(Notification* notification);
	void untrack(Notification* notification);
	void removeFromSystem(quint32 id);

signals:
	void accessChanged();

private slots:
	void onAdded(const qs::win32::notifications::ToastSnapshot& toast);
	void onRemoved(quint32 id);
	void onAccessChanged(bool allowed);

private:
	explicit NotificationHost();

	QThread thread;
	NotificationWorker* worker = nullptr;
	QList<NotificationServer*> servers;
	ObjectModel<Notification> mTracked {this};
	bool mAccess = false;
};

///! Notification server
/// Receives the system's toast notifications. See the notes on what a
/// Windows notification can and cannot do in @@Notification.
class NotificationServer: public QObject {
	Q_OBJECT;
	// clang-format off
	/// Whether the system lets this program read notifications. If false,
	/// allow it under Settings > Privacy & security > Notifications.
	Q_PROPERTY(bool hasAccess READ hasAccess NOTIFY hasAccessChanged);
	QSDOC_TYPE_OVERRIDE(ObjectModel<qs::win32::notifications::Notification>*);
	Q_PROPERTY(UntypedObjectModel* trackedNotifications READ trackedNotifications CONSTANT);
	// The capability switches below are accepted so configurations written for
	// the Linux server load. They have no effect.
	Q_PROPERTY(bool keepOnReload MEMBER mKeepOnReload NOTIFY capabilitiesChanged);
	Q_PROPERTY(bool persistenceSupported MEMBER mFlag1 NOTIFY capabilitiesChanged);
	Q_PROPERTY(bool bodySupported MEMBER mFlag2 NOTIFY capabilitiesChanged);
	Q_PROPERTY(bool bodyMarkupSupported MEMBER mFlag3 NOTIFY capabilitiesChanged);
	Q_PROPERTY(bool bodyHyperlinksSupported MEMBER mFlag4 NOTIFY capabilitiesChanged);
	Q_PROPERTY(bool bodyImagesSupported MEMBER mFlag5 NOTIFY capabilitiesChanged);
	Q_PROPERTY(bool actionsSupported MEMBER mFlag6 NOTIFY capabilitiesChanged);
	Q_PROPERTY(bool actionIconsSupported MEMBER mFlag7 NOTIFY capabilitiesChanged);
	Q_PROPERTY(bool imageSupported MEMBER mFlag8 NOTIFY capabilitiesChanged);
	Q_PROPERTY(bool inlineReplySupported MEMBER mFlag9 NOTIFY capabilitiesChanged);
	Q_PROPERTY(QVector<QString> extraHints MEMBER mExtraHints NOTIFY capabilitiesChanged);
	// clang-format on
	QML_ELEMENT;

public:
	explicit NotificationServer(QObject* parent = nullptr);
	~NotificationServer() override;
	Q_DISABLE_COPY_MOVE(NotificationServer);

	[[nodiscard]] bool hasAccess() const { return NotificationHost::instance()->hasAccess(); } // NOLINT
	[[nodiscard]] ObjectModel<Notification>* trackedNotifications() const { // NOLINT
		return NotificationHost::instance()->tracked();
	}

signals:
	/// Sent when a notification arrives. Set its tracked property to true to
	/// keep it, or it is discarded.
	void notification(qs::win32::notifications::Notification* notification);
	void hasAccessChanged();
	void capabilitiesChanged();

private:
	bool mKeepOnReload = true;
	bool mFlag1 = false;
	bool mFlag2 = true;
	bool mFlag3 = false;
	bool mFlag4 = false;
	bool mFlag5 = false;
	bool mFlag6 = false;
	bool mFlag7 = false;
	bool mFlag8 = false;
	bool mFlag9 = false;
	QVector<QString> mExtraHints;
};

} // namespace qs::win32::notifications

Q_DECLARE_METATYPE(qs::win32::notifications::ToastSnapshot);
