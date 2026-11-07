#pragma once

#include <QString>

namespace filenames {

// A file name that came from a server, made safe to create on this computer
// inside the folder the user chose: no path separators (/ and \), no ".." or
// ".", no control characters, none of the characters Windows forbids
// (<>:"|?*), no trailing dots/spaces, no reserved device names (CON, NUL,
// COM1…), at most 200 characters. Never empty.
QString safeLocal(const QString &remoteName);

} // namespace filenames
