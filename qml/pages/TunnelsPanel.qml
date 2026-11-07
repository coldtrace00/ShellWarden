import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ShellWarden

// Live port forwards of a session, with a small animated diagram per tunnel:
// dots flow along the path while data moves; the direction follows -L / -R.
Item {
    id: root
    property TerminalSession session: null
    signal closeRequested()

    function fmt(b) { return App.formatBytes(b) }

    // Tunnels saved in the host's settings: "manual" ones (and auto ones that were
    // stopped) are listed here with a Start button.
    property var saved: []
    function refreshSaved() {
        const h = root.session && root.session.hostId ? App.host(root.session.hostId) : null
        saved = h && h.forwards ? h.forwards : []
    }
    function liveKey(type, port) { return type + ":" + port }
    readonly property var liveKeys: {
        const k = {}
        if (root.session)
            for (const f of root.session.forwards) k[liveKey(f.type, String(f.bind).split(":").pop())] = true
        return k
    }
    readonly property var stopped: saved.filter(f => !liveKeys[liveKey(f.type, String(f.bindPort))])
    readonly property bool canStart: root.session && root.session.state === 4   // Connected
    onSessionChanged: refreshSaved()
    Component.onCompleted: refreshSaved()
    Connections {
        target: App.hosts
        function onModelReset() { root.refreshSaved() }
        function onDataChanged() { root.refreshSaved() }
        function onCountChanged() { root.refreshSaved() }
    }

    component FlowLine: Item {
        id: fl
        property bool active: false
        property bool reverse: false
        property color tint: Theme.accent
        implicitHeight: 14
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width
            height: 2
            radius: 1
            color: fl.active ? Theme.tint(fl.tint, "66") : Theme.border
        }
        property real phase: 0
        NumberAnimation on phase {
            running: fl.active && fl.visible
            from: 0; to: 1
            duration: 1200
            loops: Animation.Infinite
        }
        Repeater {
            model: 3
            Rectangle {
                width: 6; height: 6; radius: 3
                color: fl.tint
                anchors.verticalCenter: parent.verticalCenter
                visible: fl.active
                readonly property real t: (fl.phase + index / 3) % 1
                x: (fl.reverse ? 1 - t : t) * (fl.width - width)
                opacity: Math.sin(t * Math.PI)
            }
        }
        // arrow head
        IconLabel {
            anchors.verticalCenter: parent.verticalCenter
            x: fl.reverse ? -4 : fl.width - width + 4
            glyph: fl.reverse ? "arrowLeft" : "arrowRight"
            size: 12
            color: fl.active ? fl.tint : Theme.textFaint
        }
    }

    component Node: Rectangle {
        property string glyph: ""
        property string label: ""
        property string sub: ""
        property color tint: Theme.textDim
        implicitWidth: Math.max(72, nodeCol.implicitWidth + 14)
        implicitHeight: nodeCol.implicitHeight + 12
        radius: 8
        color: Theme.surface2
        border.color: Theme.border
        ColumnLayout {
            id: nodeCol
            anchors.centerIn: parent
            spacing: 2
            IconLabel { Layout.alignment: Qt.AlignHCenter; glyph: parent.parent.glyph; size: 15; color: parent.parent.tint }
            Text { Layout.alignment: Qt.AlignHCenter; text: parent.parent.label; color: Theme.text; font.pixelSize: 10; font.weight: Font.Medium }
            Text { Layout.alignment: Qt.AlignHCenter; visible: text !== ""; text: parent.parent.sub; color: Theme.textDim; font.pixelSize: 9; font.family: Theme.monoFont }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 10
            spacing: 6
            IconLabel { glyph: Theme.iconTunnel; size: 16; color: Theme.accent }
            Text { text: qsTr("Tunnels"); color: Theme.text; font.pixelSize: 14; font.weight: Font.DemiBold; Layout.fillWidth: true }
            AppButton { glyph: Theme.iconAdd; compact: true; text: qsTr("Add"); onClicked: addBox.visible = !addBox.visible }
            AppButton { glyph: Theme.iconClose; compact: true; variant: "ghost"; onClicked: root.closeRequested() }
        }

        // add form
        Rectangle {
            id: addBox
            visible: false
            Layout.fillWidth: true
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            implicitHeight: addCol.implicitHeight + 20
            radius: 10
            color: Theme.bg
            border.color: Theme.border
            ColumnLayout {
                id: addCol
                anchors.fill: parent
                anchors.margins: 10
                spacing: 8
                AppCombo {
                    id: typeBox
                    Layout.fillWidth: true
                    model: [qsTr("Local (-L): this PC → server → target"),
                            qsTr("Remote (-R): server → this PC"),
                            qsTr("Dynamic (-D): SOCKS5 proxy through the server")]
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 6
                    AppField { id: bindHost; Layout.fillWidth: true; label: typeBox.currentIndex === 1 ? qsTr("Server address") : qsTr("Listen on"); text: "127.0.0.1"; mono: true }
                    AppField { id: bindPort; Layout.preferredWidth: 80; label: qsTr("Port"); mono: true; field.validator: IntValidator { bottom: 1; top: 65535 } }
                }
                RowLayout {
                    Layout.fillWidth: true
                    visible: typeBox.currentIndex !== 2
                    spacing: 6
                    AppField { id: destHost; Layout.fillWidth: true; label: typeBox.currentIndex === 1 ? qsTr("Target on this PC's network") : qsTr("Target (as seen from the server)"); text: "127.0.0.1"; mono: true }
                    AppField { id: destPort; Layout.preferredWidth: 80; label: qsTr("Port"); mono: true; field.validator: IntValidator { bottom: 1; top: 65535 } }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Item { Layout.fillWidth: true }
                    AppButton {
                        text: qsTr("Open tunnel"); variant: "primary"; glyph: Theme.iconPlug
                        enabled: parseInt(bindPort.text) > 0 && (typeBox.currentIndex === 2 || (destHost.text.trim() !== "" && parseInt(destPort.text) > 0))
                        onClicked: {
                            root.session.addForward(["L", "R", "D"][typeBox.currentIndex], bindHost.text, parseInt(bindPort.text),
                                                    destHost.text, parseInt(destPort.text) || 0)
                            addBox.visible = false
                        }
                    }
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.textFaint
                    font.pixelSize: 10
                    text: (typeBox.currentIndex === 0
                           ? qsTr("Example: port 8080, target 127.0.0.1 port 80 → open http://127.0.0.1:8080 in your browser: it shows the web site running on the server itself.")
                           : typeBox.currentIndex === 1
                             ? qsTr("Example: server port 9000, target 127.0.0.1 port 3000 → on the server, 127.0.0.1:9000 reaches the program on port 3000 of this PC.")
                             : qsTr("Example: port 1080 → set SOCKS5 proxy 127.0.0.1:1080 in the browser: it browses from the server's IP."))
                          + "\n" + qsTr("Tunnels opened here last until you disconnect. To open them automatically, add them in the host's settings.")
                }
            }
        }

        // saved tunnels that are not running now
        ColumnLayout {
            visible: root.stopped.length > 0
            Layout.fillWidth: true
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            Layout.topMargin: 4
            spacing: 6
            Text { text: qsTr("Saved for this host — not running"); color: Theme.textDim; font.pixelSize: 11; font.weight: Font.Medium }
            Repeater {
                model: root.stopped
                delegate: Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 40
                    radius: 8
                    color: Theme.surface2
                    border.color: Theme.border
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 6
                        spacing: 8
                        Text { text: "-" + modelData.type; color: Theme.textDim; font.family: Theme.monoFont; font.bold: true; font.pixelSize: 11 }
                        Text {
                            Layout.fillWidth: true
                            elide: Text.ElideMiddle
                            color: Theme.text
                            font.family: Theme.monoFont
                            font.pixelSize: 11
                            text: (modelData.bindHost || "127.0.0.1") + ":" + modelData.bindPort
                                  + (modelData.type === "D" ? "  (SOCKS5)" : "  →  " + modelData.destHost + ":" + modelData.destPort)
                        }
                        AppButton {
                            compact: true
                            variant: "primary"
                            glyph: Theme.iconPlug
                            text: qsTr("Start")
                            enabled: root.canStart
                            onClicked: root.session.addForward(modelData.type, modelData.bindHost || "127.0.0.1", modelData.bindPort,
                                                               modelData.destHost || "", modelData.destPort || 0)
                        }
                    }
                }
            }
        }

        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: 8
            clip: true
            spacing: 8
            model: root.session ? root.session.forwards : []
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            delegate: Rectangle {
                id: card
                width: ListView.view.width - 20
                x: 10
                implicitHeight: cardCol.implicitHeight + 20
                radius: 10
                color: Theme.bg
                border.color: modelData.state === "error" ? Theme.danger : (busy ? Theme.ok : Theme.border)
                Behavior on border.color { ColorAnimation { duration: 200 } }
                readonly property bool busy: modelData.active > 0 || (modelData.idleMs >= 0 && modelData.idleMs < 1500)
                readonly property color tint: modelData.type === "R" ? Theme.blue : (modelData.type === "D" ? "#c58cff" : Theme.accent)

                ColumnLayout {
                    id: cardCol
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 8

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Rectangle {
                            radius: 5
                            implicitWidth: 26; implicitHeight: 20
                            color: Theme.tint(card.tint, "33")
                            Text { anchors.centerIn: parent; text: "-" + modelData.type; color: card.tint; font.pixelSize: 11; font.bold: true; font.family: Theme.monoFont }
                        }
                        Text {
                            Layout.fillWidth: true
                            text: modelData.bind + "  →  " + modelData.dest
                            color: Theme.text
                            font.pixelSize: 11
                            font.family: Theme.monoFont
                            elide: Text.ElideMiddle
                        }
                        AppButton {
                            compact: true
                            variant: "ghost"
                            glyph: Theme.iconClose
                            text: qsTr("Stop")
                            onClicked: root.session.removeForward(modelData.id)
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("Close tunnel")
                        }
                    }

                    // animated diagram: [local] ⇢ [server] ⇢ [target]
                    RowLayout {
                        Layout.fillWidth: true
                        visible: modelData.state !== "error"
                        spacing: 4
                        Node {
                            glyph: modelData.type === "R" ? "globe" : "terminal"
                            label: modelData.type === "R" ? qsTr("Internet") : qsTr("This PC")
                            sub: modelData.type === "R" ? "" : modelData.bind.split(":").pop()
                            tint: card.tint
                        }
                        FlowLine { Layout.fillWidth: true; active: card.busy; tint: card.tint }
                        Node { glyph: "server"; label: qsTr("Server"); sub: modelData.type === "R" ? modelData.bind.split(":").pop() : ""; tint: card.tint }
                        FlowLine { Layout.fillWidth: true; active: card.busy; tint: card.tint }
                        Node {
                            glyph: modelData.type === "D" ? "globe" : (modelData.type === "R" ? "terminal" : "plug")
                            label: modelData.type === "D" ? qsTr("Any host") : (modelData.type === "R" ? qsTr("This PC") : qsTr("Target"))
                            sub: modelData.type === "D" ? "" : modelData.dest.split(":").pop()
                            tint: card.tint
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        Rectangle {
                            width: 8; height: 8; radius: 4
                            color: modelData.state === "error" ? Theme.danger : (card.busy ? Theme.ok : Theme.accent)
                            SequentialAnimation on scale {
                                running: card.busy
                                loops: Animation.Infinite
                                NumberAnimation { to: 1.5; duration: 400 }
                                NumberAnimation { to: 1.0; duration: 400 }
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            text: modelData.state === "error" ? modelData.error
                                  : qsTr("%1 connection(s) · ↓ %2 · ↑ %3").arg(modelData.active).arg(root.fmt(modelData.bytesIn)).arg(root.fmt(modelData.bytesOut))
                            color: modelData.state === "error" ? Theme.danger : Theme.textDim
                            font.pixelSize: 11
                            wrapMode: Text.WordWrap
                        }
                        AppButton {
                            visible: modelData.type === "L" && modelData.state === "active"
                            compact: true
                            variant: "ghost"
                            glyph: Theme.iconGlobe
                            onClicked: Qt.openUrlExternally("http://" + modelData.bind.replace("0.0.0.0", "127.0.0.1") + "/")
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("Test: open http://%1 in the browser").arg(modelData.bind)
                        }
                        AppButton {
                            visible: modelData.type !== "R" && modelData.state === "active"
                            compact: true
                            variant: "ghost"
                            glyph: Theme.iconCopy
                            onClicked: App.copyText(modelData.bind)
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("Copy %1").arg(modelData.bind)
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: modelData.state === "active"
                        wrapMode: Text.WordWrap
                        color: Theme.textFaint
                        font.pixelSize: 10
                        text: modelData.type === "L"
                              ? qsTr("How to use: connect your program (browser, DB client…) to %1 — it reaches %2 as seen from the server.").arg(modelData.bind).arg(modelData.dest)
                              : modelData.type === "D"
                                ? qsTr("How to use: set SOCKS5 proxy %1 in the browser or app; check on any \"what is my IP\" site — it shows the server's IP.").arg(modelData.bind)
                                : qsTr("How to use: on the server, connect to %1 — it reaches %2 on this PC's side.").arg(modelData.bind).arg(modelData.dest)
                    }
                }
            }
        }

        ColumnLayout {
            visible: root.session && root.session.forwards.length === 0 && root.stopped.length === 0 && !addBox.visible
            Layout.alignment: Qt.AlignHCenter
            Layout.bottomMargin: 40
            spacing: 8
            IconLabel { Layout.alignment: Qt.AlignHCenter; glyph: Theme.iconTunnel; size: 34; color: Theme.textFaint }
            Text { Layout.alignment: Qt.AlignHCenter; text: qsTr("No tunnels"); color: Theme.text; font.pixelSize: 13 }
            Text {
                Layout.alignment: Qt.AlignHCenter
                Layout.maximumWidth: root.width - 40
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: qsTr("Forward a port, e.g. a database on the server to 127.0.0.1:5432 on this PC, or open a SOCKS5 proxy through the server.")
                      + "\n\n" + qsTr("Quick test: Add → Local, listen on 8080, target 127.0.0.1:80 → Open tunnel, then press the globe button: the server's web page opens in your browser.")
                color: Theme.textDim
                font.pixelSize: 11
            }
        }
    }
}
