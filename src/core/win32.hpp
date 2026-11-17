#pragma once

// Windows replacements for the POSIX primitives the core relies on.
// Only compiled and included on Windows.

#include <qcontainerfwd.h>
#include <qimage.h>
#include <qlist.h>
#include <qstring.h>

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

// An application installed as a package, such as one from the Store.
struct PackagedApp {
	QString name;
	// Its application user model ID. `shell:AppsFolder\<appId>` names it to
	// the shell, which is how it is started and where its icon comes from.
	QString appId;
};

// The packaged applications the Start Menu lists. They have no shortcut file,
// so scanning the Start Menu's folders does not find them.
QList<PackagedApp> packagedApps();

// The image the shell shows for an item, named the way the shell parses names:
// a path, or something like `shell:AppsFolder\<appId>`. Null if it has none.
QImage shellItemImage(const QString& parsingName, int size);

} // namespace qs::win32
