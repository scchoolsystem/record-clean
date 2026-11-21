#include "file-manager.hpp"
#include <QDate>
#include <QDir>
#include <QFileInfo>

namespace melton {

uint64_t FreeSpaceFor(const std::string &dir)
{
	return os_get_free_disk_space(dir.c_str());
}

PathResult BuildCleanPath(const Settings &s)
{
	PathResult r;
	QDate d = QDate::currentDate();
	QDir base(QString::fromStdString(s.baseDir));
	QString sub = QString("%1/%2/%3")
			      .arg(d.year(), 4, 10, QChar('0'))
			      .arg(d.month(), 2, 10, QChar('0'))
			      .arg(d.day(), 2, 10, QChar('0'));
	QString dirPath = base.filePath(sub);
	if (!QDir().mkpath(dirPath)) {
		r.error = "Cannot create recording folder: " + dirPath.toStdString();
		return r;
	}
	QFileInfo fi(dirPath);
	if (!fi.isDir() || !fi.isWritable()) {
		r.error = "Recording folder is not writable: " + dirPath.toStdString();
		return r;
	}
	r.dir = dirPath.toUtf8().toStdString();

	uint64_t freeBytes = FreeSpaceFor(r.dir);
	if (freeBytes < kMinFreeBytesToStart) {
		r.error = "Not enough free disk space (" + std::to_string(freeBytes / (1024 * 1024)) +
			  " MB free, 2048 MB needed to start)";
		return r;
	}

	char *stem = os_generate_formatted_filename(s.format.c_str(), false, s.filenameFormat.c_str());
	if (!stem) {
		r.error = "Invalid filename format";
		return r;
	}
	QString file = QString("CLEAN_") + QString::fromUtf8(stem);
	bfree(stem);
	QString candidate = QDir(dirPath).filePath(file);
	int n = 1;
	while (QFileInfo::exists(candidate)) { // never overwrite
		QFileInfo f(file);
		candidate = QDir(dirPath).filePath(f.completeBaseName() + QString("_%1.").arg(++n) + f.suffix());
	}
	r.path = candidate.toUtf8().toStdString();
	r.ok = true;
	return r;
}

} // namespace melton
