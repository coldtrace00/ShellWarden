import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ShellWarden

// Thin strip under the terminal: CPU, memory, disk, uptime, network of the server.
// Which items are shown and how often they refresh is set in the gear menu
// (also in Settings → Terminal).
Rectangle {
    id: root
    property TerminalSession session: null
    readonly property var m: session ? session.monitor : ({})
    readonly property var items: String(App.settings.monitorItems || "").split(",").filter(s => s !== "")
    function has(k) { return items.indexOf(k) >= 0 }
    function toggle(k) {
        const a = items.slice()
        const i = a.indexOf(k)
        if (i >= 0) a.splice(i, 1); else a.push(k)
        App.setSetting("monitorItems", a.join(","))
    }
    function bytes(b) { return b === undefined ? "–" : App.formatBytes(b) }
    function rate(b) { return b === undefined ? "–" : App.formatBytes(b) + "/s" }
    function uptime(s) {
        if (s === undefined) return "–"
        const d = Math.floor(s / 86400), h = Math.floor(s % 86400 / 3600), mi = Math.floor(s % 3600 / 60)
        return d > 0 ? qsTr("%1 d %2 h").arg(d).arg(h) : h > 0 ? qsTr("%1 h %2 min").arg(h).arg(mi) : qsTr("%1 min").arg(mi)
    }
    function pct(used, total) { return total > 0 ? used / total : -1 }
    function level(p) { return p < 0 ? Theme.textFaint : p > 0.9 ? Theme.danger : p > 0.7 ? Theme.warn : Theme.ok }

    implicitHeight: 30
    color: Theme.surface
    Rectangle { width: parent.width; height: 1; color: Theme.border }

    component Gauge: RowLayout {
        id: g
        property string title: ""
        property string value: ""
        property real frac: -1        // 0..1 draws a small bar, <0 = no bar
        property string tip: ""
        spacing: 6
        Text { textFormat: Text.PlainText; text: g.title; color: Theme.textFaint; font.pixelSize: 11 }
        Rectangle {
            visible: g.frac >= 0
            implicitWidth: 42; implicitHeight: 6; radius: 3
            color: Theme.surface2
            Rectangle { width: Math.max(2, parent.width * Math.min(1, g.frac)); height: parent.height; radius: 3; color: root.level(g.frac)
                        Behavior on width { NumberAnimation { duration: 300 } } }
        }
        Text { textFormat: Text.PlainText; text: g.value; color: Theme.text; font.pixelSize: 11; font.family: Theme.monoFont }
        HoverHandler { id: gh }
        ToolTip.visible: gh.hovered && g.tip !== ""
        ToolTip.text: g.tip
        ToolTip.delay: 400
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 6
        spacing: 16
        clip: true

        Text {
            textFormat: Text.PlainText
            visible: !root.m.ok
            Layout.fillWidth: true
            text: root.m.error ? root.m.error : qsTr("Reading the server load…")
            color: Theme.textFaint
            font.pixelSize: 11
            elide: Text.ElideRight
        }
        Gauge {
            visible: root.m.ok === true && root.has("cpu")
            title: qsTr("CPU")
            frac: root.m.cpu !== undefined ? root.m.cpu / 100 : -1
            value: root.m.cpu !== undefined ? Math.round(root.m.cpu) + "%" : "…"
            tip: root.m.cores ? qsTr("%1 cores").arg(root.m.cores) : ""
        }
        Gauge {
            visible: root.m.ok === true && root.has("load")
            title: qsTr("Load")
            value: root.m.load1 !== undefined ? Number(root.m.load1).toFixed(2) : "–"
            tip: qsTr("Average number of running processes over the last minute")
        }
        Gauge {
            visible: root.m.ok === true && root.has("ram") && root.m.memTotal > 0
            title: qsTr("RAM")
            frac: root.pct(root.m.memUsed, root.m.memTotal)
            value: root.bytes(root.m.memUsed) + " / " + root.bytes(root.m.memTotal)
        }
        Gauge {
            visible: root.m.ok === true && root.has("swap") && root.m.swapTotal > 0
            title: qsTr("Swap")
            frac: root.pct(root.m.swapUsed, root.m.swapTotal)
            value: root.bytes(root.m.swapUsed)
        }
        Gauge {
            visible: root.m.ok === true && root.has("disk") && root.m.diskTotal > 0
            title: qsTr("Disk /")
            frac: root.pct(root.m.diskUsed, root.m.diskTotal)
            value: Math.round(root.pct(root.m.diskUsed, root.m.diskTotal) * 100) + "%"
            tip: qsTr("%1 used of %2").arg(root.bytes(root.m.diskUsed)).arg(root.bytes(root.m.diskTotal))
        }
        Gauge {
            visible: root.m.ok === true && root.has("net")
            title: qsTr("Net")
            value: "↓ " + root.rate(root.m.rxRate) + "  ↑ " + root.rate(root.m.txRate)
        }
        Gauge {
            visible: root.m.ok === true && root.has("uptime")
            title: qsTr("Up")
            value: root.uptime(root.m.uptime)
            tip: qsTr("Time since the server was last restarted")
        }
        Item { Layout.fillWidth: true; visible: root.m.ok === true }

        AbstractButton {
            id: gear
            implicitWidth: 24; implicitHeight: 24
            hoverEnabled: true
            onClicked: cfg.opened ? cfg.close() : cfg.open()
            background: Rectangle { radius: 6; color: gear.hovered || cfg.opened ? Theme.surface2 : "transparent" }
            contentItem: Item { IconLabel { anchors.centerIn: parent; glyph: Theme.iconSettings; size: 12; color: Theme.textDim } }
            ToolTip.visible: hovered && !cfg.opened
            ToolTip.text: qsTr("What to show and how often")
        }
    }

    // settings popover (opens upwards from the gear)
    Popup {
        id: cfg
        x: root.width - width - 6
        y: -height - 6
        width: 300
        padding: 14
        palette.window: Theme.surface
        palette.windowText: Theme.text
        palette.base: Theme.surface2
        palette.text: Theme.text
        palette.button: Theme.surface2
        palette.buttonText: Theme.text
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
        background: Rectangle { radius: 10; color: Theme.surface; border.color: Theme.border }
        contentItem: ColumnLayout {
            spacing: 10
            ColumnLayout {
                spacing: 2
                Text { textFormat: Text.PlainText; text: qsTr("Server monitor"); color: Theme.text; font.pixelSize: 14; font.weight: Font.DemiBold }
                Text { textFormat: Text.PlainText; Layout.fillWidth: true; text: qsTr("What to show on the strip"); color: Theme.textFaint; font.pixelSize: 11 }
            }
            // a card with one tappable row per item
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: itemsCol.implicitHeight + 8
                radius: 8
                color: Theme.bg
                border.color: Theme.border
                ColumnLayout {
                    id: itemsCol
                    anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                    anchors.margins: 4
                    spacing: 0
                    Repeater {
                        model: [["cpu", qsTr("CPU"), "54%"], ["load", qsTr("Load average"), "0.42"], ["ram", qsTr("Memory (RAM)"), "2.1 / 8 GB"],
                                ["swap", qsTr("Swap"), "0 B"], ["disk", qsTr("Disk /"), "37%"], ["net", qsTr("Network speed"), "↓ 1.2 MB/s"],
                                ["uptime", qsTr("Uptime"), "12 d 4 h"]]
                        delegate: AbstractButton {
                            id: itemRow
                            Layout.fillWidth: true
                            implicitHeight: 34
                            hoverEnabled: true
                            readonly property bool on: root.has(modelData[0])
                            onClicked: root.toggle(modelData[0])
                            background: Rectangle { radius: 6; color: itemRow.hovered ? Theme.surface2 : "transparent" }
                            contentItem: RowLayout {
                                spacing: 8
                                Rectangle {
                                    Layout.leftMargin: 8
                                    width: 16; height: 16; radius: 4
                                    color: itemRow.on ? Theme.accent : "transparent"
                                    border.color: itemRow.on ? Theme.accent : Theme.textFaint
                                    IconLabel { anchors.centerIn: parent; visible: itemRow.on; glyph: Theme.iconCheck; size: 11; color: Theme.bg }
                                }
                                Text { textFormat: Text.PlainText; Layout.fillWidth: true; text: modelData[1]; color: itemRow.on ? Theme.text : Theme.textDim; font.pixelSize: 12 }
                                Text { textFormat: Text.PlainText; Layout.rightMargin: 8; text: modelData[2]; color: Theme.textFaint; font.pixelSize: 11; font.family: Theme.monoFont }
                            }
                        }
                    }
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6
                Text { textFormat: Text.PlainText; text: qsTr("Refresh every"); color: Theme.text; font.pixelSize: 12 }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    Repeater {
                        model: [2, 5, 10, 30, 60]
                        delegate: AbstractButton {
                            id: ivBtn
                            Layout.fillWidth: true
                            implicitHeight: 28
                            hoverEnabled: true
                            readonly property bool on: (App.settings.monitorInterval || 5) === modelData
                            onClicked: App.setSetting("monitorInterval", modelData)
                            background: Rectangle { radius: 6; color: ivBtn.on ? Theme.accentDim : (ivBtn.hovered ? Theme.surface2 : "transparent")
                                                    border.color: ivBtn.on ? Theme.accent : Theme.border }
                            contentItem: Text { textFormat: Text.PlainText; text: qsTr("%1 s").arg(modelData); color: ivBtn.on ? Theme.text : Theme.textDim; font.pixelSize: 11
                                                horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                        }
                    }
                }
            }
            Text {
                textFormat: Text.PlainText
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.textFaint
                font.pixelSize: 10
                text: qsTr("Read through a separate channel: nothing appears in your terminal. Turn it back on in Settings → Terminal.")
            }
            AppButton {
                Layout.fillWidth: true
                text: qsTr("Hide the monitor"); variant: "ghost"; compact: true; glyph: Theme.iconEyeOff
                onClicked: { cfg.close(); App.setSetting("monitorEnabled", false) }
            }
        }
    }
}
