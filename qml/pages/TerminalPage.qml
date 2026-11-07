import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import ShellWarden

// One session view: route/IP bar, toolbar, terminal, side panels (SFTP,
// tunnels) and the connection overlays (host key, password prompt, …).
Rectangle {
    id: root
    property TerminalSession session: null
    property bool inWindow: false          // shown in a detached window
    property bool highlighted: false       // active pane in split view
    property string panel: ""              // "" | "sftp" | "tunnels"
    signal closeRequested()
    signal detachRequested()
    signal dockRequested()
    signal focusRequested()
    property bool primary: true      // false for the right pane of a split: don't steal focus

    color: term.backgroundColor

    // TerminalSession.State
    readonly property int sPreparing: 0
    readonly property int sConnecting: 1
    readonly property int sHostKey: 2
    readonly property int sSecret: 3
    readonly property int sConnected: 4
    readonly property int sClosed: 5
    readonly property bool isSsh: session && session.kind === "ssh"

    readonly property bool ipConfirmed: session && session.seenIp !== "" && session.expectedIp !== ""
                                        && session.seenIp === session.expectedIp
    readonly property bool ipMismatch: session && session.seenIp !== "" && session.expectedIp !== ""
                                       && session.seenIp !== session.expectedIp && !session.viaJump
    readonly property int activeTunnels: {
        if (!session) return 0
        let n = 0
        for (const f of session.forwards) if (f.state === "active") n++
        return n
    }

    onVisibleChanged: if (visible && primary && session && session.state === sConnected) term.forceActiveFocus()

    function openSearch() {
        searchBar.visible = true
        searchField.forceActiveFocus()
        searchField.selectAll()
        if (searchField.text !== "") term.find(searchField.text, caseBox.checked)
    }
    function closeSearch() {
        searchBar.visible = false
        term.clearFind()
        term.forceActiveFocus()
    }
    Shortcut {
        sequence: "Ctrl+Shift+F"
        enabled: root.visible && (term.activeFocus || searchField.activeFocus)
        onActivated: root.openSearch()
    }
    Shortcut {
        sequence: "Ctrl+Shift+H"
        enabled: root.visible && term.activeFocus && root.session && root.session.hostId !== ""
        onActivated: historyPopup.show()
    }

    // Commands run on this host earlier: search, put one into the prompt or run it.
    // Several lines (or a very long text) on the clipboard: every line break is an
    // Enter, so show what will run before it runs.
    Modal {
        id: pasteAsk
        property string text: ""
        property int lines: 0
        title: qsTr("Paste %n lines?", "", pasteAsk.lines)
        subtitle: qsTr("Each line break works like Enter: the lines run one after another as soon as they arrive. Check the text first — what you copied from a web page is not always what you saw.")
        width: Math.min(640, (parent ? parent.width : 640) - 40)
        function ask(text, lines, preview) {
            pasteAsk.text = text
            pasteAsk.lines = lines
            pastePreview.text = preview + (text.length > 1500 ? "\n…" : "")
            pasteNoAsk.checked = false
            open()
        }
        onClosed: { pasteAsk.text = ""; pastePreview.text = ""; term.forceActiveFocus() }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(260, pastePreview.implicitHeight + 20)
            radius: 8
            color: Theme.bg
            border.color: Theme.border
            Flickable {
                anchors.fill: parent
                anchors.margins: 10
                clip: true
                contentHeight: pastePreview.implicitHeight
                contentWidth: width
                Text {
                    id: pastePreview
                    width: parent.width
                    textFormat: Text.PlainText
                    wrapMode: Text.WrapAnywhere
                    font.family: Theme.monoFont
                    font.pixelSize: 12
                    color: Theme.text
                }
            }
        }
        AppCheckBox { id: pasteNoAsk; text: qsTr("Do not ask again (Settings → Terminal)") }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            AppButton { text: qsTr("Cancel"); onClicked: pasteAsk.close() }
            AppButton {
                text: qsTr("Paste")
                variant: "primary"
                onClicked: {
                    if (pasteNoAsk.checked)
                        App.setSetting("pasteWarning", false)
                    const t = pasteAsk.text
                    pasteAsk.close()
                    term.pasteText(t)
                }
            }
        }
    }

    Modal {
        id: historyPopup
        property var all: []
        readonly property var shown: {
            const q = histSearch.text.trim().toLowerCase()
            return q === "" ? all : all.filter(c => c.toLowerCase().indexOf(q) >= 0)
        }
        property int current: 0
        title: qsTr("Command history · %1").arg(root.session ? root.session.label : "")
        subtitle: qsTr("Enter puts the command into the terminal, Ctrl+Enter runs it. A command typed with a space in front is not remembered.")
        width: Math.min(640, (parent ? parent.width : 640) - 40)
        function show() {
            all = App.commandHistory(root.session.hostId)
            histSearch.text = ""
            current = 0
            open()
            histSearch.field.forceActiveFocus()
        }
        function use(cmd, run) {
            close()
            root.session.sendText(cmd + (run ? "\r" : ""))
            term.forceActiveFocus()
        }
        onClosed: term.forceActiveFocus()

        AppField {
            id: histSearch
            Layout.fillWidth: true
            placeholder: qsTr("Search…")
            onTextChanged: historyPopup.current = 0
            field.Keys.onDownPressed: historyPopup.current = Math.min(historyPopup.current + 1, historyPopup.shown.length - 1)
            field.Keys.onUpPressed: historyPopup.current = Math.max(historyPopup.current - 1, 0)
            field.Keys.onReturnPressed: (e) => {
                if (historyPopup.shown.length > 0)
                    historyPopup.use(historyPopup.shown[historyPopup.current], (e.modifiers & Qt.ControlModifier) !== 0)
            }
        }
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 300
            radius: 8
            color: Theme.bg
            border.color: Theme.border
            ListView {
                id: histList
                anchors.fill: parent
                anchors.margins: 1
                clip: true
                model: historyPopup.shown
                currentIndex: historyPopup.current
                onCurrentIndexChanged: positionViewAtIndex(currentIndex, ListView.Contain)
                ScrollBar.vertical: ScrollBar {}
                delegate: Rectangle {
                    width: histList.width
                    height: 30
                    color: index === historyPopup.current ? Theme.surface2 : (hh.hovered ? Theme.surfaceHover : "transparent")
                    HoverHandler { id: hh }
                    TapHandler {
                        onTapped: historyPopup.current = index
                        onDoubleTapped: historyPopup.use(modelData, false)
                    }
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10; anchors.rightMargin: 6
                        spacing: 6
                        Text { textFormat: Text.PlainText; Layout.fillWidth: true; text: modelData; color: Theme.text; font.family: Theme.monoFont; font.pixelSize: 12; elide: Text.ElideRight }
                        AbstractButton {
                            visible: hh.hovered
                            width: 22; height: 22
                            onClicked: { App.forgetCommand(root.session.hostId, modelData); historyPopup.all = App.commandHistory(root.session.hostId) }
                            contentItem: Item { IconLabel { anchors.centerIn: parent; glyph: Theme.iconDelete; size: 12; color: Theme.textDim } }
                            ToolTip.visible: hovered; ToolTip.text: qsTr("Forget this command")
                        }
                    }
                }
            }
            Text {
                textFormat: Text.PlainText
                anchors.centerIn: parent
                visible: historyPopup.shown.length === 0
                width: parent.width - 40
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: historyPopup.all.length === 0 ? qsTr("No commands yet. Commands you run on this host will appear here.") : qsTr("Nothing found.")
                color: Theme.textFaint
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            AppButton {
                text: qsTr("Clear history"); variant: "ghost"; compact: true
                enabled: historyPopup.all.length > 0
                onClicked: { App.clearCommandHistory(root.session.hostId); historyPopup.all = [] }
            }
            Item { Layout.fillWidth: true }
            AppButton { text: qsTr("Close"); variant: "ghost"; onClicked: historyPopup.close() }
            AppButton {
                text: qsTr("Insert"); variant: "ghost"
                enabled: historyPopup.shown.length > 0
                onClicked: historyPopup.use(historyPopup.shown[historyPopup.current], false)
            }
            AppButton {
                text: qsTr("Run"); variant: "primary"; glyph: Theme.iconTerminal
                enabled: historyPopup.shown.length > 0
                onClicked: historyPopup.use(historyPopup.shown[historyPopup.current], true)
            }
        }
    }

    FileDialog {
        id: recordDlg
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("Text files (*.log *.txt)")]
        selectedFile: App.suggestedFile((root.session ? root.session.label.replace(/[^\w.-]+/g, "_") : "session")
                                        + "-" + new Date().toISOString().slice(0, 10) + ".log")
        onAccepted: {
            const e = root.session.startRecording(selectedFile)
            if (e !== "") console.warn(e)
        }
    }

    AppMenu {
        id: moreMenu
        AppMenuItem { text: qsTr("Find in the terminal…"); glyph: Theme.iconSearch; shortcutText: "Ctrl+Shift+F"; onTriggered: root.openSearch() }
        MenuSeparator {}
        AppMenuItem {
            glyph: Theme.iconRecord
            text: root.session && root.session.recording ? qsTr("Stop saving the session") : qsTr("Save the session to a file…")
            onTriggered: root.session.recording ? root.session.stopRecording() : recordDlg.open()
        }
        AppMenuItem {
            checkable: true
            glyph: Theme.iconBroadcast
            checked: root.session && root.session.broadcast
            text: qsTr("Broadcast: type into all tabs that have this on")
            onTriggered: root.session.broadcast = checked
        }
        MenuSeparator {}
        AppMenuItem { text: qsTr("Clear scrollback"); glyph: Theme.iconDelete; onTriggered: term.clearScrollback() }
    }

    component ToolBtn: AbstractButton {
        id: tb
        property string glyph: ""
        property bool on: false
        property string tip: ""
        property color tint: Theme.accent
        implicitWidth: label.length ? tbRow.implicitWidth + 18 : 32
        implicitHeight: 30
        property string label: ""
        hoverEnabled: true
        focusPolicy: Qt.NoFocus
        background: Rectangle {
            radius: 7
            color: tb.on ? Theme.tint(tb.tint, "26") : (tb.hovered ? Theme.surface2 : "transparent")
            border.width: tb.on ? 1 : 0
            border.color: Theme.tint(tb.tint, "80")
        }
        contentItem: Item {
            RowLayout {
                id: tbRow
                anchors.centerIn: parent
                spacing: 6
                IconLabel { glyph: tb.glyph; size: 15; color: tb.on ? tb.tint : (tb.hovered ? Theme.text : Theme.textDim) }
                Text { textFormat: Text.PlainText; visible: tb.label.length > 0; text: tb.label; color: tb.on ? Theme.text : Theme.textDim; font.pixelSize: 12 }
            }
        }
        ToolTip.visible: hovered && tip !== ""
        ToolTip.delay: 400
        ToolTip.text: tip
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // ------------------------------------------------------------
        // Route / IP bar + tools
        // ------------------------------------------------------------
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 42
            color: Theme.bg
            Rectangle {   // active-pane marker in split view
                visible: root.highlighted
                anchors.top: parent.top
                width: parent.width
                height: 2
                color: Theme.accent
            }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 8
                spacing: 8

                // route chip
                Rectangle {
                    radius: 7
                    implicitHeight: 26
                    implicitWidth: routeRow.implicitWidth + 16
                    Layout.maximumWidth: 260
                    color: !root.session ? Theme.surface
                           : root.session.viaProxy ? Theme.accentDim
                           : (root.session.killSwitch ? Theme.dangerDim : Theme.surface2)
                    RowLayout {
                        id: routeRow
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        spacing: 6
                        IconLabel {
                            glyph: root.session && root.session.kind === "telnet" ? Theme.iconWarning : root.session && root.session.kind === "local" ? Theme.iconTerminal : !root.isSsh ? Theme.iconSerial : (root.session && root.session.viaProxy ? Theme.iconShieldCheck
                                   : (root.session && root.session.viaJump ? Theme.iconHop : Theme.iconGlobe))
                            size: 14
                            color: root.session && root.session.viaProxy ? Theme.accent : Theme.textDim
                        }
                        Text {
                            textFormat: Text.PlainText
                            Layout.fillWidth: true
                            text: root.session ? root.session.routeText : ""
                            color: Theme.text
                            font.pixelSize: 11
                            elide: Text.ElideRight
                        }
                    }
                }

                Text {
                    textFormat: Text.PlainText
                    Layout.fillWidth: true
                    text: root.session ? root.session.endpoint : ""
                    color: Theme.textDim
                    font.pixelSize: 11
                    font.family: Theme.monoFont
                    elide: Text.ElideMiddle
                }

                // IP the server sees
                RowLayout {
                    spacing: 6
                    visible: root.isSsh && (root.session.seenIp !== "" || root.session.expectedIp !== "" || root.session.expectedLocation !== "")
                    Text {
                        textFormat: Text.PlainText
                        visible: root.width > 900
                        text: root.session && root.session.seenIp !== "" ? qsTr("Server sees you as") : qsTr("Expected exit IP")
                        color: Theme.textDim
                        font.pixelSize: 11
                    }
                    Rectangle {
                        radius: 7
                        implicitHeight: 26
                        implicitWidth: ipRow.implicitWidth + 16
                        color: root.ipMismatch ? Theme.warnDim : (root.ipConfirmed ? Theme.okDim : Theme.surface2)
                        border.width: 1
                        border.color: root.ipMismatch ? Theme.warn : (root.ipConfirmed ? Theme.ok : Theme.border)
                        RowLayout {
                            id: ipRow
                            anchors.centerIn: parent
                            spacing: 6
                            IconLabel {
                                visible: root.ipConfirmed || root.ipMismatch
                                glyph: root.ipConfirmed ? Theme.iconCheck : Theme.iconWarning
                                size: 12
                                color: root.ipConfirmed ? Theme.ok : Theme.warn
                            }
                            Text {
                                textFormat: Text.PlainText
                                text: !root.session ? ""
                                      : root.session.seenIp !== "" ? root.session.seenIp
                                      : (root.session.expectedIp !== "" ? root.session.expectedIp : root.session.expectedLocation)
                                color: Theme.text
                                font.pixelSize: 11
                                font.family: Theme.monoFont
                            }
                            // flag + country: known for the exit IP we checked; the server's
                            // view is the same address unless a jump host is in between
                            CountryBadge {
                                code: !root.session ? ""
                                      : (root.session.seenIp === "" || root.session.seenIp === root.session.expectedIp) ? root.session.expectedCountry : ""
                                showName: root.width > 760
                                textColor: Theme.text
                            }
                        }
                        HoverHandler { id: ipHover }
                        ToolTip.visible: ipHover.hovered
                        ToolTip.delay: 300
                        ToolTip.text: {
                            if (!root.session) return ""
                            let t = ""
                            if (root.session.seenIp !== "")
                                t += qsTr("Read from $SSH_CONNECTION on the server — this is the address in its auth log:\n%1 port %2 → server %3")
                                     .arg(root.session.seenIp).arg(root.session.seenPort).arg(root.session.serverIp)
                            if (root.session.expectedIp !== "")
                                t += (t ? "\n\n" : "") + qsTr("Route exit IP (checked before connecting): %1").arg(root.session.expectedIp)
                                     + (root.session.expectedLocation ? "\n" + root.session.expectedLocation : "")
                            if (root.session.viaJump)
                                t += "\n\n" + qsTr("Via jump host %1: the server sees the jump host's address.").arg(root.session.jumpLabel)
                            else if (root.ipMismatch)
                                t += "\n\n" + (root.session.viaProxy
                                      ? qsTr("The two differ: the proxy may use a different exit for SSH than for HTTPS.")
                                      : qsTr("The two differ: a VPN or another network route on this computer sends SSH differently than web traffic (for example IPv6 for one and IPv4 for the other)."))
                            return t
                        }
                    }
                    // how this session reaches the server, right next to the address it sees
                    Rectangle {
                        radius: 7
                        implicitHeight: 26
                        implicitWidth: viaRow.implicitWidth + 14
                        Layout.maximumWidth: 220
                        color: root.session && root.session.viaProxy ? Theme.accentDim : Theme.surface2
                        border.width: 1
                        border.color: root.session && root.session.viaProxy ? Theme.accent : Theme.border
                        RowLayout {
                            id: viaRow
                            anchors.fill: parent
                            anchors.leftMargin: 7
                            anchors.rightMargin: 7
                            spacing: 5
                            IconLabel {
                                glyph: root.session && root.session.viaProxy ? Theme.iconShieldCheck
                                       : root.session && root.session.viaJump ? Theme.iconHop : Theme.iconGlobe
                                size: 12
                                color: root.session && root.session.viaProxy ? Theme.accent : Theme.textDim
                            }
                            Text {
                                textFormat: Text.PlainText
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                font.pixelSize: 11
                                color: root.session && root.session.viaProxy ? Theme.accent : Theme.textDim
                                text: !root.session ? ""
                                      : root.session.viaProxy
                                        ? (root.width > 1100 ? qsTr("via proxy · %1").arg(App.proxy(root.session.proxyId).name || "") : qsTr("via proxy"))
                                      : root.session.viaJump ? qsTr("via jump host")
                                      : qsTr("direct")
                            }
                        }
                        HoverHandler { id: viaHover }
                        ToolTip.visible: viaHover.hovered
                        ToolTip.delay: 300
                        ToolTip.text: !root.session ? ""
                                      : root.session.viaProxy
                                        ? qsTr("This session goes through the proxy \"%1\" — the server sees the proxy's address, not yours.").arg(App.proxy(root.session.proxyId).name || "")
                                      : root.session.viaJump
                                        ? qsTr("This session goes through the jump host %1 — the server sees the jump host's address.").arg(root.session.jumpLabel)
                                      : qsTr("No proxy: this session connects directly, so the server sees your real IP (or your VPN's, if one is on).")
                    }
                }

                Rectangle { width: 1; height: 20; color: Theme.border }

                ToolBtn {
                    visible: root.isSsh
                    glyph: Theme.iconFolder
                    on: root.panel === "sftp"
                    enabled: root.session && root.session.state === root.sConnected
                    tip: qsTr("Files (SFTP)")
                    onClicked: root.panel = root.panel === "sftp" ? "" : "sftp"
                }
                ToolBtn {
                    id: tunnelsBtn
                    visible: root.isSsh
                    glyph: Theme.iconTunnel
                    on: root.panel === "tunnels"
                    tint: root.activeTunnels > 0 ? Theme.ok : Theme.accent
                    label: root.activeTunnels > 0 ? String(root.activeTunnels) : ""
                    enabled: root.session && root.session.state === root.sConnected
                    tip: qsTr("Tunnels (port forwarding)")
                    onClicked: root.panel = root.panel === "tunnels" ? "" : "tunnels"
                    Rectangle {   // pulse when traffic flows
                        visible: root.activeTunnels > 0 && root.session.activity
                        anchors.fill: parent
                        radius: 7
                        color: "transparent"
                        border.color: Theme.ok
                        border.width: 1
                        SequentialAnimation on opacity {
                            running: parent.visible
                            loops: Animation.Infinite
                            NumberAnimation { from: 0.9; to: 0.1; duration: 700 }
                        }
                    }
                }
                ToolBtn {
                    glyph: Theme.iconSnippet
                    enabled: root.session && root.session.state === root.sConnected && App.vaultUnlocked
                    tip: qsTr("Snippets")
                    onClicked: snippetMenu.popup()
                }
                ToolBtn {
                    visible: root.session && root.session.hostId !== "" && App.settings.commandHistory
                    glyph: Theme.iconClock
                    enabled: root.session && root.session.state === root.sConnected && App.vaultUnlocked
                    tip: qsTr("Command history (Ctrl+Shift+H)")
                    onClicked: historyPopup.show()
                }
                ToolBtn {
                    visible: root.session && root.session.recording
                    glyph: Theme.iconRecord
                    label: "REC"
                    on: true
                    tint: Theme.danger
                    tip: qsTr("Saving the session to %1 — click to stop").arg(root.session ? root.session.recordingPath : "")
                    onClicked: root.session.stopRecording()
                }
                ToolBtn {
                    visible: root.session && root.session.broadcast
                    glyph: Theme.iconBroadcast
                    on: true
                    tint: Theme.warn
                    tip: qsTr("Broadcast is on — click to turn it off")
                    onClicked: root.session.broadcast = false
                }
                ToolBtn {
                    glyph: Theme.iconSearch
                    on: searchBar.visible
                    tip: qsTr("Find in the terminal (Ctrl+Shift+F)")
                    onClicked: searchBar.visible ? root.closeSearch() : root.openSearch()
                }
                ToolBtn {
                    id: notesBtn
                    visible: root.session && root.session.hostId !== ""
                    glyph: Theme.iconFile
                    on: notesPop.opened
                    tint: root.hostHasNotes ? Theme.warn : Theme.accent
                    label: root.hostHasNotes ? "•" : ""
                    tip: root.hostHasNotes ? qsTr("Notes for this host") : qsTr("Add notes for this host")
                    onClicked: notesPop.opened ? notesPop.close() : notesPop.openNotes()
                }
                ToolBtn {
                    glyph: Theme.iconPalette
                    tip: qsTr("Colour theme for this tab")
                    onClicked: themeMenu.open()
                }
                ToolBtn {
                    id: moreBtn
                    glyph: Theme.iconMore
                    tip: qsTr("More")
                    onClicked: moreMenu.popup(moreBtn, moreBtn.width - moreMenu.width, moreBtn.height + 4)
                }
                ToolBtn {
                    glyph: root.inWindow ? Theme.iconDock : Theme.iconPopout
                    tip: root.inWindow ? qsTr("Back to the main window") : qsTr("Open in a separate window")
                    onClicked: root.inWindow ? root.dockRequested() : root.detachRequested()
                }
                ToolBtn {
                    visible: root.session && root.session.state !== root.sClosed
                    glyph: Theme.iconPower
                    tint: Theme.danger
                    tip: qsTr("Disconnect")
                    onClicked: root.session.disconnectSession()
                }
                ToolBtn {
                    visible: root.session && root.session.state === root.sClosed
                    glyph: Theme.iconRefresh
                    tip: qsTr("Reconnect")
                    onClicked: root.session.reconnect()
                }
            }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.border }
        }

        // ------------------------------------------------------------
        // Terminal + side panel
        // ------------------------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            TerminalItem {
                id: term
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.topMargin: 6
                anchors.rightMargin: 14
                anchors.bottomMargin: 4
                session: root.session
                fontFamily: App.settings.fontFamily
                fontSize: App.settings.fontSize
                copyOnSelect: App.settings.copyOnSelect
                rightClickPaste: App.settings.rightClickPaste
                confirmPaste: App.settings.pasteWarning !== false
                onPasteConfirmRequested: (text, lines, preview) => pasteAsk.ask(text, lines, preview)
                scrollbackLines: App.settings.scrollback
                highlightOutput: App.settings.highlightOutput
                focus: root.visible
                onActiveFocusChanged: if (activeFocus) root.focusRequested()
                onBell: {
                    if (App.settings.bell === "flash") bellFlash.restart()
                    else if (App.settings.bell === "sound") App.beep()
                }
            }

            ScrollBar {
                id: sb
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                orientation: Qt.Vertical
                policy: term.scrollbackSize > 0 ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
                readonly property real total: term.scrollbackSize + term.rows
                size: total > 0 ? term.rows / total : 1
                position: total > 0 ? (term.scrollbackSize - term.scrollOffset) / total : 0
                onPositionChanged: if (pressed) term.scrollTo(Math.round(term.scrollbackSize - position * total))
            }

            // ---------- find bar ----------
            Rectangle {
                id: searchBar
                visible: false
                z: 5
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.topMargin: 8
                anchors.rightMargin: 22
                width: Math.min(440, parent.width - 30)
                height: 40
                radius: 8
                color: Theme.surface
                border.color: Theme.accent
                Timer { id: findDelay; interval: 180; onTriggered: term.find(searchField.text, caseBox.checked) }
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10; anchors.rightMargin: 6
                    spacing: 4
                    IconLabel { glyph: Theme.iconSearch; size: 13; color: Theme.textDim }
                    TextField {
                        id: searchField
                        Layout.fillWidth: true
                        placeholderText: qsTr("Find…")
                        placeholderTextColor: Theme.textFaint
                        color: Theme.text
                        font.family: Theme.monoFont
                        background: Item {}
                        selectByMouse: true
                        onTextChanged: findDelay.restart()
                        Keys.onReturnPressed: (e) => term.findNext(!(e.modifiers & Qt.ShiftModifier))
                        Keys.onEnterPressed: (e) => term.findNext(!(e.modifiers & Qt.ShiftModifier))
                        Keys.onEscapePressed: root.closeSearch()
                    }
                    Text {
                        textFormat: Text.PlainText
                        text: searchField.text === "" ? "" : (term.matchCount === 0 ? qsTr("no matches") : (term.matchIndex + 1) + " / " + term.matchCount)
                        color: term.matchCount === 0 && searchField.text !== "" ? Theme.danger : Theme.textDim
                        font.pixelSize: 11
                    }
                    AbstractButton {
                        id: caseBox
                        checkable: true
                        implicitWidth: 28; implicitHeight: 26
                        onToggled: term.find(searchField.text, checked)
                        ToolTip.visible: hovered; ToolTip.text: qsTr("Match case")
                        hoverEnabled: true
                        background: Rectangle { radius: 5; color: caseBox.checked ? Theme.accentDim : (caseBox.hovered ? Theme.surface2 : "transparent") }
                        contentItem: Text { textFormat: Text.PlainText; text: "Aa"; color: caseBox.checked ? Theme.accent : Theme.textDim; font.pixelSize: 11; font.bold: true
                                            horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    }
                    ToolBtn { glyph: Theme.iconArrowUp; tip: qsTr("Older (Enter)"); onClicked: term.findNext(true) }
                    ToolBtn { glyph: Theme.iconArrowDown; tip: qsTr("Newer (Shift+Enter)"); onClicked: term.findNext(false) }
                    ToolBtn { glyph: Theme.iconClose; tip: qsTr("Close (Esc)"); onClicked: root.closeSearch() }
                }
            }

            // ---------- broadcast banner ----------
            Rectangle {
                visible: root.session && root.session.broadcast
                z: 4
                anchors.bottom: parent.bottom
                anchors.right: parent.right
                anchors.bottomMargin: 10
                anchors.rightMargin: 22
                height: 28
                width: bcRow.implicitWidth + 20
                radius: 14
                color: Theme.warnDim
                border.color: Theme.warn
                RowLayout {
                    id: bcRow
                    anchors.centerIn: parent
                    spacing: 6
                    IconLabel { glyph: Theme.iconBroadcast; size: 13; color: Theme.warn }
                    Text { textFormat: Text.PlainText; text: qsTr("Broadcast: typing goes to %1 tab(s)").arg(root.session ? root.session.broadcastCount : 0); color: Theme.text; font.pixelSize: 11 }
                }
            }

            Rectangle {
                id: bellRect
                anchors.fill: parent
                color: Theme.warn
                opacity: 0
                NumberAnimation on opacity { id: bellFlash; running: false; from: 0.08; to: 0; duration: 220 }
            }

            // ---------- connecting ----------
            Rectangle {
                anchors.centerIn: parent
                visible: root.session && (root.session.state === root.sPreparing || root.session.state === root.sConnecting)
                width: 380
                height: connCol.implicitHeight + 40
                radius: 14
                color: Theme.surface
                border.color: Theme.border
                ColumnLayout {
                    id: connCol
                    anchors.fill: parent
                    anchors.margins: 20
                    spacing: 10
                    BusyIndicator { Layout.alignment: Qt.AlignHCenter; running: parent.visible; implicitWidth: 34; implicitHeight: 34 }
                    Text {
                        textFormat: Text.PlainText
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        text: root.session ? root.session.statusText : ""
                        color: Theme.text
                        font.pixelSize: 13
                        wrapMode: Text.WordWrap
                    }
                    Text {
                        textFormat: Text.PlainText
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        visible: root.session && (root.session.expectedIp !== "" || root.session.expectedLocation !== "")
                        text: root.session ? qsTr("Exit IP: %1  %2").arg(root.session.expectedIp || "—").arg(root.session.expectedLocation) : ""
                        color: Theme.textDim
                        font.pixelSize: 11
                        font.family: Theme.monoFont
                        wrapMode: Text.WordWrap
                    }
                    AppButton {
                        Layout.alignment: Qt.AlignHCenter
                        text: qsTr("Cancel"); variant: "ghost"; compact: true
                        onClicked: root.session.disconnectSession()
                    }
                }
            }

            // ---------- host key ----------
            Rectangle {
                anchors.centerIn: parent
                visible: root.session && root.session.state === root.sHostKey
                width: Math.min(560, parent.width - 40)
                height: hkCol.implicitHeight + 48
                radius: 14
                color: Theme.surface
                border.width: root.session && root.session.hostKeyStatus !== 0 ? 2 : 1
                border.color: root.session && root.session.hostKeyStatus !== 0 ? Theme.danger : Theme.border

                ColumnLayout {
                    id: hkCol
                    anchors.fill: parent
                    anchors.margins: 24
                    spacing: 12

                    RowLayout {
                        spacing: 12
                        IconLabel {
                            glyph: root.session && root.session.hostKeyStatus !== 0 ? Theme.iconWarning : Theme.iconFingerprint
                            size: 22
                            color: root.session && root.session.hostKeyStatus !== 0 ? Theme.danger : Theme.accent
                        }
                        Text {
                            textFormat: Text.PlainText
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            color: Theme.text
                            font.pixelSize: 17
                            font.weight: Font.DemiBold
                            text: !root.session ? "" : root.session.hostKeyStatus === 0
                                  ? qsTr("First connection to this server")
                                  : qsTr("WARNING: the server's host key has CHANGED")
                        }
                    }
                    Text {
                        textFormat: Text.PlainText
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: Theme.textDim
                        font.pixelSize: 12
                        text: !root.session ? "" : root.session.hostKeyStatus === 0
                              ? qsTr("Compare the fingerprint with the one shown by the server admin (ssh-keygen -lf /etc/ssh/ssh_host_*_key.pub) before trusting it.")
                              : qsTr("Someone could be intercepting the connection (man-in-the-middle), or the server was reinstalled. "
                                   + "Only continue if you know why the key changed.")
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: fpCol.implicitHeight + 20
                        radius: 8
                        color: Theme.terminalBg
                        border.color: Theme.border
                        ColumnLayout {
                            id: fpCol
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 4
                            Text { textFormat: Text.PlainText; text: root.session ? root.session.hostKeyType : ""; color: Theme.textDim; font.pixelSize: 11; font.family: Theme.monoFont }
                            TextEdit {
                                textFormat: TextEdit.PlainText
                                Layout.fillWidth: true
                                text: root.session ? root.session.hostKeyFingerprint : ""
                                readOnly: true
                                selectByMouse: true
                                wrapMode: TextEdit.WrapAnywhere
                                color: Theme.text
                                font.pixelSize: 13
                                font.family: Theme.monoFont
                            }
                        }
                    }
                    // SSHFP: does the domain's DNS publish this key?
                    RowLayout {
                        Layout.fillWidth: true
                        visible: root.session && root.session.sshfpState !== ""
                        spacing: 8
                        readonly property string st: root.session ? root.session.sshfpState : ""
                        readonly property color tint: st === "match" ? Theme.ok : st === "mismatch" ? Theme.danger : Theme.textDim
                        BusyIndicator { visible: parent.st === "checking"; running: visible; implicitWidth: 18; implicitHeight: 18 }
                        IconLabel {
                            visible: parent.st !== "checking"
                            glyph: parent.st === "match" ? Theme.iconShieldCheck : parent.st === "mismatch" ? Theme.iconWarning : Theme.iconInfo
                            size: 14; color: parent.tint
                        }
                        Text {
                            textFormat: Text.PlainText
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            font.pixelSize: 12
                            color: parent.tint
                            text: parent.st === "checking" ? qsTr("Checking DNS (SSHFP)…")
                                  : parent.st === "match" ? (root.session.sshfpSecure
                                        ? qsTr("DNS confirms this key (SSHFP, signed with DNSSEC). It is safe to trust it.")
                                        : qsTr("DNS has the same key (SSHFP), but the domain is not signed with DNSSEC, so this is only a hint."))
                                  : parent.st === "mismatch" ? qsTr("DNS (SSHFP) lists a DIFFERENT key for this server. Do not connect unless you know why.")
                                  : parent.st === "none" ? qsTr("This domain publishes no SSHFP records — compare the fingerprint another way.")
                                  : qsTr("Could not check DNS (SSHFP).")
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Item { Layout.fillWidth: true }
                        AppButton { text: qsTr("Cancel"); variant: "ghost"; onClicked: root.session.acceptHostKey(false) }
                        AppButton {
                            text: root.session && root.session.hostKeyStatus !== 0 ? qsTr("Replace key and connect") : qsTr("Trust and connect")
                            variant: root.session && root.session.hostKeyStatus !== 0 ? "danger" : "primary"
                            onClicked: root.session.acceptHostKey(true)
                        }
                    }
                }
            }

            // ---------- password / passphrase / keyboard-interactive ----------
            Rectangle {
                id: secretCard
                anchors.centerIn: parent
                visible: root.session && root.session.state === root.sSecret
                width: Math.min(440, parent.width - 40)
                height: secCol.implicitHeight + 48
                radius: 14
                color: Theme.surface
                border.color: Theme.border

                onVisibleChanged: if (visible) { secretField.text = ""; secretField.field.forceActiveFocus() }

                function submit() {
                    root.session.submitSecret(secretField.text, saveSecret.visible && saveSecret.checked)
                    secretField.text = ""
                }

                ColumnLayout {
                    id: secCol
                    anchors.fill: parent
                    anchors.margins: 24
                    spacing: 12
                    Text {
                        textFormat: Text.PlainText
                        text: !root.session ? "" : root.session.secretKind === 1 ? qsTr("Key passphrase")
                              : root.session.secretKind === 2 ? (/code|otp|token|verif|2fa|authenticator/i.test(root.session.secretPrompt)
                                                                  ? qsTr("Server 2FA code") : qsTr("Server asks"))
                              : qsTr("Password")
                        color: Theme.text
                        font.pixelSize: 17
                        font.weight: Font.DemiBold
                    }
                    Text {
                        textFormat: Text.PlainText
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: root.session ? root.session.secretPrompt : ""
                        color: root.session && root.session.secretRetry ? Theme.danger : Theme.textDim
                        font.pixelSize: 12
                    }
                    AppField {
                        id: secretField
                        Layout.fillWidth: true
                        secret: !(root.session && root.session.secretEcho)
                        onAccepted: secretCard.submit()
                    }
                    AppCheckBox {
                        id: saveSecret
                        visible: root.session && root.session.canSaveSecret && root.session.secretKind !== 2
                        checked: true
                        text: root.session && root.session.secretKind === 1
                              ? qsTr("Save passphrase in the vault") : qsTr("Save password in the vault")
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Saved only after the server accepts it.")
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Item { Layout.fillWidth: true }
                        AppButton { text: qsTr("Cancel"); variant: "ghost"; onClicked: root.session.cancelSecret() }
                        AppButton { text: qsTr("Continue"); variant: "primary"; onClicked: secretCard.submit() }
                    }
                }
            }

            // ---------- closed banner ----------
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 16
                visible: root.session && root.session.state === root.sClosed
                height: closedRow.implicitHeight + 24
                radius: 10
                color: root.session && root.session.closedWithError ? Theme.dangerDim : Theme.surface
                border.color: root.session && root.session.closedWithError ? Theme.danger : Theme.border

                RowLayout {
                    id: closedRow
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 12
                    IconLabel {
                        glyph: root.session && root.session.closedWithError ? Theme.iconWarning : Theme.iconPlug
                        color: root.session && root.session.closedWithError ? Theme.danger : Theme.textDim
                    }
                    Text {
                        textFormat: Text.PlainText
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: !root.session ? "" : root.session.statusText
                              + (root.session.reconnectIn > 0
                                 ? "\n" + qsTr("Reconnecting in %1 s (attempt %2 of %3)…").arg(root.session.reconnectIn).arg(root.session.reconnectAttempt + 1).arg(5)
                                 : "")
                        color: Theme.text
                        font.pixelSize: 13
                    }
                    AppButton { visible: root.session && root.session.reconnectIn > 0; text: qsTr("Stop"); variant: "ghost"; compact: true; onClicked: root.session.cancelReconnect() }
                    AppButton { text: qsTr("Reconnect"); glyph: Theme.iconRefresh; variant: "primary"; compact: true; onClicked: { root.session.cancelReconnect(); root.session.reconnect() } }
                    AppButton { text: qsTr("Close tab"); variant: "ghost"; compact: true; onClicked: root.closeRequested() }
                }
            }
        }

        // side panel
        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: root.panel !== "" ? Math.min(420, root.width * 0.45) : 0
            visible: Layout.preferredWidth > 1
            color: Theme.surface
            clip: true
            Behavior on Layout.preferredWidth { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }
            Rectangle { width: 1; height: parent.height; color: Theme.border }
            Loader {
                anchors.fill: parent
                anchors.leftMargin: 1
                active: root.panel !== ""
                sourceComponent: root.panel === "sftp" ? sftpComp : tunnelsComp
            }
        }
        }

        MonitorBar {
            Layout.fillWidth: true
            session: root.session
            visible: root.isSsh && root.session && root.session.monitorOn && root.session.state === root.sConnected
        }
    }

    Component { id: sftpComp; SftpPanel { session: root.session; onCloseRequested: root.panel = "" } }
    Component { id: tunnelsComp; TunnelsPanel { session: root.session; onCloseRequested: root.panel = "" } }

    AppMenu {
        id: snippetMenu
        // Instantiator, not Repeater: a Repeater inside a Menu leaves an empty row.
        Instantiator {
            model: App.snippets
            delegate: AppMenuItem {
                required property var model
                text: (model.script ? "▸ " : "") + model.name
                onTriggered: {
                    const idx = (function () { for (let i = 0; i < App.sessions.count; ++i) if (App.sessions.at(i) === root.session) return i; return -1 })()
                    App.runSnippet(model.id, idx, false)
                    term.forceActiveFocus()
                }
            }
            onObjectAdded: (i, o) => snippetMenu.insertItem(i, o)
            onObjectRemoved: (i, o) => snippetMenu.removeItem(o)
        }
        AppMenuItem {
            enabled: false
            visible: App.snippets.count === 0
            height: visible ? implicitHeight : 0
            text: qsTr("No snippets yet — add them in the Snippets page")
        }
    }

    // host notes (encrypted with the host in the vault)
    property bool hostHasNotes: false
    function refreshNotesFlag() { hostHasNotes = !!(session && session.hostId && App.hostNotes(session.hostId) !== "") }
    onSessionChanged: refreshNotesFlag()
    Connections { target: App.hosts; function onModelReset() { root.refreshNotesFlag() } }
    Popup {
        id: notesPop
        parent: notesBtn
        x: Math.min(0, -width + notesBtn.width)
        y: notesBtn.height + 6
        width: 420
        height: 340
        padding: 12
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
        palette.window: Theme.surface
        palette.windowText: Theme.text
        palette.base: Theme.surface2
        palette.text: Theme.text
        background: Rectangle { radius: 10; color: Theme.surface; border.color: Theme.border }
        property string err: ""
        function openNotes() { notesEdit.text = App.hostNotes(root.session.hostId); err = ""; open(); notesEdit.forceActiveFocus() }
        contentItem: ColumnLayout {
            spacing: 8
            RowLayout {
                Layout.fillWidth: true
                IconLabel { glyph: Theme.iconFile; size: 14; color: Theme.accent }
                Text { textFormat: Text.PlainText; Layout.fillWidth: true; text: qsTr("Notes · %1").arg(root.session ? root.session.label : ""); color: Theme.text; font.pixelSize: 13; font.weight: Font.DemiBold; elide: Text.ElideRight }
                IconLabel { glyph: Theme.iconLock; size: 11; color: Theme.textFaint }
                Text { textFormat: Text.PlainText; text: qsTr("encrypted"); color: Theme.textFaint; font.pixelSize: 10 }
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: 8
                color: Theme.surface2
                border.color: notesEdit.activeFocus ? Theme.accent : Theme.border
                ScrollView {
                    anchors.fill: parent
                    anchors.margins: 1
                    TextArea {
                        textFormat: TextArea.PlainText
                        id: notesEdit
                        wrapMode: TextArea.Wrap
                        selectByMouse: true
                        color: Theme.text
                        font.pixelSize: 13
                        placeholderText: qsTr("What runs here, who to call, where the backups are…")
                        placeholderTextColor: Theme.textFaint
                        background: null
                    }
                }
            }
            Text { textFormat: Text.PlainText; visible: notesPop.err !== ""; text: notesPop.err; color: Theme.danger; font.pixelSize: 11 }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                AppButton { text: qsTr("Close"); variant: "ghost"; compact: true; onClicked: notesPop.close() }
                AppButton {
                    text: qsTr("Save"); variant: "primary"; compact: true
                    onClicked: {
                        notesPop.err = App.setHostNotes(root.session.hostId, notesEdit.text)
                        if (notesPop.err === "") { root.refreshNotesFlag(); notesPop.close() }
                    }
                }
            }
        }
    }

    ThemeEditor { id: tabThemeEditor }

    // Theme gallery for this tab: mini previews, scrollable.
    Popup {
        id: themeMenu
        x: root.width - width - 10
        y: 50
        width: Math.min(452, root.width - 20)
        height: Math.min(520, root.height - 60)
        padding: 10
        modal: false
        focus: true
        background: Rectangle { radius: 10; color: Theme.surface; border.color: Theme.border }
        onClosed: term.forceActiveFocus()
        contentItem: ColumnLayout {
            spacing: 8
            RowLayout {
                Layout.fillWidth: true
                Text { textFormat: Text.PlainText; Layout.fillWidth: true; text: qsTr("Colour theme for this tab"); color: Theme.text; font.pixelSize: 13; font.weight: Font.DemiBold }
                AppButton { text: qsTr("Create your own"); glyph: Theme.iconAdd; compact: true; onClicked: { themeMenu.close(); tabThemeEditor.openNew() } }
            }
            GridView {
                id: themeGrid
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                cellWidth: Math.floor(width / 2)
                cellHeight: 98
                boundsBehavior: Flickable.StopAtBounds
                model: App.terminalThemes
                ScrollBar.vertical: ScrollBar {}
                delegate: Item {
                    width: themeGrid.cellWidth
                    height: themeGrid.cellHeight
                    ThemePreview {
                        anchors.fill: parent
                        anchors.margins: 4
                        theme: modelData
                        onEditRequested: { themeMenu.close(); tabThemeEditor.openEdit(modelData) }
                        selected: root.session && root.session.themeId === modelData.id
                        onClicked: { root.session.themeId = modelData.id; themeMenu.close() }
                    }
                }
            }
        }
    }

    Connections {
        target: root.session
        function onChanged() {
            if (root.visible && root.primary && root.session.state === root.sConnected && !term.activeFocus && !root.inWindow)
                term.forceActiveFocus()
            if (root.session.state !== root.sConnected && root.panel !== "")
                root.panel = ""
        }
    }
}
