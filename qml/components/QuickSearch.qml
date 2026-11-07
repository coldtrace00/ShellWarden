import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ShellWarden

// Ctrl+K / Ctrl+Shift+K: one search box for hosts, open tabs, snippets and pages.
// Matching is by any part of the name, address, user or group, and also by
// letters in order ("prdweb" finds "prod-web-01").
Popup {
    id: root
    parent: Overlay.overlay
    x: Math.round((parent.width - width) / 2)
    y: Math.round(parent.height * 0.12)
    width: Math.min(620, parent.width - 32)
    height: Math.min(460, parent.height - y - 20)
    modal: true
    focus: true
    padding: 10
    Overlay.modal: Rectangle { color: "#80070910" }
    background: Rectangle { radius: 12; color: Theme.surface; border.color: Theme.accent }

    signal openTab(int index)
    signal openPage(string page)
    property int currentTab: -1

    property var results: []
    property int sel: 0

    // returns a score > 0 when q matches s (higher is better), 0 otherwise
    function score(q, s) {
        if (!s) return 0
        s = String(s).toLowerCase()
        const i = s.indexOf(q)
        if (i === 0) return 100
        if (i > 0) return 80 - Math.min(i, 30)
        // letters in order
        let pos = -1, gaps = 0
        for (const ch of q) {
            const n = s.indexOf(ch, pos + 1)
            if (n < 0) return 0
            gaps += n - pos - 1
            pos = n
        }
        return Math.max(1, 40 - gaps)
    }
    function rebuild() {
        const q = field.text.trim().toLowerCase()
        const out = []
        const add = (kind, title, sub, glyph, act, fields) => {
            let best = q === "" ? 1 : 0
            for (const f of fields) best = Math.max(best, score(q, f))
            if (best > 0) out.push({ kind: kind, title: title, sub: sub, glyph: glyph, act: act, s: best + (kind === "tab" ? 5 : 0) })
        }
        for (let i = 0; i < App.sessions.count; ++i) {
            const s = App.sessions.at(i)
            add("tab", s.label, qsTr("Open tab") + " · " + s.endpoint, Theme.iconTerminal, { t: "tab", i: i }, [s.label, s.endpoint])
        }
        for (let i = 0; i < App.hosts.count; ++i) {
            const h = App.hosts.get(i)
            const addr = h.type === "serial" ? h.serialPort : ((h.username ? h.username + "@" : "") + h.hostname + (h.port && h.port !== 22 ? ":" + h.port : ""))
            add("host", h.label, qsTr("Connect") + " · " + addr + (h.group ? " · " + h.group : ""), Theme.iconServer,
                { t: "host", id: h.id }, [h.label, h.hostname, h.username, h.group, addr])
        }
        for (let i = 0; i < App.snippets.count; ++i) {
            const sn = App.snippets.get(i)
            add("snippet", sn.name, qsTr("Run in the current tab") + " · " + String(sn.command).split("\n")[0], Theme.iconSnippet,
                { t: "snippet", id: sn.id }, [sn.name, sn.group, sn.command])
        }
        const pages = [["hosts", qsTr("Hosts")], ["keys", qsTr("Keychain")], ["proxies", qsTr("Proxies")], ["snippets", qsTr("Snippets")],
                       ["known", qsTr("Known hosts")], ["log", qsTr("Activity log")], ["settings", qsTr("Settings")]]
        for (const p of pages)
            add("page", p[1], qsTr("Go to page"), Theme.iconArrowUp, { t: "page", p: p[0] }, [p[1], p[0]])
        out.sort((a, b) => b.s - a.s)
        results = out.slice(0, 60)
        sel = 0
    }
    function activate(r) {
        if (!r) return
        close()
        const a = r.act
        if (a.t === "tab") root.openTab(a.i)
        else if (a.t === "host") App.openHost(a.id)
        else if (a.t === "page") root.openPage(a.p)
        else if (a.t === "snippet") App.runSnippet(a.id, root.currentTab, false)
    }

    onOpened: { field.text = ""; rebuild(); field.forceActiveFocus() }

    contentItem: ColumnLayout {
        spacing: 8
        TextField {
            id: field
            Layout.fillWidth: true
            implicitHeight: 40
            leftPadding: 36
            placeholderText: qsTr("Search hosts, tabs, snippets, pages…")
            placeholderTextColor: Theme.textFaint
            color: Theme.text
            font.pixelSize: 14
            background: Rectangle {
                radius: 8; color: Theme.surface2; border.color: Theme.border
                IconLabel { anchors.left: parent.left; anchors.leftMargin: 12; anchors.verticalCenter: parent.verticalCenter; glyph: Theme.iconSearch; size: 14; color: Theme.textDim }
            }
            onTextChanged: root.rebuild()
            Keys.onDownPressed: { root.sel = Math.min(root.results.length - 1, root.sel + 1); list.positionViewAtIndex(root.sel, ListView.Contain) }
            Keys.onUpPressed: { root.sel = Math.max(0, root.sel - 1); list.positionViewAtIndex(root.sel, ListView.Contain) }
            Keys.onReturnPressed: root.activate(root.results[root.sel])
            Keys.onEnterPressed: root.activate(root.results[root.sel])
            Keys.onEscapePressed: root.close()
        }
        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.results
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            delegate: AbstractButton {
                id: rowBtn
                width: list.width
                height: 44
                hoverEnabled: true
                onHoveredChanged: if (hovered) root.sel = index
                onClicked: root.activate(modelData)
                background: Rectangle { radius: 8; color: index === root.sel ? Theme.surfaceHover : "transparent" }
                contentItem: RowLayout {
                    spacing: 10
                    Rectangle {
                        Layout.leftMargin: 6
                        width: 28; height: 28; radius: 7
                        color: modelData.kind === "tab" ? Theme.accentDim : Theme.surface2
                        IconLabel { anchors.centerIn: parent; glyph: modelData.glyph; size: 13; color: modelData.kind === "tab" ? Theme.accent : Theme.textDim }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 1
                        Text { Layout.fillWidth: true; text: modelData.title; color: Theme.text; font.pixelSize: 13; elide: Text.ElideRight }
                        Text { Layout.fillWidth: true; text: modelData.sub; color: Theme.textFaint; font.pixelSize: 11; elide: Text.ElideRight }
                    }
                    Text { visible: index === root.sel; text: "↵"; color: Theme.textDim; font.pixelSize: 13; Layout.rightMargin: 10 }
                }
            }
            Text {
                anchors.centerIn: parent
                visible: root.results.length === 0
                text: qsTr("Nothing found")
                color: Theme.textFaint
            }
        }
        Text {
            Layout.fillWidth: true
            text: qsTr("↑↓ choose · Enter open · Esc close · works with part of a name or letters in order")
            color: Theme.textFaint
            font.pixelSize: 10
            elide: Text.ElideRight
        }
    }
}
