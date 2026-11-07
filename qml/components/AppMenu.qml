import QtQuick
import QtQuick.Controls
import ShellWarden

// Menu that is as wide as its longest item, so nothing is cut off.
// Colours are set here and not inherited: on Windows (Qt 6.8+) a menu can be
// its own native window, which does not see the main window's dark palette.
Menu {
    id: menu
    margins: 6      // never past the window edge
    topPadding: 4
    bottomPadding: 4
    width: {
        let w = 200
        for (let i = 0; i < count; ++i) {
            const it = itemAt(i)
            if (it && it.visible)
                w = Math.max(w, it.implicitWidth + (it.checkable ? 28 : 0))
        }
        return Math.min(w + 8, 520)
    }
    palette.window: Theme.surface
    palette.windowText: Theme.text
    palette.base: Theme.surface
    palette.text: Theme.text
    palette.button: Theme.surface2
    palette.buttonText: Theme.text
    palette.highlight: Theme.accent
    palette.highlightedText: "#0b1214"
    palette.light: Theme.surfaceHover
    palette.mid: Theme.border
    palette.dark: Theme.border
    palette.midlight: Theme.surface2
    background: Rectangle {
        implicitWidth: 200
        radius: 10
        color: Theme.surface
        border.color: Theme.border
    }
    delegate: AppMenuItem {}
}
