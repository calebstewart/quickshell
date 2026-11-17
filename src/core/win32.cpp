#include "win32.hpp"
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

#include <io.h>
#include <qdir.h>
#include <qfile.h>
#include <qstring.h>
#include <windows.h>
#include <winioctl.h>

namespace qs::win32 {

namespace {

HANDLE osHandle(QFile& file) {
	auto fd = file.handle();
	if (fd == -1) return INVALID_HANDLE_VALUE;
	return reinterpret_cast<HANDLE>(_get_osfhandle(fd)); // NOLINT
}

OVERLAPPED markerRange() {
	OVERLAPPED range = {};
	range.Offset = 0;
	range.OffsetHigh = 0x7fffffff;
	return range;
}

// Not exposed by the mingw-w64 headers.
struct ReparseMountPointBuffer {
	DWORD reparseTag;
	WORD reparseDataLength;
	WORD reserved;
	WORD substituteNameOffset;
	WORD substituteNameLength;
	WORD printNameOffset;
	WORD printNameLength;
	WCHAR pathBuffer[1]; // NOLINT
};

} // namespace

LockResult tryLockMarker(QFile& file) {
	auto handle = osHandle(file);
	if (handle == INVALID_HANDLE_VALUE) return LockResult::Error;

	auto range = markerRange();
	if (LockFileEx(handle, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &range)) {
		return LockResult::Acquired;
	}

	auto error = GetLastError();
	if (error == ERROR_LOCK_VIOLATION || error == ERROR_IO_PENDING) return LockResult::Contended;
	return LockResult::Error;
}

bool isMarkerLocked(QFile& file) {
	auto handle = osHandle(file);
	if (handle == INVALID_HANDLE_VALUE) return false;

	auto range = markerRange();
	if (LockFileEx(handle, LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &range)) {
		range = markerRange();
		UnlockFileEx(handle, 0, 1, 0, &range);
		return false;
	}

	return true;
}

bool waitMarkerUnlocked(QFile& file) {
	auto handle = osHandle(file);
	if (handle == INVALID_HANDLE_VALUE) return false;

	// A shared lock, so several waiters do not block each other.
	auto range = markerRange();
	return LockFileEx(handle, 0, 0, 1, 0, &range) != 0;
}

bool replaceDirLink(const QString& target, const QString& link) {
	auto nativeLink = QDir::toNativeSeparators(link).toStdWString();
	auto nativeTarget = QDir::toNativeSeparators(QDir(target).absolutePath()).toStdWString();

	// Removes a junction or an empty directory, never the junction's target.
	RemoveDirectoryW(nativeLink.c_str());
	DeleteFileW(nativeLink.c_str());

	if (!CreateDirectoryW(nativeLink.c_str(), nullptr)) return false;

	auto handle = CreateFileW(
	    nativeLink.c_str(),
	    GENERIC_WRITE,
	    0,
	    nullptr,
	    OPEN_EXISTING,
	    FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS,
	    nullptr
	);

	if (handle == INVALID_HANDLE_VALUE) {
		RemoveDirectoryW(nativeLink.c_str());
		return false;
	}

	auto substituteName = std::wstring(L"\\??\\") + nativeTarget;
	auto substituteBytes = substituteName.size() * sizeof(WCHAR);
	auto printBytes = nativeTarget.size() * sizeof(WCHAR);

	// Both names are stored null terminated, one after the other.
	auto pathBytes = substituteBytes + sizeof(WCHAR) + printBytes + sizeof(WCHAR);
	auto headerBytes = offsetof(ReparseMountPointBuffer, pathBuffer);
	auto buffer = std::vector<char>(headerBytes + pathBytes, 0);

	auto* reparse = reinterpret_cast<ReparseMountPointBuffer*>(buffer.data()); // NOLINT
	reparse->reparseTag = IO_REPARSE_TAG_MOUNT_POINT;
	reparse->reparseDataLength = static_cast<WORD>(4 * sizeof(WORD) + pathBytes);
	reparse->substituteNameOffset = 0;
	reparse->substituteNameLength = static_cast<WORD>(substituteBytes);
	reparse->printNameOffset = static_cast<WORD>(substituteBytes + sizeof(WCHAR));
	reparse->printNameLength = static_cast<WORD>(printBytes);

	auto* path = buffer.data() + headerBytes; // NOLINT
	std::memcpy(path, substituteName.data(), substituteBytes);
	std::memcpy(path + substituteBytes + sizeof(WCHAR), nativeTarget.data(), printBytes); // NOLINT

	DWORD returned = 0;
	auto ok = DeviceIoControl(
	    handle,
	    FSCTL_SET_REPARSE_POINT,
	    buffer.data(),
	    static_cast<DWORD>(buffer.size()),
	    nullptr,
	    0,
	    &returned,
	    nullptr
	);

	CloseHandle(handle);

	if (!ok) {
		RemoveDirectoryW(nativeLink.c_str());
		return false;
	}

	return true;
}

} // namespace qs::win32
