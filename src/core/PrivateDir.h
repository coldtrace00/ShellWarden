#pragma once

#ifndef PRIVATEDIR_NO_QT
#include <QString>
#endif

// Keeps the data folder private to the user who runs ShellWarden.
//
// Linux: the folder and every folder in it 0700, files 0600 (the owner's
// execute bit is kept, e.g. for a downloaded AppImage).
// Windows: the folder gets its own (protected) access list — the current user,
// SYSTEM and Administrators — and everything in it inherits that list, so
// other user accounts on the same computer cannot read it. Done only when the
// folder is not like that already.
namespace privatedir {

#ifndef PRIVATEDIR_NO_QT
// Returns false and an explanation when the folder could not be made private
// (for example a folder the user may write to but not change permissions of).
bool protect(const QString &dir, QString *error);
#endif

#ifdef _WIN32
// Qt-free core of the Windows part (so it can be built and tested alone).
// Returns 0 when nothing had to change, 1 when the access list was replaced,
// -1 on error (Win32 error code in *winError).
int protectWindowsDir(const wchar_t *path, unsigned long *winError);
#endif

} // namespace privatedir
