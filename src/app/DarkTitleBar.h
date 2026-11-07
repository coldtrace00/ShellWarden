#pragma once

class QWindow;

// Windows 10/11: the window's own title bar (min / max / close) dark, in the
// colour of the app, whatever the Windows light/dark setting is.
// Elsewhere it does nothing (the window manager draws the title bar).
void applyDarkTitleBar(QWindow *window);
