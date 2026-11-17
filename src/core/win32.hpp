#pragma once

// Windows replacements for the POSIX primitives the core relies on.
// Only compiled and included on Windows.

#include <qcontainerfwd.h>

class QFile;

namespace qs::win32 {

enum class LockResult : quint8 {
	Acquired,
	Contended,
	Error,
};

// Stand-in for a whole-file fcntl write lock used as a liveness marker.
//
// Windows byte range locks are mandatory rather than advisory, so locking the
// file's contents would stop other processes from reading them. The marker is
// instead a single byte far past the end of any real file.
LockResult tryLockMarker(QFile& file);

// Returns true if another handle holds the marker taken by tryLockMarker().
bool isMarkerLocked(QFile& file);

// Blocks until no other handle holds the marker. Returns false on error.
bool waitMarkerUnlocked(QFile& file);

// Replaces whatever is at `link` with a directory link to `target`. Uses a
// junction, which unlike a symlink needs neither privileges nor developer mode.
bool replaceDirLink(const QString& target, const QString& link);

} // namespace qs::win32
