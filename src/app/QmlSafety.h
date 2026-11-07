#pragma once
#include <QUrl>

class QQmlEngine;

// The interface never needs the network or other computers. Text from servers
// (file names, error messages, proxy replies, banners) ends up in QML, and any
// QML text shown as rich text turns "<img src=…>" into a load: http:// goes to
// the web directly (outside the proxy, Tor and kill switch), and on Windows
// file://server/share/… is a UNC path, an SMB connection that reveals the real
// IP and can hand out an NTLM hash. So every URL QML loads goes through
// allowed(): files built into the program, data: URLs and files on this
// computer only.
namespace qmlsafety {

// qrc:, data:, and file: with no host and no "//server" path. Everything else
// (http, https, ftp, file://host/…, file:////server/…, \\server\…) is refused.
bool allowed(const QUrl &url);

// Puts allowed() in front of the engine: its network access (images, XHR,
// remote QML) and its URL interceptor (Image.source, rich-text images,
// imports — local files that QML reads without the network). Call before
// loading anything. Safe to call more than once per engine.
void install(QQmlEngine *engine);

// How many URLs were refused since start (for tests and diagnostics).
int blockedCount();

} // namespace qmlsafety
