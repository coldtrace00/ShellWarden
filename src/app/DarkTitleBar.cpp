#include "DarkTitleBar.h"

#include <QWindow>

#ifdef Q_OS_WIN
#  include <windows.h>
#  include <dwmapi.h>

namespace {
// not in every MinGW header
constexpr DWORD kImmersiveDarkOld = 19;   // Windows 10 1809–1909
constexpr DWORD kImmersiveDark = 20;      // Windows 10 2004+ / 11
constexpr DWORD kBorderColor = 34;        // Windows 11
constexpr DWORD kCaptionColor = 35;       // Windows 11
constexpr DWORD kTextColor = 36;          // Windows 11

constexpr COLORREF rgb(int r, int g, int b) { return COLORREF(r | (g << 8) | (b << 16)); }
}

void applyDarkTitleBar(QWindow *window)
{
    if (!window || window->type() == Qt::Popup || window->type() == Qt::ToolTip)
        return;
    const HWND hwnd = reinterpret_cast<HWND>(window->winId());
    if (!hwnd)
        return;
    BOOL dark = TRUE;
    if (FAILED(DwmSetWindowAttribute(hwnd, kImmersiveDark, &dark, sizeof dark)))
        DwmSetWindowAttribute(hwnd, kImmersiveDarkOld, &dark, sizeof dark);
    // Windows 11: exactly the colours of the app (Theme.bg / Theme.border / Theme.textDim)
    const COLORREF caption = rgb(0x0f, 0x11, 0x1a);
    const COLORREF border = rgb(0x2b, 0x32, 0x49);
    const COLORREF text = rgb(0xc9, 0xcd, 0xd8);
    DwmSetWindowAttribute(hwnd, kCaptionColor, &caption, sizeof caption);
    DwmSetWindowAttribute(hwnd, kBorderColor, &border, sizeof border);
    DwmSetWindowAttribute(hwnd, kTextColor, &text, sizeof text);
}
#else
void applyDarkTitleBar(QWindow *) {}
#endif
