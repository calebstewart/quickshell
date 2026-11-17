#pragma once

#include <qimage.h>
#include <qlist.h>
#include <qobject.h>
#include <qqmlintegration.h>
#include <qquickimageprovider.h>
#include <qstring.h>
#include <qtimer.h>
#include <qtmetamacros.h>
#include <qtypes.h>
#include <windows.h>

#include "../../core/doc.hpp"
#include "../../core/imageprovider.hpp"
#include "../../core/model.hpp"

// Quickshell.Services.SystemTray for Windows.
//
// Applications publish notification area icons with Shell_NotifyIcon, which
// sends a WM_COPYDATA message to the first window of class "Shell_TrayWnd".
// TrayHost creates such a window above the taskbar's, records what passes
// through, and forwards every message to the window that was there before,
// so the taskbar's own notification area keeps working.

namespace qs::win32::tray {

///! Statuses a SystemTrayItem can have.
namespace Status { // NOLINT
Q_NAMESPACE;
QML_ELEMENT;

enum Enum : quint8 {
	Passive = 0,
	Active = 1,
	NeedsAttention = 2,
};
Q_ENUM_NS(Enum);

} // namespace Status

///! Categories a SystemTrayItem can have.
namespace Category { // NOLINT
Q_NAMESPACE;
QML_ELEMENT;

enum Enum : quint8 {
	Hardware = 0,
	SystemServices = 1,
	ApplicationStatus = 2,
	Communications = 3,
};
Q_ENUM_NS(Enum);

} // namespace Category

class TrayItem;

class TrayImageHandle: public QsIndexedImageHandle {
public:
	explicit TrayImageHandle(TrayItem* item)
	    : QsIndexedImageHandle(QQmlImageProviderBase::Image)
	    , item(item) {}

	QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;

private:
	TrayItem* item;
};

///! An icon in the notification area.
/// Windows applications draw their own menus, so an item never has a menu
/// object. @@secondaryActivate() sends the right click that makes the
/// application show its menu at the cursor.
class TrayItem: public QObject {
	Q_OBJECT;
	// clang-format off
	/// The name of the program that owns the icon, without its extension.
	Q_PROPERTY(QString id READ id CONSTANT);
	/// The icon's tooltip text.
	Q_PROPERTY(QString title READ title NOTIFY titleChanged);
	Q_PROPERTY(qs::win32::tray::Status::Enum status READ status CONSTANT);
	Q_PROPERTY(qs::win32::tray::Category::Enum category READ category CONSTANT);
	/// Image source for the icon. Changes whenever the application updates it.
	Q_PROPERTY(QString icon READ icon NOTIFY iconChanged);
	Q_PROPERTY(QString tooltipTitle READ title NOTIFY titleChanged);
	Q_PROPERTY(QString tooltipDescription READ tooltipDescription CONSTANT);
	Q_PROPERTY(bool hasMenu READ hasMenu CONSTANT);
	Q_PROPERTY(QObject* menu READ menu CONSTANT);
	Q_PROPERTY(bool onlyMenu READ onlyMenu CONSTANT);
	// clang-format on
	QML_NAMED_ELEMENT(SystemTrayItem);
	QML_UNCREATABLE("SystemTrayItems can only be acquired from SystemTray");

public:
	explicit TrayItem(QObject* parent = nullptr);

	/// Left click.
	Q_INVOKABLE void activate();
	/// Right click, which normally opens the application's own menu.
	Q_INVOKABLE void secondaryActivate();
	/// Not supported by notification area icons.
	Q_INVOKABLE void scroll(qint32 delta, bool horizontal) const;
	/// Same as @@secondaryActivate(), as the application positions its own menu.
	Q_INVOKABLE void display(QObject* parentWindow, qint32 relativeX, qint32 relativeY);

	[[nodiscard]] QString id() const { return this->mId; }
	[[nodiscard]] QString title() const { return this->mTitle; }
	[[nodiscard]] Status::Enum status() const { return Status::Active; }
	[[nodiscard]] Category::Enum category() const { return Category::ApplicationStatus; }
	[[nodiscard]] QString icon() const { return this->imageHandle.url(); }
	[[nodiscard]] QString tooltipDescription() const { return QString(); }
	[[nodiscard]] bool hasMenu() const { return false; }
	[[nodiscard]] QObject* menu() const { return nullptr; }
	[[nodiscard]] bool onlyMenu() const { return false; }

signals:
	void titleChanged();
	void iconChanged();

private:
	void sendMouseMessage(UINT message) const;

	// What identifies the icon: the owning window and an id, or a GUID.
	HWND window = nullptr;
	UINT iconId = 0;
	GUID guid = {};
	bool hasGuid = false;

	UINT callbackMessage = 0;
	UINT version = 0;
	bool hidden = false;
	bool listed = false;

	QString mId;
	QString mTitle;
	QImage image;
	TrayImageHandle imageHandle {this};

	friend class TrayHost;
	friend class TrayImageHandle;
};

// An AppBar request as SHAppBarMessage sends it: a 40 byte APPBARDATA with 32
// bit handles, then the request and where to find its shared memory.
struct AppBarMessageData {
	BYTE appBarData[40]; // NOLINT
	DWORD message;
	DWORD padding1;
	ULONG64 sharedMemory;
	DWORD sourceProcessId;
	DWORD padding2;
};

class TrayHost: public QObject {
	Q_OBJECT;

public:
	static TrayHost* instance();

	[[nodiscard]] ObjectModel<TrayItem>* items() { return &this->mItems; }

private:
	explicit TrayHost();
	~TrayHost() override;

	static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
	LRESULT onCopyData(WPARAM wParam, LPARAM lParam);
	bool onNotifyIcon(const void* data, DWORD size);
	LRESULT forward(WPARAM wParam, LPARAM lParam, bool* forwarded) const;
	LRESULT forwardAppBar(WPARAM wParam, const AppBarMessageData& request, bool* forwarded) const;

	void maintain();
	void updateListing(TrayItem* item);
	[[nodiscard]] TrayItem* find(HWND window, UINT iconId, const GUID& guid, bool hasGuid) const;
	void remove(TrayItem* item);

	HWND hwnd = nullptr;
	// The notification area window that was first before this one was created.
	HWND previous = nullptr;
	QList<TrayItem*> all;
	ObjectModel<TrayItem> mItems {this};
	QTimer maintainTimer;
};

///! System tray
/// The Windows notification area. See @@SystemTrayItem for how its items differ
/// from StatusNotifierItems.
class SystemTray: public QObject {
	Q_OBJECT;
	/// List of all notification area icons that are not hidden by their application.
	QSDOC_TYPE_OVERRIDE(ObjectModel<qs::win32::tray::TrayItem>*);
	Q_PROPERTY(UntypedObjectModel* items READ items CONSTANT);
	QML_ELEMENT;
	QML_SINGLETON;

public:
	explicit SystemTray(QObject* parent = nullptr): QObject(parent) {}

	[[nodiscard]] ObjectModel<TrayItem>* items() { return TrayHost::instance()->items(); } // NOLINT
};

} // namespace qs::win32::tray
