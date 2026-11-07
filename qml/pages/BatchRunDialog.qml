import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ShellWarden

// Run one command (or several lines) on many hosts at once and see, per host,
// whether it worked. No tabs are opened: each host gets a short connection.
Popup {
    id: root
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(860, (parent ? parent.width : 860) - 32)
    height: Math.min(700, (parent ? parent.height : 700) - 32)
    modal: true
    focus: true
    padding: 20
    closePolicy: Popup.CloseOnEscape
    Overlay.modal: Rectangle { color: "#a0070910" }
    background: Rectangle { radius: 14; color: Theme.surface; border.color: Theme.border }

    property var picked: ({})          // hostId -> true
    property string filter: ""
    property string group: "*"         // "*" = all
    property bool showResults: false
    property string error: ""
    property int expanded: -1
    readonly property bool enterRuns: App.settings.batchRunKey === "enter"
    readonly property var batch: App.batch

    function openWith(command, hostIds) {
        cmd.text = command || cmd.text
        const p = {}
        for (const id of (hostIds || [])) p[id] = true
        picked = p
        showResults = false
        error = ""
        open()
        cmd.forceActiveFocus()
    }
    readonly property var hosts: {
        App.hosts.count
        const out = []
        for (let i = 0; i < App.hosts.count; ++i) {
            const h = App.hosts.get(i)
            if (h.type === "serial" || h.type === "telnet") continue
            out.push({ id: h.id, label: h.label, group: h.group || "", addr: (h.username ? h.username + "@" : "") + h.hostname,
                       protected: App.hostIsProtected(h.id), color: h.color || "" })
        }
        return out
    }
    readonly property var groups: {
        const g = []
        for (const h of hosts) if (h.group && g.indexOf(h.group) < 0) g.push(h.group)
        return g.sort()
    }
    readonly property var visibleHosts: hosts.filter(h => (group === "*" || h.group === group)
        && (filter === "" || (h.label + " " + h.addr + " " + h.group).toLowerCase().indexOf(filter.toLowerCase()) >= 0))
    readonly property int pickedCount: { let n = 0; for (const k in picked) if (picked[k]) ++n; return n }
    function setAll(on) {
        const p = Object.assign({}, picked)
        for (const h of visibleHosts) p[h.id] = on
        picked = p
    }
    function tryRun() {
        if (cmd.text.trim() === "") { error = qsTr("First type the command (step 1)."); return }
        if (pickedCount === 0) { error = qsTr("Now tick at least one server (step 2)."); return }
        run()
    }
    // Ctrl+Enter works anywhere in the window, not only in the command box
    Shortcut {
        sequences: ["Ctrl+Return", "Ctrl+Enter"]
        enabled: root.opened && !root.showResults
        onActivated: root.tryRun()
    }
    readonly property bool needMaster: hosts.some(h => picked[h.id] && h.protected)
    function run(ids) {
        const list = ids || hosts.filter(h => picked[h.id]).map(h => h.id)
        error = secrets.prepare()
        if (error !== "") return
        error = App.runOnHosts(list, cmd.text, needMaster ? secrets.masterPassword : "")
        secrets.clear()
        if (error === "") { showResults = true; expanded = -1 }
    }

    // numbered step header: "1  What to run" + one plain-language hint
    component Step: RowLayout {
        id: st
        property int n: 1
        property string title: ""
        property string hint: ""
        property bool done: false
        spacing: 10
        Rectangle {
            Layout.alignment: Qt.AlignTop
            width: 22; height: 22; radius: 11
            color: st.done ? Theme.accent : Theme.surface2
            border.color: st.done ? Theme.accent : Theme.border
            Text { textFormat: Text.PlainText; anchors.centerIn: parent; visible: !st.done; text: st.n; color: Theme.text; font.pixelSize: 11; font.weight: Font.Bold }
            IconLabel { anchors.centerIn: parent; visible: st.done; glyph: Theme.iconCheck; size: 12; color: Theme.bg }
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 1
            Text { textFormat: Text.PlainText; text: st.title; color: Theme.text; font.pixelSize: 13; font.weight: Font.DemiBold }
            Text { textFormat: Text.PlainText; Layout.fillWidth: true; visible: st.hint !== ""; text: st.hint; color: Theme.textFaint; font.pixelSize: 11; wrapMode: Text.WordWrap }
        }
    }

    component Chip: AbstractButton {
        id: chip
        property bool on: false
        implicitHeight: 28
        implicitWidth: chipText.implicitWidth + 22
        hoverEnabled: true
        background: Rectangle { radius: 14; color: chip.on ? Theme.accentDim : (chip.hovered ? Theme.surface2 : "transparent"); border.color: chip.on ? Theme.accent : Theme.border }
        contentItem: Text { textFormat: Text.PlainText; id: chipText; text: chip.text; color: chip.on ? Theme.text : Theme.textDim; font.pixelSize: 12
                            horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
    }

    contentItem: ColumnLayout {
        spacing: 12
        RowLayout {
            Layout.fillWidth: true
            IconLabel { glyph: Theme.iconTerminal; size: 18; color: Theme.accent; Layout.alignment: Qt.AlignTop; Layout.topMargin: 4 }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 1
                Text { textFormat: Text.PlainText; text: qsTr("Run on several hosts"); color: Theme.text; font.pixelSize: 18; font.weight: Font.DemiBold }
                Text {
                    textFormat: Text.PlainText
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: qsTr("Type a command once, tick the servers, press Run. ShellWarden connects to each one by itself and shows where it worked and where it failed.")
                    color: Theme.textDim; font.pixelSize: 12
                }
            }
            AppButton { glyph: Theme.iconClose; variant: "ghost"; compact: true; onClicked: root.close() }
        }

        // =============== setup ===============
        ColumnLayout {
            visible: !root.showResults
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 10

            Step {
                Layout.fillWidth: true
                n: 1
                done: cmd.text.trim() !== ""
                title: qsTr("What to run")
                hint: qsTr("Any shell command, e.g. \"uptime\" or \"df -h\". Several lines run one after another.")
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 96
                radius: 8
                color: Theme.bg
                border.color: cmd.activeFocus ? Theme.accent : Theme.border
                ScrollView {
                    anchors.fill: parent
                    anchors.margins: 1
                    TextArea {
                        textFormat: TextArea.PlainText
                        id: cmd
                        color: Theme.text
                        font.family: Theme.monoFont
                        font.pixelSize: 13
                        wrapMode: TextArea.Wrap
                        selectByMouse: true
                        placeholderText: qsTr("uptime")
                        placeholderTextColor: Theme.textFaint
                        background: null
                        // Ctrl+Enter (or Enter, if chosen) runs; the other one makes a new line
                        Keys.onPressed: (e) => {
                            if (e.key !== Qt.Key_Return && e.key !== Qt.Key_Enter) return
                            const ctrl = (e.modifiers & Qt.ControlModifier) !== 0
                            const shift = (e.modifiers & Qt.ShiftModifier) !== 0
                            const runIt = root.enterRuns ? (!shift && !ctrl) || ctrl : ctrl
                            if (!runIt) return
                            e.accepted = true
                            root.tryRun()
                        }
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                // how to run from the keyboard
                    Text { textFormat: Text.PlainText; text: qsTr("Run with:"); color: Theme.textDim; font.pixelSize: 11 }
                    Repeater {
                        model: [["ctrl", "Ctrl+Enter"], ["enter", "Enter"]]
                        delegate: Chip {
                            text: modelData[1]
                            on: (App.settings.batchRunKey || "ctrl") === modelData[0]
                            onClicked: App.setSetting("batchRunKey", modelData[0])
                        }
                    }
                    Text {
                        textFormat: Text.PlainText
                        text: root.enterRuns ? qsTr("Shift+Enter — new line") : qsTr("Enter — new line")
                        color: Theme.textFaint; font.pixelSize: 11
                    }
                    Item { Layout.fillWidth: true }
                AppCombo {
                    id: snipBox
                    Layout.preferredWidth: 260
                    visible: App.snippets.count > 0
                    model: {
                        const a = [qsTr("Insert a snippet…")]
                        for (let i = 0; i < App.snippets.count; ++i) a.push(App.snippets.get(i).name)
                        return a
                    }
                    onActivated: (i) => { if (i > 0) cmd.text = App.snippets.get(i - 1).command; currentIndex = 0 }
                }
            }
            Text {
                textFormat: Text.PlainText
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.textFaint
                font.pixelSize: 11
                text: qsTr("Runs without a terminal: commands that ask questions (sudo password, y/n) will wait and time out. Use -y and non-interactive options.")
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 4
                Step {
                    Layout.fillWidth: true
                    n: 2
                    done: root.pickedCount > 0
                    title: root.pickedCount > 0 ? qsTr("Where to run — %n server(s) chosen", "", root.pickedCount) : qsTr("Where to run")
                    hint: qsTr("Click servers to tick them, or click a group to tick the whole group.")
                }
                TextField {
                    id: search
                    Layout.preferredWidth: 200
                    implicitHeight: 30
                    placeholderText: qsTr("Search")
                    placeholderTextColor: Theme.textFaint
                    color: Theme.text
                    font.pixelSize: 12
                    onTextChanged: root.filter = text
                    background: Rectangle { radius: 6; color: Theme.surface2; border.color: search.activeFocus ? Theme.accent : Theme.border }
                }
                AppButton { text: qsTr("All"); compact: true; variant: "ghost"; onClicked: root.setAll(true) }
                AppButton { text: qsTr("None"); compact: true; variant: "ghost"; onClicked: root.setAll(false) }
            }
            // groups: one click selects a whole group
            Flow {
                Layout.fillWidth: true
                spacing: 6
                visible: root.groups.length > 0
                Chip { text: qsTr("All hosts"); on: root.group === "*"; onClicked: root.group = "*" }
                Repeater {
                    model: root.groups
                    delegate: Chip {
                        text: modelData
                        on: root.group === modelData
                        onClicked: { root.group = modelData; root.setAll(true) }
                    }
                }
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: 10
                color: Theme.bg
                border.color: Theme.border
                clip: true
                GridView {
                    id: hostGrid
                    anchors.fill: parent
                    anchors.margins: 6
                    clip: true
                    cellWidth: Math.floor(width / Math.max(1, Math.floor(width / 260)))
                    cellHeight: 46
                    model: root.visibleHosts
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {}
                    delegate: AbstractButton {
                        id: hb
                        width: hostGrid.cellWidth - 6
                        height: 40
                        hoverEnabled: true
                        readonly property bool on: !!root.picked[modelData.id]
                        onClicked: { const p = Object.assign({}, root.picked); p[modelData.id] = !hb.on; root.picked = p }
                        background: Rectangle { radius: 8; color: hb.on ? Theme.accentDim : (hb.hovered ? Theme.surface2 : "transparent")
                                                border.color: hb.on ? Theme.accent : "transparent" }
                        contentItem: RowLayout {
                            spacing: 8
                            Rectangle {
                                Layout.leftMargin: 8
                                width: 16; height: 16; radius: 4
                                color: hb.on ? Theme.accent : "transparent"
                                border.color: hb.on ? Theme.accent : Theme.textFaint
                                IconLabel { anchors.centerIn: parent; visible: hb.on; glyph: Theme.iconCheck; size: 11; color: Theme.bg }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 0
                                Text { textFormat: Text.PlainText; Layout.fillWidth: true; text: modelData.label; color: Theme.text; font.pixelSize: 12; elide: Text.ElideRight }
                                Text { textFormat: Text.PlainText; Layout.fillWidth: true; text: modelData.addr + (modelData.group ? " · " + modelData.group : ""); color: Theme.textFaint; font.pixelSize: 10; font.family: Theme.monoFont; elide: Text.ElideRight }
                            }
                            IconLabel {
                                visible: modelData.protected
                                Layout.rightMargin: 8
                                glyph: Theme.iconShield; size: 12; color: Theme.warn
                                HoverHandler { id: ph }
                                ToolTip.visible: ph.hovered
                                ToolTip.text: qsTr("Asks for the master password: you enter it once for the whole run")
                            }
                        }
                    }
                }
                Text { textFormat: Text.PlainText; anchors.centerIn: parent; visible: root.visibleHosts.length === 0; text: qsTr("No hosts"); color: Theme.textFaint }
            }
            Text { textFormat: Text.PlainText; visible: root.error !== ""; text: root.error; color: Theme.danger; font.pixelSize: 12 }
            RowLayout {
                Layout.fillWidth: true
                Step {
                    Layout.fillWidth: true
                    n: 3
                    title: qsTr("Run")
                    visible: !secrets.needed
                    hint: cmd.text.trim() === "" ? qsTr("First type the command (step 1).")
                          : root.pickedCount === 0 ? qsTr("Now tick at least one server (step 2).")
                          : qsTr("Each server gets a short connection of its own; no tabs are opened.")
                }
                // "ask the master password" servers: one master password for the whole run
                BatchSecrets {
                    id: secrets
                    Layout.fillWidth: true
                    hostIds: root.hosts.filter(h => root.picked[h.id]).map(h => h.id)
                    needMaster: root.needMaster
                    onSubmit: root.tryRun()
                }
                AppButton { text: qsTr("Close"); variant: "ghost"; onClicked: root.close() }
                AppButton {
                    text: qsTr("Run on %n host(s)", "", root.pickedCount) + "  ·  " + (root.enterRuns ? "Enter" : "Ctrl+Enter"); variant: "primary"; glyph: Theme.iconTerminal
                    enabled: root.pickedCount > 0 && cmd.text.trim() !== ""
                    onClicked: root.run()
                }
            }
        }

        // =============== results ===============
        ColumnLayout {
            visible: root.showResults
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 10

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 34
                radius: 8
                color: Theme.bg
                border.color: Theme.border
                Text {
                    textFormat: Text.PlainText
                    anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 10
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                    text: "$ " + cmd.text.split("\n")[0] + (cmd.text.indexOf("\n") >= 0 ? "  …" : "")
                    color: Theme.textDim; font.family: Theme.monoFont; font.pixelSize: 12
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 14
                BusyIndicator { visible: root.batch.running; running: visible; implicitWidth: 22; implicitHeight: 22 }
                Text {
                    textFormat: Text.PlainText
                    text: root.batch.running ? qsTr("Running… %1 of %2 done").arg(root.batch.doneCount).arg(root.batch.results.length)
                                             : qsTr("Finished")
                    color: Theme.text; font.pixelSize: 13; font.weight: Font.DemiBold
                }
                Text { textFormat: Text.PlainText; text: "✓ " + root.batch.okCount; color: Theme.ok; font.pixelSize: 13; font.weight: Font.DemiBold }
                Text { textFormat: Text.PlainText; text: "✗ " + root.batch.failCount; color: root.batch.failCount > 0 ? Theme.danger : Theme.textFaint; font.pixelSize: 13; font.weight: Font.DemiBold }
                Item { Layout.fillWidth: true }
                Text { textFormat: Text.PlainText; text: qsTr("Click a row to see its output"); color: Theme.textFaint; font.pixelSize: 11 }
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: 10
                color: Theme.bg
                border.color: Theme.border
                clip: true
                ListView {
                    id: resList
                    anchors.fill: parent
                    anchors.margins: 1
                    clip: true
                    model: root.batch.results
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {}
                    delegate: Rectangle {
                        id: row
                        width: resList.width
                        readonly property bool open: root.expanded === index
                        readonly property color tint: modelData.state === "ok" ? Theme.ok
                                                    : modelData.state === "running" ? Theme.accent
                                                    : modelData.state === "queued" ? Theme.textFaint
                                                    : modelData.state === "skipped" ? Theme.warn : Theme.danger
                        implicitHeight: head.height + (open ? outBox.height + 10 : 0)
                        color: index % 2 ? "transparent" : Theme.tint(Theme.surface2, "40")
                        Item {
                            id: head
                            width: parent.width
                            height: 44
                            MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.expanded = row.open ? -1 : index }
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 12; anchors.rightMargin: 12
                                spacing: 10
                                Rectangle {
                                    width: 22; height: 22; radius: 11
                                    color: Theme.tint(row.tint, "2e")
                                    IconLabel {
                                        anchors.centerIn: parent
                                        visible: modelData.state !== "running" && modelData.state !== "queued"
                                        glyph: modelData.state === "ok" ? Theme.iconCheck : modelData.state === "skipped" ? Theme.iconWarning : Theme.iconClose
                                        size: 12; color: row.tint
                                    }
                                    BusyIndicator { anchors.centerIn: parent; visible: modelData.state === "running"; running: visible; width: 18; height: 18 }
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 0
                                    Text { textFormat: Text.PlainText; Layout.fillWidth: true; text: modelData.label; color: Theme.text; font.pixelSize: 13; elide: Text.ElideRight }
                                    Text {
                                        textFormat: Text.PlainText
                                        Layout.fillWidth: true
                                        elide: Text.ElideRight
                                        font.pixelSize: 11
                                        color: modelData.state === "ok" ? Theme.textFaint : row.tint
                                        text: modelData.state === "ok" ? (modelData.output.trim().split("\n").pop() || qsTr("done, no output"))
                                              : modelData.state === "queued" ? qsTr("waiting…")
                                              : modelData.state === "running" ? qsTr("running…")
                                              : modelData.error
                                    }
                                }
                                Text {
                                    textFormat: Text.PlainText
                                    visible: modelData.state === "ok" || modelData.state === "fail"
                                    text: (modelData.exitCode >= 0 ? qsTr("code %1").arg(modelData.exitCode) + " · " : "") + (modelData.ms / 1000).toFixed(1) + " s"
                                    color: Theme.textFaint; font.pixelSize: 11; font.family: Theme.monoFont
                                }
                                IconLabel { glyph: row.open ? Theme.iconArrowUp : Theme.iconArrowDown; size: 12; color: Theme.textDim }
                            }
                        }
                        Rectangle {
                            id: outBox
                            visible: row.open
                            anchors.top: head.bottom
                            anchors.left: parent.left; anchors.right: parent.right
                            anchors.leftMargin: 12; anchors.rightMargin: 12
                            height: Math.min(260, outText.implicitHeight + 16)
                            radius: 6
                            color: Theme.terminalBg
                            border.color: Theme.border
                            ScrollView {
                                anchors.fill: parent
                                anchors.margins: 6
                                TextArea {
                                    textFormat: TextArea.PlainText
                                    id: outText
                                    readOnly: true
                                    selectByMouse: true
                                    wrapMode: TextArea.WrapAnywhere
                                    color: Theme.text
                                    font.family: Theme.monoFont
                                    font.pixelSize: 12
                                    background: null
                                    text: (modelData.output || "") + (modelData.error && modelData.state !== "ok" ? (modelData.output ? "\n" : "") + "⚠ " + modelData.error : "")
                                          || qsTr("(no output)")
                                }
                            }
                            AppButton {
                                anchors.top: parent.top; anchors.right: parent.right; anchors.margins: 4
                                glyph: Theme.iconCopy; compact: true; variant: "ghost"
                                onClicked: App.copyText(modelData.output)
                                ToolTip.visible: hovered
                                ToolTip.text: qsTr("Copy output")
                            }
                        }
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                AppButton { text: qsTr("Back"); glyph: Theme.iconBack; variant: "ghost"; enabled: !root.batch.running; onClicked: root.showResults = false }
                Item { Layout.fillWidth: true }
                AppButton { visible: root.batch.running; text: qsTr("Stop"); variant: "danger"; onClicked: root.batch.cancel() }
                AppButton {
                    visible: !root.batch.running && root.batch.failCount > 0
                    text: qsTr("Run again on failed (%1)").arg(root.batch.failCount)
                    onClicked: root.run(root.batch.results.filter(r => r.state !== "ok").map(r => r.hostId))
                }
                AppButton { visible: !root.batch.running; text: qsTr("Run again"); glyph: Theme.iconRefresh; variant: "primary"; onClicked: root.run() }
            }
        }
    }
}
