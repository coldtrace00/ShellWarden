pragma Singleton
import QtQuick

QtObject {
    // Surfaces
    readonly property color bg: "#0f111a"
    readonly property color sidebar: "#141725"
    readonly property color surface: "#1a1e2d"
    readonly property color surface2: "#222738"
    readonly property color surfaceHover: "#283047"
    readonly property color border: "#2b3249"
    readonly property color terminalBg: "#12141c"

    // Text
    readonly property color text: "#e6e8f0"
    readonly property color textDim: "#8a91a8"
    readonly property color textFaint: "#79809c"   // hints and secondary lines: still readable on dark

    // Accents
    readonly property color accent: "#4fd1c5"
    readonly property color accentDim: "#1f4f4c"
    readonly property color blue: "#5e8bff"
    readonly property color danger: "#ff5c72"
    readonly property color dangerDim: "#4a1f2a"
    readonly property color warn: "#f5c26b"
    readonly property color warnDim: "#4a3a1c"
    readonly property color ok: "#4fd18b"
    readonly property color okDim: "#1c4a33"

    readonly property int radius: 10
    readonly property int radiusSmall: 6

    readonly property string monoFont: Qt.platform.os === "windows" ? "Cascadia Mono" : "monospace"
    // Icon names (see Icons.qml)
    readonly property string iconAdd: "plus"
    readonly property string iconEdit: "edit"
    readonly property string iconDelete: "trash"
    readonly property string iconMore: "more"
    readonly property string iconKey: "key"
    readonly property string iconLock: "lock"
    readonly property string iconUnlock: "unlock"
    readonly property string iconSettings: "sliders"
    readonly property string iconShield: "shield"
    readonly property string iconShieldCheck: "shieldCheck"
    readonly property string iconGlobe: "globe"
    readonly property string iconCopy: "copy"
    readonly property string iconImage: "image"
    readonly property string iconSave: "download"
    readonly property string iconUpload: "upload"
    readonly property string iconRefresh: "refresh"
    readonly property string iconClose: "x"
    readonly property string iconSearch: "search"
    readonly property string iconTerminal: "terminal"
    readonly property string iconServer: "server"
    readonly property string iconFingerprint: "fingerprint"
    readonly property string iconWarning: "warning"
    readonly property string iconInfo: "info"
    readonly property string iconCheck: "check"
    readonly property string iconPlug: "plug"
    readonly property string iconFolder: "folder"
    readonly property string iconFile: "file"
    readonly property string iconEye: "eye"
    readonly property string iconEyeOff: "eyeOff"
    readonly property string iconSidebar: "sidebar"
    readonly property string iconPopout: "popout"
    readonly property string iconDock: "dock"
    readonly property string iconGenerate: "sparkles"
    readonly property string iconLog: "list"
    readonly property string iconPalette: "palette"
    readonly property string iconTunnel: "tunnel"
    readonly property string iconHop: "hop"
    readonly property string iconSplit: "split"
    readonly property string iconSplitH: "splitH"
    readonly property string iconSnippet: "snippet"
    readonly property string iconSerial: "serial"
    readonly property string iconUpdate: "update"
    readonly property string iconDatabase: "database"
    readonly property string iconActivity: "activity"
    readonly property string iconBack: "back"
    readonly property string iconArrowUp: "arrowUp"
    readonly property string iconArrowDown: "arrowDown"
    readonly property string iconRecord: "record"
    readonly property string iconBroadcast: "broadcast"
    readonly property string iconArrowRight: "arrowRight"
    readonly property string iconPower: "power"
    readonly property string iconClock: "clock"
    readonly property string iconChevronDown: "chevronDown"

    // Stable avatar colour for a host label
    function avatarColor(seed) {
        const palette = ["#4fd1c5", "#5e8bff", "#c58cff", "#f5c26b", "#ff8a65", "#4fd18b", "#ff5c72", "#7dd3fc"]
        let h = 0
        const s = seed || "?"
        for (let i = 0; i < s.length; ++i)
            h = (h * 31 + s.charCodeAt(i)) >>> 0
        return palette[h % palette.length]
    }

    // "#rrggbb" + alpha hex ("2e") -> "#2errggbb"
    function tint(hex, alphaHex) {
        return "#" + alphaHex + String(hex).substring(1)
    }

    function initials(label) {
        const s = (label || "?").trim()
        const parts = s.split(/[\s._-]+/).filter(p => p.length > 0)
        if (parts.length >= 2)
            return (parts[0][0] + parts[1][0]).toUpperCase()
        return s.substring(0, 2).toUpperCase()
    }
}
