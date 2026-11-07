import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ShellWarden

// Compare one config file between two servers: red = only on the first,
// green = only on the second.
Popup {
    id: root
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(980, (parent ? parent.width : 980) - 32)
    height: Math.min(720, (parent ? parent.height : 720) - 32)
    modal: true
    focus: true
    padding: 20
    closePolicy: Popup.CloseOnEscape
    Overlay.modal: Rectangle { color: "#a0070910" }
    background: Rectangle { radius: 14; color: Theme.surface; border.color: Theme.border }

    readonly property var runner: App.diffRunner
    property string error: ""
    property var rows: []
    property int added: 0
    property int removed: 0
    property bool onlyChanges: true
    property bool waiting: false

    readonly property var hostList: {
        App.hosts.count
        const out = []
        for (let i = 0; i < App.hosts.count; ++i) {
            const h = App.hosts.get(i)
            if (h.type !== "serial" && h.type !== "telnet") out.push({ id: h.id, label: h.label })
        }
        return out
    }
    function openFor(hostId) {
        error = ""; rows = []; waiting = false
        const i = hostList.findIndex(h => h.id === hostId)
        if (i >= 0) hostA.currentIndex = i
        open()
    }

    // line diff (longest common subsequence); big files fall back to line-by-line
    function diff(a, b) {
        const n = a.length, m = b.length, out = []
        if (n * m > 4000000) {
            for (let i = 0; i < Math.max(n, m); ++i) {
                if (a[i] === b[i]) out.push({ t: 0, a: i + 1, b: i + 1, s: a[i] })
                else { if (i < n) out.push({ t: -1, a: i + 1, b: 0, s: a[i] }); if (i < m) out.push({ t: 1, a: 0, b: i + 1, s: b[i] }) }
            }
            return out
        }
        const w = m + 1, L = new Uint32Array((n + 1) * w)
        for (let i = n - 1; i >= 0; --i)
            for (let j = m - 1; j >= 0; --j)
                L[i * w + j] = a[i] === b[j] ? L[(i + 1) * w + j + 1] + 1 : Math.max(L[(i + 1) * w + j], L[i * w + j + 1])
        let i = 0, j = 0
        while (i < n && j < m) {
            if (a[i] === b[j]) { out.push({ t: 0, a: i + 1, b: j + 1, s: a[i] }); ++i; ++j }
            else if (L[(i + 1) * w + j] >= L[i * w + j + 1]) { out.push({ t: -1, a: i + 1, b: 0, s: a[i] }); ++i }
            else { out.push({ t: 1, a: 0, b: j + 1, s: b[j] }); ++j }
        }
        for (; i < n; ++i) out.push({ t: -1, a: i + 1, b: 0, s: a[i] })
        for (; j < m; ++j) out.push({ t: 1, a: 0, b: j + 1, s: b[j] })
        return out
    }
    function build() {
        const r = runner.results
        if (r.length < 2) return
        for (const x of r) if (x.state !== "ok") { error = x.label + ": " + (x.error || qsTr("could not read the file")); return }
        const split = (t) => { const l = t.replace(/\r/g, "").split("\n"); if (l.length && l[l.length - 1] === "") l.pop(); return l }
        const d = diff(split(r[0].output), split(r[1].output))
        let ad = 0, rm = 0
        for (const x of d) { if (x.t > 0) ++ad; else if (x.t < 0) ++rm }
        added = ad; removed = rm
        // keep 3 lines of context around changes when "only changes" is on
        const keep = new Array(d.length).fill(!onlyChanges)
        if (onlyChanges)
            for (let k = 0; k < d.length; ++k)
                if (d[k].t !== 0) for (let c = Math.max(0, k - 3); c <= Math.min(d.length - 1, k + 3); ++c) keep[c] = true
        const out = []
        let skipped = 0
        for (let k = 0; k < d.length; ++k) {
            if (keep[k]) { if (skipped) { out.push({ t: 2, s: qsTr("… %n same line(s) …", "", skipped) }); skipped = 0 } out.push(d[k]) }
            else ++skipped
        }
        if (skipped) out.push({ t: 2, s: qsTr("… %n same line(s) …", "", skipped) })
        rows = out
    }
    onOnlyChangesChanged: if (rows.length || added || removed) build()
    Connections {
        target: root.runner
        function onFinished() { if (root.waiting) { root.waiting = false; root.build() } }
    }

    contentItem: ColumnLayout {
        spacing: 12
        RowLayout {
            Layout.fillWidth: true
            IconLabel { glyph: Theme.iconSplit; size: 18; color: Theme.accent }
            Text { Layout.fillWidth: true; text: qsTr("Compare a file on two servers"); color: Theme.text; font.pixelSize: 18; font.weight: Font.DemiBold }
            AppButton { glyph: Theme.iconClose; variant: "ghost"; compact: true; onClicked: root.close() }
        }
        GridLayout {
            Layout.fillWidth: true
            columns: root.width > 760 ? 2 : 1
            columnSpacing: 12
            rowSpacing: 8
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 5
                Text { text: qsTr("First server"); color: "#ff7a8c"; font.pixelSize: 12; font.weight: Font.Medium }
                AppCombo { id: hostA; Layout.fillWidth: true; model: root.hostList.map(h => h.label) }
                AppField { id: pathA; Layout.fillWidth: true; label: qsTr("File"); mono: true; text: "/etc/nginx/nginx.conf" }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 5
                Text { text: qsTr("Second server"); color: "#6be3a2"; font.pixelSize: 12; font.weight: Font.Medium }
                AppCombo { id: hostB; Layout.fillWidth: true; model: root.hostList.map(h => h.label); currentIndex: Math.min(1, count - 1) }
                AppField { id: pathB; Layout.fillWidth: true; label: qsTr("File (empty = same path)"); mono: true; placeholder: pathA.text }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            AppButton {
                text: qsTr("Compare"); variant: "primary"; glyph: Theme.iconSplit
                enabled: !root.runner.running && root.hostList.length > 0
                onClicked: {
                    root.rows = []; root.added = 0; root.removed = 0
                    root.error = App.compareFiles(root.hostList[hostA.currentIndex].id, pathA.text,
                                                  root.hostList[hostB.currentIndex].id, pathB.text.trim() || pathA.text)
                    root.waiting = root.error === ""
                }
            }
            BusyIndicator { visible: root.runner.running; running: visible; implicitWidth: 22; implicitHeight: 22 }
            Text {
                visible: !root.runner.running && root.error === "" && (root.rows.length > 0 || root.added + root.removed > 0)
                text: root.added + root.removed === 0 ? qsTr("The files are identical") : qsTr("−%1 lines · +%2 lines").arg(root.removed).arg(root.added)
                color: root.added + root.removed === 0 ? Theme.ok : Theme.text
                font.pixelSize: 13; font.weight: Font.DemiBold
            }
            Text { visible: root.error !== ""; Layout.fillWidth: true; text: root.error; color: Theme.danger; font.pixelSize: 12; wrapMode: Text.WordWrap }
            Item { Layout.fillWidth: true; visible: root.error === "" }
            Text { text: qsTr("Only changes"); color: Theme.textDim; font.pixelSize: 12 }
            AppSwitch { checked: root.onlyChanges; onToggled: root.onlyChanges = checked }
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: 10
            color: Theme.terminalBg
            border.color: Theme.border
            clip: true
            ListView {
                id: diffList
                anchors.fill: parent
                anchors.margins: 1
                clip: true
                model: root.rows
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}
                ScrollBar.horizontal: ScrollBar {}
                contentWidth: Math.max(width, 1600)
                flickableDirection: Flickable.AutoFlickDirection
                delegate: Rectangle {
                    width: diffList.contentWidth
                    height: 20
                    color: modelData.t < 0 ? "#33ff5c72" : modelData.t > 0 ? "#334fd18b" : "transparent"
                    Row {
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 0
                        Text { width: 46; horizontalAlignment: Text.AlignRight; text: modelData.a || ""; color: Theme.textFaint; font.family: Theme.monoFont; font.pixelSize: 11 }
                        Text { width: 46; horizontalAlignment: Text.AlignRight; text: modelData.b || ""; color: Theme.textFaint; font.family: Theme.monoFont; font.pixelSize: 11 }
                        Text {
                            leftPadding: 10
                            text: (modelData.t < 0 ? "− " : modelData.t > 0 ? "+ " : modelData.t === 2 ? "" : "  ") + modelData.s
                            color: modelData.t < 0 ? "#ff9aa8" : modelData.t > 0 ? "#8ef0b8" : modelData.t === 2 ? Theme.textFaint : Theme.text
                            font.family: Theme.monoFont
                            font.pixelSize: 12
                            font.italic: modelData.t === 2
                            textFormat: Text.PlainText
                        }
                    }
                }
            }
            Text {
                anchors.centerIn: parent
                visible: root.rows.length === 0 && !root.runner.running
                width: parent.width - 40
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                color: Theme.textFaint
                text: qsTr("Choose two servers and a file, e.g. /etc/nginx/nginx.conf, /etc/ssh/sshd_config or /etc/hosts. The file is only read.")
            }
        }
    }
}
