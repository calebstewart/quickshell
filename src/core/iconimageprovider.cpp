#include "iconimageprovider.hpp"
#include <algorithm>

#include <qabstractfileiconprovider.h>
#include <qcolor.h>
#include <qfileinfo.h>
#include <qicon.h>
#include <qurl.h>

#ifdef Q_OS_WIN
#include <qdir.h>
#include <qimage.h>
#include <windows.h>
// windows.h must come first
#include <commctrl.h>
#include <commoncontrols.h>
#include <shellapi.h>

namespace {

// The icon the shell shows for a file, from its large icon lists. Unlike the
// icon of the file itself, this has no shortcut arrow drawn over it.
QImage shellIcon(const QString& path) {
	// IID_IImageList, spelled out as not every import library defines it.
	static const GUID imageListId = {
	    0x46eb5926,
	    0x582e,
	    0x4017,
	    {0x9f, 0xdf, 0xe8, 0x99, 0x8d, 0xaa, 0x09, 0x50},
	};

	SHFILEINFOW info = {};
	auto native = QDir::toNativeSeparators(path).toStdWString();
	if (SHGetFileInfoW(native.c_str(), 0, &info, sizeof(info), SHGFI_SYSICONINDEX) == 0) return {};

	auto fromList = [&](int list) -> QImage {
		IImageList* images = nullptr;
		if (FAILED(SHGetImageList(list, imageListId, reinterpret_cast<void**>(&images))) // NOLINT
		    || images == nullptr)
		{
			return {};
		}

		HICON handle = nullptr;
		QImage image;
		if (SUCCEEDED(images->GetIcon(info.iIcon, ILD_TRANSPARENT, &handle)) && handle != nullptr) {
			image = QImage::fromHICON(handle);
			DestroyIcon(handle);
		}

		images->Release();
		return image;
	};

	// The 256 pixel list holds a small icon in one corner of an otherwise
	// empty image when the application has no large icon.
	auto jumbo = fromList(SHIL_JUMBO);
	if (!jumbo.isNull()) {
		auto usesWholeImage = false;
		for (auto y = 80; y < jumbo.height() && !usesWholeImage; y += 8) {
			for (auto x = 80; x < jumbo.width(); x += 8) {
				if (qAlpha(jumbo.pixel(x, y)) != 0) {
					usesWholeImage = true;
					break;
				}
			}
		}

		if (usesWholeImage) return jumbo;
	}

	return fromList(SHIL_EXTRALARGE);
}

} // namespace
#endif
#include <qlogging.h>
#include <qpainter.h>
#include <qpixmap.h>
#include <qsize.h>
#include <qstring.h>

QPixmap
IconImageProvider::requestPixmap(const QString& id, QSize* size, const QSize& requestedSize) {
	QString iconName;
	QString fallbackName;
	QString path;

	auto splitIdx = id.indexOf("?path=");
	if (splitIdx != -1) {
		iconName = id.sliced(0, splitIdx);
		path = id.sliced(splitIdx + 6);
		path = QString("/%1/%2").arg(path, iconName.sliced(iconName.lastIndexOf('/') + 1));
	} else {
		splitIdx = id.indexOf("?fallback=");
		if (splitIdx != -1) {
			iconName = id.sliced(0, splitIdx);
			fallbackName = id.sliced(splitIdx + 10);
		} else {
			iconName = id;
		}
	}

	auto icon = QIcon::fromTheme(iconName);

#ifdef Q_OS_WIN
	// There are no icon themes on Windows. An icon named by a file path is the
	// icon the shell shows for that file, which for a shortcut or a program is
	// the application's own.
	if (icon.isNull()) {
		auto file = QFileInfo(iconName);
		if (!file.exists()) file = QFileInfo(QUrl::fromPercentEncoding(iconName.toUtf8()));

		if (file.isAbsolute() && file.exists()) {
			auto image = shellIcon(file.absoluteFilePath());
			if (!image.isNull()) icon = QIcon(QPixmap::fromImage(image));
			else icon = QAbstractFileIconProvider().icon(file);
		}
	}
#endif

	if (icon.isNull() && !fallbackName.isEmpty()) icon = QIcon::fromTheme(fallbackName);
	if (icon.isNull() && !path.isEmpty()) icon = QPixmap(path);

	auto targetSize = requestedSize.isValid() ? requestedSize : QSize(100, 100);
	if (targetSize.width() == 0 || targetSize.height() == 0) targetSize = QSize(2, 2);
	auto pixmap = icon.pixmap(targetSize.width(), targetSize.height());

	if (pixmap.isNull()) {
		qWarning() << "Could not load icon" << id << "at size" << targetSize << "from request";
		pixmap = IconImageProvider::missingPixmap(targetSize);
	}

	if (size != nullptr) *size = pixmap.size();
	return pixmap;
}

QPixmap IconImageProvider::missingPixmap(const QSize& size) {
	auto width = size.width() % 2 == 0 ? size.width() : size.width() + 1;
	auto height = size.height() % 2 == 0 ? size.height() : size.height() + 1;
	width = std::max(width, 2);
	height = std::max(height, 2);

	auto pixmap = QPixmap(width, height);
	pixmap.fill(QColorConstants::Black);
	auto painter = QPainter(&pixmap);

	auto halfWidth = width / 2;
	auto halfHeight = height / 2;
	auto purple = QColor(0xd900d8);
	painter.fillRect(halfWidth, 0, halfWidth, halfHeight, purple);
	painter.fillRect(0, halfHeight, halfWidth, halfHeight, purple);
	return pixmap;
}

QString IconImageProvider::requestString(
    const QString& icon,
    const QString& path,
    const QString& fallback
) {
	auto req = "image://icon/" + icon;

	if (!path.isEmpty()) {
		req += "?path=" + path;
	}

	if (!fallback.isEmpty()) {
		req += "?fallback=" + fallback;
	}

	return req;
}
