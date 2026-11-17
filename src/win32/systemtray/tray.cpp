#include "tray.hpp"
#include <algorithm>
#include <cstddef>
#include <cstring>

#include <qfileinfo.h>
#include <qimage.h>
#include <qlogging.h>
#include <qloggingcategory.h>
#include <qobject.h>
#include <qsize.h>
#include <qstring.h>
#include <qtimer.h>
#include <qtypes.h>
#include <windows.h>
// windows.h must come first
#include <shellapi.h>

#include "../../core/logcat.hpp"

namespace qs::win32::tray {

namespace {
QS_LOGGING_CATEGORY(logTray, "quickshell.win32.tray", QtWarningMsg);

const wchar_t* const TRAY_CLASS = L"Shell_TrayWnd";

// WM_COPYDATA dwData values. Shell_NotifyIcon uses 1 and SHAppBarMessage 0.
// Anything else, such as icon position requests, is only forwarded.
constexpr ULONG_PTR COPYDATA_APPBAR = 0;
constexpr ULONG_PTR COPYDATA_NOTIFYICON = 1;
constexpr DWORD NOTIFYICON_SIGNATURE = 0x34753423;

// Size of the APPBARDATA block SHAppBarMessage places in shared memory.
constexpr DWORD SHARED_APPBAR_SIZE = 40;

// The shell's shared memory helpers. shlwapi.dll exports them by ordinal
// only, so an import by name fails to load.
struct SharedMemoryApi {
	HANDLE(WINAPI* alloc)(const void* data, DWORD size, DWORD processId) = nullptr;
	void*(WINAPI* lock)(HANDLE data, DWORD processId) = nullptr;
	BOOL(WINAPI* unlock)(void* data) = nullptr;
	BOOL(WINAPI* free)(HANDLE data, DWORD processId) = nullptr;

	[[nodiscard]] bool available() const {
		return this->alloc != nullptr && this->lock != nullptr && this->unlock != nullptr
		    && this->free != nullptr;
	}
};

const SharedMemoryApi& sharedMemoryApi() {
	static const SharedMemoryApi api = [] {
		SharedMemoryApi api;
		auto* module = LoadLibraryW(L"shlwapi.dll");
		if (module == nullptr) return api;

		// NOLINTBEGIN
		api.alloc = reinterpret_cast<decltype(api.alloc)>(GetProcAddress(module, MAKEINTRESOURCEA(7)));
		api.lock = reinterpret_cast<decltype(api.lock)>(GetProcAddress(module, MAKEINTRESOURCEA(8)));
		api.unlock =
		    reinterpret_cast<decltype(api.unlock)>(GetProcAddress(module, MAKEINTRESOURCEA(9)));
		api.free = reinterpret_cast<decltype(api.free)>(GetProcAddress(module, MAKEINTRESOURCEA(10)));
		// NOLINTEND
		return api;
	}();

	return api;
}

// NOTIFYICONDATAW as it is sent between processes: handles are always 32 bits
// wide, whatever the bitness of the sender.
struct NotifyIconData32 {
	DWORD cbSize;
	DWORD hWnd;
	UINT uID;
	UINT uFlags;
	UINT uCallbackMessage;
	DWORD hIcon;
	WCHAR szTip[128]; // NOLINT
	DWORD dwState;
	DWORD dwStateMask;
	WCHAR szInfo[256]; // NOLINT
	UINT uVersion;
	WCHAR szInfoTitle[64]; // NOLINT
	DWORD dwInfoFlags;
	GUID guidItem;
	DWORD hBalloonIcon;
};

struct ShellTrayData {
	DWORD signature;
	DWORD message;
	NotifyIconData32 nid;
};

// Handles are sign extended when widened.
template <typename T>
T widenHandle(DWORD value) {
	return reinterpret_cast<T>(static_cast<LONG_PTR>(static_cast<LONG>(value))); // NOLINT
}

QString processName(HWND window) {
	DWORD pid = 0;
	GetWindowThreadProcessId(window, &pid);
	if (pid == 0) return QString();

	auto* process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
	if (process == nullptr) return QString();

	WCHAR path[MAX_PATH] = {}; // NOLINT
	DWORD length = MAX_PATH;
	auto ok = QueryFullProcessImageNameW(process, 0, path, &length);
	CloseHandle(process);

	if (!ok) return QString();
	return QFileInfo(QString::fromWCharArray(path, static_cast<qsizetype>(length)))
	    .completeBaseName();
}

} // namespace

QImage
TrayImageHandle::requestImage(const QString& /*unused*/, QSize* size, const QSize& requestedSize) {
	auto image = this->item->image;

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

TrayItem::TrayItem(QObject* parent): QObject(parent) {}

void TrayItem::sendMouseMessage(UINT message) const {
	if (this->callbackMessage == 0 || !IsWindow(this->window)) return;

	POINT cursor = {};
	GetCursorPos(&cursor);

	// Version 4 icons get the cursor position and have the icon id moved into lParam.
	if (this->version >= NOTIFYICON_VERSION_4) {
		PostMessageW(
		    this->window,
		    this->callbackMessage,
		    MAKEWPARAM(cursor.x, cursor.y),
		    MAKELPARAM(message, this->iconId)
		);
	} else {
		PostMessageW(this->window, this->callbackMessage, this->iconId, message);
	}
}

void TrayItem::activate() {
	if (!IsWindow(this->window)) return;

	// The application usually wants to bring a window forward in response.
	DWORD pid = 0;
	GetWindowThreadProcessId(this->window, &pid);
	if (pid != 0) AllowSetForegroundWindow(pid);

	this->sendMouseMessage(WM_LBUTTONDOWN);
	this->sendMouseMessage(WM_LBUTTONUP);
	if (this->version >= NOTIFYICON_VERSION_4) this->sendMouseMessage(NIN_SELECT);
}

void TrayItem::secondaryActivate() {
	if (!IsWindow(this->window)) return;

	// A popup menu only dismisses properly if its owner is the foreground window.
	DWORD pid = 0;
	GetWindowThreadProcessId(this->window, &pid);
	if (pid != 0) AllowSetForegroundWindow(pid);

	this->sendMouseMessage(WM_RBUTTONDOWN);
	this->sendMouseMessage(WM_RBUTTONUP);
	if (this->version >= NOTIFYICON_VERSION_4) this->sendMouseMessage(WM_CONTEXTMENU);
}

void TrayItem::scroll(qint32 /*unused*/, bool /*unused*/) const {}

void TrayItem::display(QObject* /*unused*/, qint32 /*unused*/, qint32 /*unused*/) {
	this->secondaryActivate();
}

TrayHost* TrayHost::instance() {
	static auto* instance = new TrayHost(); // NOLINT
	return instance;
}

TrayHost::TrayHost() {
	this->previous = FindWindowW(TRAY_CLASS, nullptr);

	WNDCLASSW windowClass = {};
	windowClass.lpfnWndProc = &TrayHost::windowProc;
	windowClass.hInstance = GetModuleHandleW(nullptr);
	windowClass.lpszClassName = TRAY_CLASS;
	RegisterClassW(&windowClass);

	this->hwnd = CreateWindowExW(
	    WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
	    TRAY_CLASS,
	    L"",
	    WS_POPUP,
	    0,
	    0,
	    0,
	    0,
	    nullptr,
	    nullptr,
	    windowClass.hInstance,
	    nullptr
	);

	if (this->hwnd == nullptr) {
		qCWarning(logTray) << "Could not create the notification area window, error" << GetLastError();
		return;
	}

	SetWindowLongPtrW(this->hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this)); // NOLINT
	this->maintain();

	QObject::connect(&this->maintainTimer, &QTimer::timeout, this, &TrayHost::maintain);
	this->maintainTimer.start(1000);

	// Asks every application to publish its icons again, as it would after the
	// taskbar restarts. This is the only way to learn about existing icons.
	auto taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
	SendNotifyMessageW(HWND_BROADCAST, taskbarCreated, 0, 0);

	qCInfo(logTray) << "Notification area window created, forwarding to" << this->previous;
}

TrayHost::~TrayHost() {
	if (this->hwnd != nullptr) DestroyWindow(this->hwnd);
}

LRESULT CALLBACK TrayHost::windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
	auto* self = reinterpret_cast<TrayHost*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA)); // NOLINT

	if (self != nullptr && message == WM_COPYDATA) return self->onCopyData(wParam, lParam);

	// The shell and other programs ask the taskbar things through private
	// messages, such as whether notifications should be shown right now.
	// Those are passed on to the real one and its answer returned. Registered
	// messages are left alone: they are broadcast, so it receives its own copy.
	auto forwardable = (message >= WM_USER && message < 0xc000) || message == WM_COMMAND;

	if (self != nullptr && forwardable && self->previous != nullptr && IsWindow(self->previous)) {
		DWORD_PTR result = 0;
		auto sent = SendMessageTimeoutW(
		    self->previous,
		    message,
		    wParam,
		    lParam,
		    SMTO_ABORTIFHUNG,
		    3000,
		    &result
		);

		if (sent != 0) return static_cast<LRESULT>(result);
	}

	return DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT TrayHost::forward(WPARAM wParam, LPARAM lParam, bool* forwarded) const {
	*forwarded = false;
	if (this->previous == nullptr || !IsWindow(this->previous)) return 0;

	DWORD_PTR result = 0;
	auto sent = SendMessageTimeoutW(
	    this->previous,
	    WM_COPYDATA,
	    wParam,
	    lParam,
	    SMTO_ABORTIFHUNG,
	    3000,
	    &result
	);

	if (sent == 0) return 0;
	*forwarded = true;
	return static_cast<LRESULT>(result);
}

LRESULT
TrayHost::forwardAppBar(WPARAM wParam, const AppBarMessageData& request, bool* forwarded) const {
	*forwarded = false;
	if (this->previous == nullptr || !IsWindow(this->previous)) return 0;

	auto message = request;
	auto send = [&]() -> LRESULT {
		COPYDATASTRUCT copyData = {};
		copyData.dwData = COPYDATA_APPBAR;
		copyData.cbData = sizeof(AppBarMessageData);
		copyData.lpData = &message;

		DWORD_PTR result = 0;
		auto sent = SendMessageTimeoutW(
		    this->previous,
		    WM_COPYDATA,
		    wParam,
		    reinterpret_cast<LPARAM>(&copyData), // NOLINT
		    SMTO_ABORTIFHUNG,
		    3000,
		    &result
		);

		*forwarded = sent != 0;
		return static_cast<LRESULT>(result);
	};

	// Requests that exchange a rectangle refer to a block of shared memory by a
	// token that only works in the process it was shared with. The sender
	// shares it with the owner of the first Shell_TrayWnd, which is this
	// process, so the real notification area cannot open it and skips the
	// request while still reporting success. The block is copied into one
	// shared with that process, and its answer is copied back afterwards.
	const auto& shared = sharedMemoryApi();
	auto* senderHandle =
	    reinterpret_cast<HANDLE>(static_cast<ULONG_PTR>(request.sharedMemory)); // NOLINT
	if (senderHandle == nullptr || !shared.available()) return send();

	// The sender shared its block with this process, as the owner of the window
	// it found.
	auto* senderData = shared.lock(senderHandle, GetCurrentProcessId());
	if (senderData == nullptr) return send();

	DWORD targetPid = 0;
	GetWindowThreadProcessId(this->previous, &targetPid);

	// The same block, shared with the real notification area's process.
	auto* targetHandle =
	    targetPid != 0 ? shared.alloc(senderData, SHARED_APPBAR_SIZE, targetPid) : nullptr;
	LRESULT result = 0;

	if (targetHandle != nullptr) {
		message.sharedMemory =
		    static_cast<ULONG64>(reinterpret_cast<ULONG_PTR>(targetHandle)); // NOLINT
		result = send();

		// A block is opened with the id of the process it was shared with.
		if (auto* answer = shared.lock(targetHandle, targetPid)) {
			std::memcpy(senderData, answer, SHARED_APPBAR_SIZE);
			shared.unlock(answer);
		} else {
			qCWarning(logTray) << "Could not read the notification area's answer to an AppBar request";
		}

		shared.free(targetHandle, targetPid);
	} else {
		qCWarning(logTray) << "Could not share AppBar data with the notification area, error"
		                   << GetLastError();
		result = send();
	}

	shared.unlock(senderData);

	return result;
}

LRESULT TrayHost::onCopyData(WPARAM wParam, LPARAM lParam) {
	auto* copyData = reinterpret_cast<COPYDATASTRUCT*>(lParam); // NOLINT
	auto handled = false;

	if (copyData != nullptr && copyData->dwData == COPYDATA_NOTIFYICON && copyData->lpData != nullptr)
	{
		handled = this->onNotifyIcon(copyData->lpData, copyData->cbData);
	}

	// Everything goes on to the real notification area, including AppBar
	// requests, which travel through the same window and expect its answer.
	auto forwarded = false;
	LRESULT result = 0;

	if (copyData != nullptr && copyData->dwData == COPYDATA_APPBAR
	    && copyData->cbData == sizeof(AppBarMessageData) && copyData->lpData != nullptr)
	{
		result =
		    this->forwardAppBar(wParam, *static_cast<AppBarMessageData*>(copyData->lpData), &forwarded);
	} else {
		result = this->forward(wParam, lParam, &forwarded);
	}

	if (copyData != nullptr && copyData->dwData != COPYDATA_NOTIFYICON) {
		qCDebug(logTray) << "Forwarded request of type" << copyData->dwData << "size"
		                 << copyData->cbData << "forwarded" << forwarded << "result" << result;
	}

	if (handled) return TRUE;
	return forwarded ? result : FALSE;
}

TrayItem* TrayHost::find(HWND window, UINT iconId, const GUID& guid, bool hasGuid) const {
	for (auto* item: this->all) {
		if (hasGuid && item->hasGuid) {
			if (IsEqualGUID(item->guid, guid)) return item;
		} else if (!hasGuid && !item->hasGuid) {
			if (item->window == window && item->iconId == iconId) return item;
		}
	}

	return nullptr;
}

void TrayHost::updateListing(TrayItem* item) {
	auto shouldList = !item->hidden;
	if (shouldList == item->listed) return;

	if (shouldList) this->mItems.insertObject(item);
	else this->mItems.removeObject(item);

	item->listed = shouldList;
}

void TrayHost::remove(TrayItem* item) {
	if (item->listed) this->mItems.removeObject(item);
	this->all.removeOne(item);
	item->deleteLater();
}

bool TrayHost::onNotifyIcon(const void* data, DWORD size) {
	// Older senders use a shorter structure, so only what was sent is read.
	ShellTrayData tray = {};
	if (size < offsetof(ShellTrayData, nid) + offsetof(NotifyIconData32, szTip)) return false;
	std::memcpy(&tray, data, std::min<size_t>(size, sizeof(ShellTrayData)));

	if (tray.signature != NOTIFYICON_SIGNATURE) return false;

	const auto& nid = tray.nid;
	auto window = widenHandle<HWND>(nid.hWnd);
	auto hasGuid = (nid.uFlags & NIF_GUID) != 0;
	auto* item = this->find(window, nid.uID, nid.guidItem, hasGuid);

	switch (tray.message) {
	case NIM_ADD:
	case NIM_MODIFY: {
		if (item == nullptr) {
			// A modify for an icon that was never seen being added is treated as
			// an add: the icon predates this window.
			item = new TrayItem(this);
			item->window = window;
			item->iconId = nid.uID;
			item->guid = nid.guidItem;
			item->hasGuid = hasGuid;
			item->mId = processName(window);
			this->all.append(item);
			qCDebug(logTray) << "Icon added by" << item->mId;
		}

		if ((nid.uFlags & NIF_MESSAGE) != 0) item->callbackMessage = nid.uCallbackMessage;

		if ((nid.uFlags & NIF_TIP) != 0) {
			auto tip =
			    QString::fromWCharArray(nid.szTip, static_cast<qsizetype>(wcsnlen(nid.szTip, 128)));
			if (tip != item->mTitle) {
				item->mTitle = tip;
				emit item->titleChanged();
			}
		}

		if ((nid.uFlags & NIF_ICON) != 0) {
			// The application may destroy the icon once Shell_NotifyIcon returns,
			// so it is copied now.
			auto* icon = widenHandle<HICON>(nid.hIcon);
			item->image = icon != nullptr ? QImage::fromHICON(icon) : QImage();
			item->imageHandle.imageChanged();
			emit item->iconChanged();
		}

		if ((nid.uFlags & NIF_STATE) != 0 && (nid.dwStateMask & NIS_HIDDEN) != 0) {
			item->hidden = (nid.dwState & NIS_HIDDEN) != 0;
		}

		this->updateListing(item);
		return true;
	}
	case NIM_DELETE:
		if (item != nullptr) {
			qCDebug(logTray) << "Icon removed by" << item->mId;
			this->remove(item);
		}
		return true;
	case NIM_SETVERSION:
		if (item != nullptr) item->version = nid.uVersion;
		return true;
	case NIM_SETFOCUS: return true;
	default: return false;
	}
}

void TrayHost::maintain() {
	if (this->hwnd == nullptr) return;

	// FindWindow returns the highest window of the class, which is where
	// Shell_NotifyIcon sends. The taskbar raises itself from time to time.
	auto* first = FindWindowW(TRAY_CLASS, nullptr);
	if (first != this->hwnd) {
		// A window of this class that is not ours and not the one we forward
		// to means the taskbar restarted.
		if (first != nullptr && first != this->previous) this->previous = first;

		SetWindowPos(this->hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
	}

	// An application that exits without removing its icon leaves it behind.
	for (auto* item: QList<TrayItem*>(this->all)) {
		if (!IsWindow(item->window)) this->remove(item);
	}
}

} // namespace qs::win32::tray
