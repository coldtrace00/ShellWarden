#pragma once

#include <QString>

// The security-relevant libraries must be new enough not only at build time
// (cmake/Deps.cmake) but also at run time: a DLL next to the exe or a system
// library can be swapped for an older one.
//   libssh    >= 0.10.6  (Terrapin, CVE-2023-48795)
//   libsodium >= 1.0.18
//   OpenSSL   >= 3.0
namespace depversions {

// Empty when everything is fine, otherwise what is too old.
QString runtimeProblem();

// "libssh 0.11.1 · libsodium 1.0.20 · OpenSSL 3.4.1" for the About box
QString summary();

} // namespace depversions
