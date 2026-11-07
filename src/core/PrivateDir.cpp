#include "PrivateDir.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QObject>

namespace privatedir {

bool protect(const QString &dir, QString *error)
{
    if (!QFileInfo(dir).isDir()) {
        if (error)
            *error = QObject::tr("Folder not found: %1").arg(dir);
        return false;
    }
#ifdef _WIN32
    unsigned long code = 0;
    const std::wstring w = QDir::toNativeSeparators(dir).toStdWString();
    if (protectWindowsDir(w.c_str(), &code) < 0) {
        if (error)
            *error = QObject::tr("Could not limit access to %1 to your user account (Windows error %2). "
                                 "Other accounts on this computer may be able to read it.")
                         .arg(QDir::toNativeSeparators(dir)).arg(code);
        return false;
    }
    return true;
#else
    const auto dirPerms = QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner;
    bool ok = QFile::setPermissions(dir, dirPerms);
    QDirIterator it(dir, QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QFileInfo fi = it.fileInfo();
        if (fi.isSymLink())
            continue;   // never follow links out of the folder
        if (fi.isDir()) {
            ok = QFile::setPermissions(path, dirPerms) && ok;
        } else {
            QFileDevice::Permissions p = QFileDevice::ReadOwner | QFileDevice::WriteOwner;
            if (fi.permissions() & QFileDevice::ExeOwner)
                p |= QFileDevice::ExeOwner;
            ok = QFile::setPermissions(path, p) && ok;
        }
    }
    if (!ok && error)
        *error = QObject::tr("Could not make every file in %1 private (chmod failed).").arg(dir);
    return ok;
#endif
}

} // namespace privatedir
