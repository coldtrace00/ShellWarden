import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import ShellWarden

// Side panel for creating / editing a host: SSH or serial, credentials,
// route (proxy, jump host), tunnels and appearance.
Drawer {
    id: root
    property string agentName: ""
    property string hostId: ""
    property bool serial: false
    property bool telnet: false          // plain-text Telnet (old switches / routers)
    property var forwards: []

    edge: Qt.RightEdge
    width: Math.min(480, (parent ? parent.width : 480) * 0.92)
    height: parent ? parent.height : 600
    parent: Overlay.overlay
    modal: true
    dim: true
    Overlay.modal: Rectangle { color: "#80070910" }

    background: Rectangle {
        color: Theme.surface
        Rectangle { width: 1; height: parent.height; color: Theme.border }
    }

    readonly property bool pendingTunnelValid: parseInt(fBind.text) > 0
                                              && (fType.currentIndex === 2 || /^.+:\d+$/.test(fDest.text.trim()))
    function addPendingTunnel() {
        if (!pendingTunnelValid) return
        const d = fDest.text.trim()
        const i = d.lastIndexOf(":")
        const f = root.forwards.slice()
        f.push({ type: ["L", "R", "D"][fType.currentIndex], bindHost: "127.0.0.1", bindPort: parseInt(fBind.text),
                 destHost: fType.currentIndex === 2 ? "" : d.substring(0, i),
                 destPort: fType.currentIndex === 2 ? 0 : parseInt(d.substring(i + 1)), enabled: fAuto.checked })
        root.forwards = f
        fBind.text = ""; fDest.text = ""
    }

    readonly property var hostColors: ["", "#4fd1c5", "#2dd4bf", "#22c55e", "#84cc16", "#a3e635", "#facc15", "#f5c26b",
                                        "#fb923c", "#ff8a65", "#ef4444", "#ff5c72", "#f43f5e", "#ec4899", "#d946ef", "#c58cff",
                                        "#a78bfa", "#8b5cf6", "#6366f1", "#5e8bff", "#3b82f6", "#0ea5e9", "#7dd3fc", "#94a3b8",
                                        "#e2e8f0", "#a16207"]

    function indexIn(model, id) {
        for (let i = 0; i < model.count; ++i)
            if (model.get(i).id === id) return i + 1
        return 0
    }
    function themeIndex(id) {
        for (let i = 0; i < App.terminalThemes.length; ++i)
            if (App.terminalThemes[i].id === id) return i + 1
        return 0
    }

    function openFor(id, asSerial) {
        root.hostId = id
        const h = id ? App.host(id) : ({})
        root.serial = id ? h.type === "serial" : !!asSerial
        root.telnet = id ? h.type === "telnet" : false
        label.text = h.label || ""
        group.text = h.group || ""
        hostname.text = h.type === "serial" ? "" : (h.hostname || "")
        port.text = String(h.port || 22)
        username.text = h.username || ""
        password.text = h.password || ""
        password.revealed = false
        keyBox.currentIndex = indexIn(App.keys, h.keyId || "")
        proxyBox.currentIndex = indexIn(App.proxies, h.proxyId || "")
        jumpBox.currentIndex = jumpIndex(h.jumpHostId || "")
        colorPick.value = h.color || ""
        themeBox.currentIndex = themeIndex(h.theme || "")
        root.forwards = (h.forwards || []).slice()
        fBind.text = ""; fDest.text = ""; fType.currentIndex = 0
        confirmMaster.checked = !!h.confirmMaster
        alwaysAsk.checked = !!h.alwaysAskPassphrase
        useAgent.checked = !!h.useAgent
        agentForward.checked = !!h.agentForward
        root.agentName = App.agentStatus().name || ""
        notesArea.text = h.notes || ""
        serialPort.editText = h.serialPort || ""
        baud.currentIndex = Math.max(0, baud.model.indexOf(String(h.baud || 115200)))
        dataBits.currentIndex = Math.max(0, [8, 7, 6, 5].indexOf(h.dataBits || 8))
        parity.currentIndex = Math.max(0, ["none", "even", "odd", "mark", "space"].indexOf(h.parity || "none"))
        stopBits.currentIndex = (h.stopBits || 1) === 2 ? 1 : 0
        flow.currentIndex = Math.max(0, ["none", "rtscts", "xonxoff"].indexOf(h.flow || "none"))
        err.text = ""
        open();
        if (root.serial) label.field.forceActiveFocus(); else hostname.field.forceActiveFocus()
    }

    // jump host candidates: all SSH hosts except this one
    readonly property var jumpCandidates: {
        const out = []
        for (let i = 0; i < App.hosts.count; ++i) {
            const h = App.hosts.get(i)
            if (h.id !== root.hostId && h.type !== "serial" && h.type !== "telnet") out.push(h)
        }
        return out
    }
    function jumpIndex(id) {
        for (let i = 0; i < jumpCandidates.length; ++i)
            if (jumpCandidates[i].id === id) return i + 1
        return 0
    }

    function save(andConnect) {
        const host = {
            id: root.hostId,
            type: root.serial ? "serial" : root.telnet ? "telnet" : "ssh",
            label: label.text.trim(),
            group: group.text.trim(),
            color: colorPick.value,
            theme: themeBox.currentIndex > 0 ? App.terminalThemes[themeBox.currentIndex - 1].id : "",
            notes: notesArea.text
        }
        if (root.serial) {
            host.serialPort = serialPort.editText.trim()
            host.baud = parseInt(baud.currentText)
            host.dataBits = [8, 7, 6, 5][dataBits.currentIndex]
            host.parity = ["none", "even", "odd", "mark", "space"][parity.currentIndex]
            host.stopBits = stopBits.currentIndex === 1 ? 2 : 1
            host.flow = ["none", "rtscts", "xonxoff"][flow.currentIndex]
        } else {
            host.hostname = hostname.text.trim()
            host.port = parseInt(port.text) || 0
            host.username = username.text.trim()
            host.password = password.text
            host.keyId = keyBox.currentIndex > 0 ? App.keys.get(keyBox.currentIndex - 1).id : ""
            host.proxyId = proxyBox.currentIndex > 0 ? App.proxies.get(proxyBox.currentIndex - 1).id : ""
            host.jumpHostId = jumpBox.currentIndex > 0 ? root.jumpCandidates[jumpBox.currentIndex - 1].id : ""
            root.addPendingTunnel()   // filled in but "Add" not pressed: keep it
            host.forwards = root.forwards
            host.confirmMaster = confirmMaster.checked
            host.alwaysAskPassphrase = alwaysAsk.checked
            host.useAgent = useAgent.checked
            host.agentForward = agentForward.checked
        }
        const e = App.saveHost(host)
        if (e !== "") { err.text = e; return }
        password.text = ""
        close()
        if (andConnect && root.hostId !== "")
            App.openHost(root.hostId)
    }

    component SectionTitle: Text {
        Layout.leftMargin: 24
        Layout.topMargin: 10
        color: Theme.textFaint
        font.pixelSize: 11
        font.letterSpacing: 1
    }
    component LabeledCombo: ColumnLayout {
        property alias label: lbl.text
        property alias combo: cb
        property alias model: cb.model
        property alias currentIndex: cb.currentIndex
        property alias currentText: cb.currentText
        spacing: 5
        Text { id: lbl; color: Theme.textDim; font.pixelSize: 12; font.weight: Font.Medium }
        AppCombo { id: cb; Layout.fillWidth: true }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 24
            Layout.bottomMargin: 8
            Text {
                text: root.hostId ? qsTr("Edit host") : qsTr("New host")
                color: Theme.text
                font.pixelSize: 20
                font.weight: Font.DemiBold
                Layout.fillWidth: true
            }
            // in a narrow drawer "Delete" moves up here so the footer stays on one line
            AppButton {
                visible: root.hostId !== "" && root.width < 560
                glyph: Theme.iconDelete; variant: "ghost"; compact: true
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Delete")
                onClicked: { App.deleteHost(root.hostId); root.close() }
            }
            AppButton { glyph: Theme.iconClose; variant: "ghost"; compact: true; onClicked: root.close() }
        }

        // SSH / Serial switch
        RowLayout {
            Layout.leftMargin: 24
            Layout.rightMargin: 24
            Layout.bottomMargin: 10
            spacing: 6
            Repeater {
                model: [{ t: qsTr("SSH"), k: "ssh", g: Theme.iconTerminal }, { t: qsTr("Telnet"), k: "telnet", g: Theme.iconGlobe },
                        { t: qsTr("Serial (COM)"), k: "serial", g: Theme.iconSerial }]
                delegate: AbstractButton {
                    id: seg
                    readonly property bool on: (root.serial ? "serial" : root.telnet ? "telnet" : "ssh") === modelData.k
                    enabled: modelData.k === "serial" ? App.serialAvailable : true
                    implicitHeight: 32
                    implicitWidth: segRow.implicitWidth + 24
                    hoverEnabled: true
                    onClicked: {
                        root.serial = modelData.k === "serial"
                        const wasTelnet = root.telnet
                        root.telnet = modelData.k === "telnet"
                        if (root.telnet && port.text === "22") port.text = "23"
                        else if (wasTelnet && !root.telnet && port.text === "23") port.text = "22"
                    }
                    background: Rectangle { radius: 8; color: seg.on ? Theme.accentDim : (seg.hovered ? Theme.surface2 : "transparent"); border.color: seg.on ? Theme.accent : Theme.border }
                    contentItem: Item {
                        RowLayout {
                            id: segRow
                            anchors.centerIn: parent
                            spacing: 6
                            IconLabel { glyph: modelData.g; size: 14; color: seg.on ? Theme.accent : Theme.textDim }
                            Text { text: modelData.t; color: seg.on ? Theme.text : Theme.textDim; font.pixelSize: 12 }
                        }
                    }
                }
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true

            ColumnLayout {
                width: parent.width
                spacing: 12

                Item { height: 2 }

                // ---------------- SSH ----------------
                SectionTitle { visible: !root.serial; text: qsTr("ADDRESS") }
                RowLayout {
                    visible: !root.serial
                    Layout.fillWidth: true
                    Layout.leftMargin: 24; Layout.rightMargin: 24
                    spacing: 10
                    AppField { id: hostname; Layout.fillWidth: true; label: qsTr("Host name or IP"); placeholder: "example.com"; mono: true }
                    AppField { id: port; Layout.preferredWidth: 90; label: qsTr("Port"); mono: true; field.validator: IntValidator { bottom: 1; top: 65535 } }
                }
                Rectangle {
                    visible: root.telnet
                    Layout.fillWidth: true
                    Layout.leftMargin: 24; Layout.rightMargin: 24
                    implicitHeight: tnWarn.implicitHeight + 20
                    radius: 8
                    color: Theme.warnDim
                    border.color: Theme.warn
                    RowLayout {
                        id: tnWarn
                        anchors.left: parent.left; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                        anchors.margins: 10
                        spacing: 8
                        IconLabel { glyph: Theme.iconWarning; size: 16; color: Theme.warn; Layout.alignment: Qt.AlignTop }
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            color: Theme.text
                            font.pixelSize: 12
                            text: qsTr("Telnet is NOT encrypted: the login, the password and everything you type can be read on the way. Use it only for old switches, routers and devices inside your own network. The login is typed in the terminal when the device asks.")
                        }
                    }
                }

                // ---------------- Serial ----------------
                SectionTitle { visible: root.serial; text: qsTr("SERIAL PORT") }
                RowLayout {
                    visible: root.serial
                    Layout.fillWidth: true
                    Layout.leftMargin: 24; Layout.rightMargin: 24
                    spacing: 10
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 5
                        Text { text: qsTr("Port"); color: Theme.textDim; font.pixelSize: 12; font.weight: Font.Medium }
                        AppCombo { id: serialPort; Layout.fillWidth: true; editable: true; model: App.serialPorts() }
                    }
                    LabeledCombo { id: baud; Layout.preferredWidth: 130; label: qsTr("Speed (baud)")
                                   model: ["9600", "19200", "38400", "57600", "115200", "230400", "460800", "921600"]; currentIndex: 4 }
                }
                RowLayout {
                    visible: root.serial
                    Layout.fillWidth: true
                    Layout.leftMargin: 24; Layout.rightMargin: 24
                    spacing: 8
                    LabeledCombo { id: dataBits; Layout.fillWidth: true; label: qsTr("Data bits"); model: ["8", "7", "6", "5"] }
                    LabeledCombo { id: parity; Layout.fillWidth: true; label: qsTr("Parity"); model: [qsTr("None"), qsTr("Even"), qsTr("Odd"), qsTr("Mark"), qsTr("Space")] }
                    LabeledCombo { id: stopBits; Layout.fillWidth: true; label: qsTr("Stop bits"); model: ["1", "2"] }
                    LabeledCombo { id: flow; Layout.fillWidth: true; label: qsTr("Flow control"); model: [qsTr("None"), "RTS/CTS", "XON/XOFF"] }
                }

                AppField { id: label; Layout.fillWidth: true; Layout.leftMargin: 24; Layout.rightMargin: 24; label: qsTr("Label"); placeholder: root.serial ? qsTr("Defaults to the port name") : qsTr("Defaults to the host name") }
                AppField { id: group; Layout.fillWidth: true; Layout.leftMargin: 24; Layout.rightMargin: 24; label: qsTr("Group"); placeholder: qsTr("e.g. Production") }

                // ---------------- credentials ----------------
                SectionTitle { visible: !root.serial && !root.telnet; text: qsTr("CREDENTIALS") }
                AppField { id: username; visible: !root.serial && !root.telnet; Layout.fillWidth: true; Layout.leftMargin: 24; Layout.rightMargin: 24; label: qsTr("Username"); placeholder: "root"; mono: true }
                AppField {
                    id: password
                    visible: !root.serial && !root.telnet
                    Layout.fillWidth: true; Layout.leftMargin: 24; Layout.rightMargin: 24
                    label: qsTr("Password")
                    secret: true
                    placeholder: qsTr("Leave empty to be asked when connecting")
                    hint: qsTr("Stored encrypted in the vault.")
                }
                ColumnLayout {
                    visible: !root.serial && !root.telnet
                    Layout.fillWidth: true; Layout.leftMargin: 24; Layout.rightMargin: 24
                    spacing: 5
                    Text { text: qsTr("SSH key (from the keychain)"); color: Theme.textDim; font.pixelSize: 12; font.weight: Font.Medium }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6
                    AppCombo {
                        id: keyBox
                        Layout.fillWidth: true
                        model: {
                            const items = [qsTr("None")]
                            for (let i = 0; i < App.keys.count; ++i) {
                                const k = App.keys.get(i)
                                items.push(k.name + "  ·  " + k.type)
                            }
                            return items
                        }
                    }
                        AppButton {
                            glyph: Theme.iconAdd; compact: true
                            onClicked: genKey.openFresh()
                            ToolTip.visible: hovered; ToolTip.text: qsTr("Create a new key")
                        }
                        AppButton {
                            glyph: Theme.iconFolder; compact: true
                            onClicked: importKey.openFresh()
                            ToolTip.visible: hovered; ToolTip.text: qsTr("Import a key (OpenSSH, PEM, PuTTY .ppk)")
                        }
                    }
                    Text {
                        visible: keyBox.currentIndex > 0 && App.keys.count >= keyBox.currentIndex
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: {
                            if (keyBox.currentIndex <= 0) return ""
                            const k = App.keys.get(keyBox.currentIndex - 1)
                            if (!k.encrypted) return qsTr("Key has no passphrase.")
                            return k.hasPassphrase ? qsTr("Passphrase saved in the vault — no prompt.")
                                                   : qsTr("Passphrase will be asked on connect (you can save it then).")
                        }
                        color: Theme.textFaint
                        font.pixelSize: 11
                    }
                }

                // ---------------- route ----------------
                SectionTitle { visible: !root.serial; text: qsTr("ROUTE") }
                ColumnLayout {
                    visible: !root.serial
                    Layout.fillWidth: true; Layout.leftMargin: 24; Layout.rightMargin: 24
                    spacing: 5
                    Text { text: qsTr("Proxy"); color: Theme.textDim; font.pixelSize: 12; font.weight: Font.Medium }
                    AppCombo {
                        id: proxyBox
                        Layout.fillWidth: true
                        model: {
                            const items = [App.settings.killSwitch ? qsTr("Default proxy (kill switch)") : qsTr("Direct connection")]
                            for (let i = 0; i < App.proxies.count; ++i) {
                                const p = App.proxies.get(i)
                                items.push(p.name + "  ·  " + (p.type === "http" ? "HTTP" : p.type === "tor" ? "Tor" : p.type === "cloudflare" ? "Cloudflare" : "SOCKS5"))
                            }
                            return items
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        font.pixelSize: 11
                        color: proxyBox.currentIndex === 0 && App.settings.killSwitch && !App.settings.defaultProxyId
                               ? Theme.danger : Theme.textFaint
                        text: proxyBox.currentIndex > 0 && App.proxies.get(proxyBox.currentIndex - 1).type === "cloudflare"
                              ? qsTr("Through Cloudflare Tunnel: the address above must be the hostname published in Cloudflare (e.g. ssh.example.com); cloudflared is started automatically. The port is not used.")
                              : proxyBox.currentIndex > 0
                              ? qsTr("All traffic to this host goes through the proxy. If the proxy fails the connection fails — it never falls back to direct.")
                              : (App.settings.killSwitch
                                 ? (App.settings.defaultProxyId ? qsTr("Kill switch is on: the default proxy will be used.")
                                                                : qsTr("Kill switch is on and no default proxy is set: connecting will be blocked."))
                                 : qsTr("Connects directly from your IP."))
                    }
                }
                ColumnLayout {
                    visible: !root.serial && !root.telnet
                    Layout.fillWidth: true; Layout.leftMargin: 24; Layout.rightMargin: 24
                    spacing: 5
                    Text { text: qsTr("Jump host (ProxyJump)"); color: Theme.textDim; font.pixelSize: 12; font.weight: Font.Medium }
                    AppCombo {
                        id: jumpBox
                        Layout.fillWidth: true
                        model: {
                            const items = [qsTr("None — connect to the host itself")]
                            for (const h of root.jumpCandidates) items.push(h.label + "  ·  " + h.hostname)
                            return items
                        }
                    }
                    Text {
                        visible: jumpBox.currentIndex > 0
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        font.pixelSize: 11
                        color: Theme.textFaint
                        text: qsTr("Route: proxy (if any) → jump host → this host. The jump host uses its own saved password/key; connect to it directly once to trust its key.")
                    }
                }

                // ---------------- tunnels ----------------
                SectionTitle { visible: !root.serial && !root.telnet; text: qsTr("TUNNELS") }
                ColumnLayout {
                    visible: !root.serial && !root.telnet
                    Layout.fillWidth: true; Layout.leftMargin: 24; Layout.rightMargin: 24
                    spacing: 6
                    Repeater {
                        model: root.forwards
                        delegate: Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: 38
                            radius: 8
                            color: Theme.surface2
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 10
                                anchors.rightMargin: 6
                                spacing: 8
                                Text { text: "-" + modelData.type; color: Theme.accent; font.family: Theme.monoFont; font.bold: true; font.pixelSize: 12 }
                                Text {
                                    Layout.fillWidth: true
                                    elide: Text.ElideMiddle
                                    color: Theme.text
                                    font.family: Theme.monoFont
                                    font.pixelSize: 11
                                    text: (modelData.bindHost || "127.0.0.1") + ":" + modelData.bindPort
                                          + (modelData.type === "D" ? "  (SOCKS5)" : "  →  " + modelData.destHost + ":" + modelData.destPort)
                                }
                                // "auto": open together with the connection; otherwise it is
                                // started by hand from the Tunnels panel of the tab
                                AbstractButton {
                                    id: autoBtn
                                    readonly property bool on: modelData.enabled !== false
                                    implicitHeight: 22
                                    implicitWidth: autoTxt.implicitWidth + 14
                                    hoverEnabled: true
                                    onClicked: {
                                        const f = root.forwards.slice()
                                        f[index] = Object.assign({}, f[index], { enabled: !autoBtn.on })
                                        root.forwards = f
                                    }
                                    background: Rectangle { radius: 6; color: autoBtn.on ? Theme.accentDim : "transparent"; border.color: autoBtn.on ? Theme.accent : Theme.border }
                                    contentItem: Text { id: autoTxt; text: autoBtn.on ? qsTr("auto") : qsTr("manual"); color: autoBtn.on ? Theme.accent : Theme.textDim
                                                        font.pixelSize: 10; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                                    ToolTip.visible: hovered
                                    ToolTip.text: autoBtn.on ? qsTr("Opens by itself on connect. Click: start it by hand from the Tunnels panel instead.")
                                                             : qsTr("Started by hand: in the tab press the Tunnels button → Start. Click: open it by itself on connect.")
                                }
                                AbstractButton {
                                    width: 24; height: 24
                                    onClicked: { const f = root.forwards.slice(); f.splice(index, 1); root.forwards = f }
                                    contentItem: Item { IconLabel { anchors.centerIn: parent; glyph: Theme.iconClose; size: 11; color: Theme.textDim } }
                                }
                            }
                        }
                    }
                    // new tunnel form
                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: tf.implicitHeight + 20
                        radius: 8
                        color: Theme.bg
                        border.color: Theme.border
                        ColumnLayout {
                            id: tf
                            anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                            anchors.margins: 10
                            spacing: 8
                            AppCombo {
                                id: fType
                                Layout.fillWidth: true
                                model: [qsTr("L — open a server port on this PC"), qsTr("R — open a port of this PC on the server"), qsTr("D — SOCKS5 proxy through the server")]
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8
                                ColumnLayout {
                                    spacing: 3
                                    Text { text: fType.currentIndex === 1 ? qsTr("Port on the server") : qsTr("Port on this PC"); color: Theme.textDim; font.pixelSize: 11 }
                                    TextField {
                                        id: fBind; Layout.preferredWidth: 110; placeholderText: fType.currentIndex === 2 ? "1080" : "8080"
                                        color: Theme.text; font.family: Theme.monoFont
                                        validator: IntValidator { bottom: 1; top: 65535 }
                                    }
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    visible: fType.currentIndex !== 2
                                    spacing: 3
                                    Text { text: fType.currentIndex === 1 ? qsTr("Goes to (address:port on this PC's side)") : qsTr("Goes to (address:port as the server sees it)")
                                           color: Theme.textDim; font.pixelSize: 11; elide: Text.ElideRight; Layout.fillWidth: true }
                                    TextField { id: fDest; Layout.fillWidth: true; placeholderText: "127.0.0.1:80"; color: Theme.text; font.family: Theme.monoFont }
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8
                                Text { Layout.fillWidth: true; Layout.alignment: Qt.AlignVCenter; text: qsTr("Open by itself on connect"); color: Theme.text; font.pixelSize: 12; elide: Text.ElideRight }
                                AppSwitch { id: fAuto; checked: false; Layout.alignment: Qt.AlignVCenter }
                            }
                            AppButton {
                                id: addFwd
                                Layout.fillWidth: true
                                text: qsTr("Add tunnel")
                                glyph: Theme.iconAdd
                                enabled: root.pendingTunnelValid
                                onClicked: root.addPendingTunnel()
                            }
                            Text {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                font.pixelSize: 11
                                color: Theme.textFaint
                                text: fType.currentIndex === 0
                                      ? qsTr("Example: port 8080, goes to 127.0.0.1:80 — then http://127.0.0.1:8080 in your browser opens the web site that runs on the server itself. Or port 18090 → 127.0.0.1:22 and then \"ssh -p 18090 user@127.0.0.1\" reaches the server's SSH through the tunnel.")
                                      : fType.currentIndex === 1
                                        ? qsTr("Example: port 9000 on the server, goes to 127.0.0.1:3000 — programs on the server can then reach a service running on your PC at 127.0.0.1:9000.")
                                        : qsTr("Example: port 1080 — set SOCKS5 proxy 127.0.0.1:1080 in the browser and it browses from the server's IP.")
                            }
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        font.pixelSize: 11
                        color: Theme.warn
                        visible: root.pendingTunnelValid
                        text: qsTr("Press \"Add tunnel\" (or Save — it is added for you).")
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: root.forwards.length > 0
                        wrapMode: Text.WordWrap
                        font.pixelSize: 11
                        color: Theme.textFaint
                        text: qsTr("\"auto\" — opens together with the connection. \"manual\" — stays closed; during the session press the Tunnels button in the tab and Start / Stop it whenever you need.")
                    }
                }

                // ---------------- advanced security ----------------
                SectionTitle { visible: !root.serial; text: qsTr("ADVANCED SECURITY") }
                ColumnLayout {
                    visible: !root.serial
                    Layout.fillWidth: true; Layout.leftMargin: 24; Layout.rightMargin: 24
                    spacing: 10

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Text { text: qsTr("Ask for the master password to connect"); color: Theme.text; font.pixelSize: 13 }
                            Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textFaint; font.pixelSize: 11
                                   text: qsTr("Even when the vault is unlocked. Good for production servers.") }
                        }
                        AppSwitch { id: confirmMaster }
                    }
                    RowLayout {
                        visible: !root.telnet
                        Layout.fillWidth: true
                        spacing: 12
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Text { text: qsTr("Always ask the key passphrase"); color: Theme.text; font.pixelSize: 13 }
                            Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textFaint; font.pixelSize: 11
                                   text: qsTr("Ignore a passphrase saved in the vault for this host.") }
                        }
                        AppSwitch { id: alwaysAsk }
                    }
                    RowLayout {
                        visible: !root.telnet
                        Layout.fillWidth: true
                        spacing: 12
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Text { text: qsTr("Use keys from the SSH agent"); color: Theme.text; font.pixelSize: 13 }
                            Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textFaint; font.pixelSize: 11
                                   text: (root.agentName !== "" ? qsTr("Found: %1.").arg(root.agentName) + " "
                                                                : qsTr("No agent is running right now.") + " ")
                                         + qsTr("Pageant, the Windows OpenSSH agent, ssh-agent, KeePassXC, a YubiKey… are tried before the key above.") }
                        }
                        AppSwitch { id: useAgent }
                    }
                    RowLayout {
                        visible: !root.telnet
                        Layout.fillWidth: true
                        spacing: 12
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Text { text: qsTr("Agent forwarding (ssh -A)"); color: Theme.text; font.pixelSize: 13 }
                            Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: agentForward.checked ? Theme.warn : Theme.textFaint; font.pixelSize: 11
                                   text: qsTr("From this server you can go further (git pull, ssh to the next server) with your keys, which never leave this computer. Only for servers you trust: while you are connected, root there can use your agent.") }
                        }
                        AppSwitch { id: agentForward }
                    }
                }

                // ---------------- appearance ----------------
                // ---------------- notes ----------------
                SectionTitle { text: qsTr("NOTES") }
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 24; Layout.rightMargin: 24
                    spacing: 5
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.max(90, Math.min(240, notesArea.implicitHeight + 16))
                        radius: 8
                        color: Theme.surface2
                        border.color: notesArea.activeFocus ? Theme.accent : Theme.border
                        ScrollView {
                            anchors.fill: parent
                            anchors.margins: 1
                            TextArea {
                                id: notesArea
                                wrapMode: TextArea.Wrap
                                selectByMouse: true
                                color: Theme.text
                                font.pixelSize: 13
                                placeholderText: qsTr("e.g. what runs here, who to call, where the backups are, links to docs…")
                                placeholderTextColor: Theme.textFaint
                                background: null
                            }
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: qsTr("Stored encrypted in the vault, like passwords. In an open tab: the notes button in the toolbar.")
                        color: Theme.textFaint; font.pixelSize: 11
                    }
                }

                SectionTitle { text: qsTr("APPEARANCE") }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.leftMargin: 24; Layout.rightMargin: 24
                    implicitHeight: lookCol.implicitHeight + 28
                    radius: 10
                    color: Theme.bg
                    border.color: Theme.border

                    ColumnLayout {
                        id: lookCol
                        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                        anchors.margins: 14
                        spacing: 10

                        // live preview: how the tab and the card will look
                        readonly property string previewName: label.text.trim() !== "" ? label.text.trim()
                                                              : (hostname.text.trim() !== "" ? hostname.text.trim() : qsTr("New host"))
                        readonly property color previewColor: colorPick.value !== "" ? colorPick.value : Theme.avatarColor(previewName)
                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: 52
                            radius: 8
                            color: Theme.surface
                            border.color: Theme.border
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 10; anchors.rightMargin: 12
                                spacing: 10
                                Rectangle {
                                    width: 32; height: 32; radius: 8
                                    color: Theme.tint(lookCol.previewColor, "2e")
                                    Text { anchors.centerIn: parent; text: Theme.initials(lookCol.previewName); color: lookCol.previewColor; font.pixelSize: 12; font.weight: Font.Bold }
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 4
                                    Text { Layout.fillWidth: true; text: lookCol.previewName; color: Theme.text; font.pixelSize: 13; font.weight: Font.DemiBold; elide: Text.ElideRight }
                                    Rectangle { Layout.preferredWidth: 90; height: 3; radius: 2; color: lookCol.previewColor }
                                }
                                Text { text: qsTr("preview"); color: Theme.textFaint; font.pixelSize: 10 }
                            }
                        }

                        Text { text: qsTr("Colour"); color: Theme.textDim; font.pixelSize: 12; font.weight: Font.Medium }
                        // even grid: every cell has the same width, circles are centred in it
                        GridLayout {
                            id: colorPick
                            property string value: ""
                            readonly property bool isCustom: value !== "" && root.hostColors.indexOf(value) < 0
                            Layout.fillWidth: true
                            columns: Math.max(6, Math.floor((width + columnSpacing) / (30 + columnSpacing)))
                            rowSpacing: 8
                            columnSpacing: 6
                            Repeater {
                                model: root.hostColors.concat(["+"])
                                delegate: Item {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 28
                                    readonly property bool isAuto: modelData === ""
                                    readonly property bool isPlus: modelData === "+"
                                    readonly property bool on: isPlus ? colorPick.isCustom : colorPick.value === modelData
                                    Rectangle {
                                        anchors.centerIn: parent
                                        width: 26; height: 26; radius: 13
                                        color: parent.isPlus ? (colorPick.isCustom ? colorPick.value : Theme.surface2)
                                               : parent.isAuto ? Theme.surface2 : modelData
                                        border.width: parent.on ? 2 : 1
                                        border.color: parent.on ? Theme.text : Theme.border
                                        Rectangle {   // inner ring so the selected colour stays visible
                                            visible: parent.parent.on
                                            anchors.fill: parent; anchors.margins: 2
                                            radius: width / 2
                                            color: "transparent"
                                            border.width: 2
                                            border.color: Theme.bg
                                        }
                                        Text { visible: parent.parent.isAuto; anchors.centerIn: parent; text: "A"; color: Theme.textDim; font.pixelSize: 11; font.weight: Font.DemiBold }
                                        IconLabel { visible: parent.parent.isPlus && !colorPick.isCustom; anchors.centerIn: parent; glyph: Theme.iconAdd; size: 12; color: Theme.textDim }
                                    }
                                    MouseArea {
                                        id: swMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            if (parent.isPlus) { colorDlg.selectedColor = colorPick.value || "#4fd1c5"; colorDlg.open() }
                                            else colorPick.value = modelData
                                        }
                                    }
                                    ToolTip.visible: swMouse.containsMouse && (isAuto || isPlus)
                                    ToolTip.text: isAuto ? qsTr("Automatic: picked from the name") : qsTr("Any colour…")
                                }
                            }
                        }
                        ColorPicker {
                            id: colorDlg
                            title: qsTr("Host colour")
                            onAccepted: colorPick.value = selectedColor.toString()
                        }

                        Text { text: qsTr("Terminal theme"); color: Theme.textDim; font.pixelSize: 12; font.weight: Font.Medium; Layout.topMargin: 4 }
                        // 0 = default from Settings, i > 0 = App.terminalThemes[i - 1]
                        ListView {
                            id: themeBox
                            property int currentIndex: 0
                            Layout.fillWidth: true
                            Layout.preferredHeight: 92
                            orientation: ListView.Horizontal
                            spacing: 8
                            clip: true
                            boundsBehavior: Flickable.StopAtBounds
                            ScrollBar.horizontal: ScrollBar { policy: ScrollBar.AsNeeded }
                            onCurrentIndexChanged: positionViewAtIndex(currentIndex, ListView.Contain)
                            model: {
                                let def = App.terminalThemes[0]
                                for (const t of App.terminalThemes) if (t.id === App.settings.terminalTheme) def = t
                                return [Object.assign({}, def, { name: qsTr("Default"), custom: false })].concat(App.terminalThemes)
                            }
                            delegate: ThemePreview {
                                width: 150; height: 82
                                theme: modelData
                                selected: themeBox.currentIndex === index
                                onClicked: themeBox.currentIndex = index
                                editable: false
                            }
                        }
                    }
                }

                Text {
                    id: err
                    visible: text !== ""
                    Layout.fillWidth: true; Layout.leftMargin: 24; Layout.rightMargin: 24
                    color: Theme.danger
                    wrapMode: Text.WordWrap
                    font.pixelSize: 12
                }
                Item { height: 8 }
            }
        }

        // the save error is shown right above the buttons, so it is never scrolled away
        Rectangle {
            visible: err.text !== ""
            Layout.fillWidth: true
            implicitHeight: footErr.implicitHeight + 20
            color: Theme.dangerDim
            Text {
                id: footErr
                anchors.left: parent.left; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 24; anchors.rightMargin: 24
                text: err.text
                color: Theme.text
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }
        // Footer: a Flow (not a RowLayout) so long translations wrap to a second
        // line instead of making the whole drawer wider than the window.
        Item {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            implicitHeight: footer.implicitHeight + 32
            Flow {
                id: footer
                anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                anchors.margins: 16
                spacing: 8
                layoutDirection: Qt.RightToLeft
                readonly property bool narrow: root.width < 560
                AppButton {
                    visible: root.hostId !== ""
                    text: qsTr("Save & connect"); glyph: Theme.iconPlug; variant: "primary"
                    onClicked: root.save(true)
                }
                AppButton { text: qsTr("Save"); variant: "secondary"; onClicked: root.save(false) }
                AppButton { text: qsTr("Cancel"); variant: "ghost"; onClicked: root.close() }
                AppButton {
                    visible: root.hostId !== "" && !footer.narrow
                    text: qsTr("Delete"); glyph: Theme.iconDelete; variant: "danger"
                    onClicked: { App.deleteHost(root.hostId); root.close() }
                }
            }
        }
    }

    // New keys are appended to the keychain: select the newest one.
    KeyGenerateDialog { id: genKey; onCreated: keyBox.currentIndex = App.keys.count }
    KeyImportDialog { id: importKey; onCreated: keyBox.currentIndex = App.keys.count }

    onClosed: { password.text = ""; password.revealed = false }
}
