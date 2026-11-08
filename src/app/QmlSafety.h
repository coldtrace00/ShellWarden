#pragma once
#include <QUrl>

class QQmlEngine;

// The interface never needs the network or other computers. Text from servers
// (file names, error messages, proxy replies, banners) ends up in QML, and any
// QML text shown as rich text turns "<img src=…>" into a load: http:// goes to
// the web directly (outside the proxy, Tor and kill switch), and on Windows
// file://server/share/… is a UNC path, an SMB connection that reveals the real
// IP and can hand out an NTLM hash.
//
// The defence is in this order:
//  1. Text is plain text. Every Text/Label in qml/ sets textFormat: PlainText
//     (checked in CI by tests/check_qml_text.py), controls are used only
//     through App* wrappers with plain labels, and the shared ToolTip is
//     switched to plain text (PlainToolTips.qml). This is what really protects.
//  2. Text from the other side is cleaned and cut (core/SafeText.h).
//  3. This file: what QML loads by URL goes through allowed() — files built into
//     the program, data: URLs and files on this computer only.
//
// Limit of 3: an <img src='file://server/…'> inside RichText or StyledText is
// read by Qt as a local file WITHOUT asking the interceptor (also in current
// Qt), so on Windows it would reach the share. http:// in rich text is stopped
// (network access goes through here), Image.source is stopped, but for that
// one case only 1 and 2 protect. tests/qml_safety_tests.cpp checks the limit is
// still there, so this comment stays true.
namespace qmlsafety {

// qrc:, data:, and file: with no host and no "//server" path. Everything else
// (http, https, ftp, file://host/…, file:////server/…, \\server\…) is refused.
bool allowed(const QUrl &url);

// Puts allowed() in front of the engine: its network access (images, XHR,
// remote QML, http images in rich text) and its URL interceptor (Image.source,
// imports — but not images inside rich text, see above). Call before loading
// anything. Safe to call more than once per engine.
void install(QQmlEngine *engine);

// How many URLs were refused since start (for tests and diagnostics).
int blockedCount();

} // namespace qmlsafety
