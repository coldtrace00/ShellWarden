import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import ShellWarden

Drawer {
    id: root
    property string proxyId: ""
    property var testResult: null
    property bool testing: false
    readonly property bool isTor: typeBox.currentIndex === 2
    readonly property bool isCf: typeBox.currentIndex === 3
    readonly property bool isSs: typeBox.currentIndex === 4
    readonly property var types: ["socks5", "http", "tor", "cloudflare", "shadowsocks"]
    readonly property var ssMethods: App.shadowsocksMethods()
    readonly property var torModes: ["auto", "browser", "service", "exe"]
    property string torMode: "auto"
    property var torInfo: ({})
    property bool torChecking: false
    function refreshTor() { torChecking = true; App.checkTor(torMode) }
    Connections {
        target: App
        function onTorStatusReady(info) { if (info.mode === root.torMode) { root.torInfo = info; root.torChecking = false } }
    }

    edge: Qt.RightEdge
    width: Math.min(440, (parent ? parent.width : 440) * 0.9)
    height: parent ? parent.height : 600
    parent: Overlay.overlay
    modal: true
    Overlay.modal: Rectangle { color: "#80070910" }
    background: Rectangle {
        color: Theme.surface
        Rectangle { width: 1; height: parent.height; color: Theme.border }
    }

    function openFor(id) {
        root.proxyId = id
        const p = id ? App.proxy(id) : ({})
        name.text = p.name || ""
        typeBox.currentIndex = Math.max(0, types.indexOf(p.type || "socks5"))
        ssMethod.currentIndex = Math.max(0, ssMethods.indexOf(p.method || "chacha20-ietf-poly1305"))
        ssLink.text = ""; ssLinkErr.text = ""
        cfPath.text = p.cfPath || ""
        torMode = p.torMode || "auto"
        torPath.text = p.torPath || ""
        if (p.type === "tor") refreshTor()
        host.text = p.host || ""
        port.text = p.port ? String(p.port) : "1080"
        user.text = p.username || ""
        pass.text = p.password || ""
        err.text = ""
        testResult = null
        testing = false
        open()
        if (!isTor && !isCf) host.field.forceActiveFocus()
    }

    function draft() {
        return {
            id: root.proxyId,
            name: name.text.trim(),
            type: root.types[typeBox.currentIndex],
            method: root.isSs ? root.ssMethods[ssMethod.currentIndex] : "",
            cfPath: cfPath.text.trim(),
            torMode: root.torMode,
            torPath: torPath.text.trim(),
            host: root.isCf ? "cloudflared" : host.text.trim(),
            port: root.isCf ? 1 : (parseInt(port.text) || 0),
            username: root.isSs ? "" : user.text,
            password: pass.text
        }
    }

    Connections {
        target: App
        function onProxyDraftChecked(result) {
            if (!root.opened) return
            root.testing = false
            root.testResult = result
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 24
            Layout.bottomMargin: 8
            Text {
                text: root.proxyId ? qsTr("Edit proxy") : qsTr("New proxy")
                color: Theme.text; font.pixelSize: 20; font.weight: Font.DemiBold
                Layout.fillWidth: true
            }
            AppButton { glyph: Theme.iconClose; variant: "ghost"; compact: true; onClicked: root.close() }
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: availableWidth
            clip: true
        ColumnLayout {
            x: 24
            width: parent.width - 48
            spacing: 14

            AppField { id: name; Layout.fillWidth: true; label: qsTr("Name"); placeholder: qsTr("e.g. NL exit") }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 5
                Text { text: qsTr("Type"); color: Theme.textDim; font.pixelSize: 12; font.weight: Font.Medium }
                AppCombo {
                    id: typeBox
                    Layout.fillWidth: true
                    model: [qsTr("SOCKS5 (remote DNS)"), qsTr("HTTP CONNECT"), qsTr("Tor"), qsTr("Cloudflare Tunnel"), qsTr("Shadowsocks (encrypted, hard to block)")]
                    onActivated: (i) => {
                        if (i === 4 && (port.text === "1080" || port.text === "8080")) port.text = "8388"
                        if (port.text === "1080" && i === 1) port.text = "8080"; else if (port.text === "8080" && i === 0) port.text = "1080"
                        if (i === 2) { if (name.text.trim() === "") name.text = "Tor"; root.refreshTor() }
                        if (i === 3 && name.text.trim() === "") name.text = "Cloudflare Tunnel"
                    }
                }
            }

            // ---------------- Tor ----------------
            ColumnLayout {
                visible: root.isTor
                Layout.fillWidth: true
                spacing: 8
                Text { text: qsTr("Which Tor to use"); color: Theme.textDim; font.pixelSize: 12; font.weight: Font.Medium }
                Repeater {
                    model: [
                        { m: "auto", t: qsTr("Automatically"), d: qsTr("Tor Browser if it is open, otherwise the Tor service") },
                        { m: "browser", t: qsTr("Tor Browser"), d: qsTr("Keep Tor Browser open while you work (port 9150)") },
                        { m: "service", t: qsTr("Tor service"), d: qsTr("Tor installed as a system service (port 9050)") },
                        { m: "exe", t: qsTr("ShellWarden starts tor.exe"), d: qsTr("Nothing to keep open: started when needed, stopped on exit") }
                    ]
                    delegate: AbstractButton {
                        id: modeBtn
                        readonly property bool on: root.torMode === modelData.m
                        Layout.fillWidth: true
                        implicitHeight: modeCol.implicitHeight + 16
                        hoverEnabled: true
                        onClicked: { root.torMode = modelData.m; root.refreshTor() }
                        background: Rectangle { radius: 8; color: modeBtn.on ? Theme.accentDim : (modeBtn.hovered ? Theme.surface2 : "transparent")
                                                border.color: modeBtn.on ? Theme.accent : Theme.border }
                        contentItem: RowLayout {
                            spacing: 10
                            Rectangle {
                                Layout.leftMargin: 10
                                width: 16; height: 16; radius: 8
                                color: "transparent"; border.width: 2; border.color: modeBtn.on ? Theme.accent : Theme.textFaint
                                Rectangle { anchors.centerIn: parent; width: 8; height: 8; radius: 4; color: Theme.accent; visible: modeBtn.on }
                            }
                            ColumnLayout {
                                id: modeCol
                                Layout.fillWidth: true
                                Layout.rightMargin: 10
                                spacing: 1
                                Text { text: modelData.t; color: Theme.text; font.pixelSize: 13 }
                                Text { Layout.fillWidth: true; text: modelData.d; color: modeBtn.on ? Theme.text : Theme.textDim; opacity: modeBtn.on ? 0.85 : 1; font.pixelSize: 11; wrapMode: Text.WordWrap }
                            }
                        }
                    }
                }
                // tor.exe path
                ColumnLayout {
                    visible: root.torMode === "exe"
                    Layout.fillWidth: true
                    spacing: 6
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        AppField { id: torPath; Layout.fillWidth: true; label: qsTr("Path to tor.exe"); mono: true
                                   placeholder: root.torInfo.exe ? root.torInfo.exe : qsTr("found automatically if next to ShellWarden") }
                        AppButton { Layout.alignment: Qt.AlignBottom; text: qsTr("Browse…"); glyph: Theme.iconFolder; onClicked: torDlg.open() }
                    }
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        font.pixelSize: 11
                        color: Theme.textFaint
                        textFormat: Text.StyledText
                        linkColor: Theme.accent
                        onLinkActivated: (l) => Qt.openUrlExternally(l)
                        text: qsTr("Get the \"Tor Expert Bundle\" for your system at <a href=\"https://www.torproject.org/download/tor/\">torproject.org/download/tor</a>, unpack it and choose tor.exe (or put the folder \"tor\" next to ShellWarden.exe).")
                    }
                    FileDialog {
                        id: torDlg
                        title: qsTr("Choose tor.exe")
                        onAccepted: torPath.text = decodeURIComponent(String(selectedFile).replace(/^file:\/{2,3}/, Qt.platform.os === "windows" ? "" : "/"))
                    }
                }
                // what is running now
                Rectangle {
                    visible: root.torMode !== "exe"
                    Layout.fillWidth: true
                    implicitHeight: torState.implicitHeight + 20
                    radius: 8
                    color: root.torChecking ? Theme.surface2 : root.torInfo.port > 0 ? Theme.okDim : Theme.warnDim
                    border.color: root.torChecking ? Theme.border : root.torInfo.port > 0 ? Theme.ok : Theme.warn
                    RowLayout {
                        id: torState
                        anchors.left: parent.left; anchors.right: parent.right; anchors.verticalCenter: parent.verticalCenter
                        anchors.margins: 10
                        spacing: 8
                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            color: Theme.text
                            font.pixelSize: 12
                            text: root.torChecking ? qsTr("Looking for a running Tor…") : root.torInfo.port > 0
                                  ? (root.torInfo.which === "browser" ? qsTr("Found: Tor Browser on 127.0.0.1:9150")
                                     : root.torInfo.which === "service" ? qsTr("Found: Tor service on 127.0.0.1:9050")
                                     : qsTr("Found: Tor started by ShellWarden"))
                                  : (root.torMode === "browser" ? qsTr("Tor Browser is not running. Open it and wait until it says \"Connected\".")
                                     : root.torMode === "service" ? qsTr("The Tor service is not running on port 9050.")
                                     : qsTr("No running Tor found. Open Tor Browser, start the Tor service, or choose \"ShellWarden starts tor.exe\"."))
                        }
                        AppButton { text: qsTr("Check"); compact: true; glyph: Theme.iconRefresh; onClicked: root.refreshTor() }
                    }
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    font.pixelSize: 11
                    color: Theme.textDim
                    text: qsTr("Tor is slow and many servers block Tor exits. .onion addresses work as host names. \"New identity\" on the Proxies page gives a new exit IP.")
                }
            }

            // ---------------- Cloudflare Tunnel ----------------
            ColumnLayout {
                visible: root.isCf
                Layout.fillWidth: true
                spacing: 10
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: cfHow.implicitHeight + 20
                    radius: 8
                    color: Theme.surface2
                    border.color: Theme.border
                    Text {
                        id: cfHow
                        anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                        anchors.margins: 10
                        wrapMode: Text.WordWrap
                        color: Theme.textDim
                        font.pixelSize: 12
                        textFormat: Text.StyledText
                        linkColor: Theme.accent
                        onLinkActivated: (l) => Qt.openUrlExternally(l)
                        text: qsTr("The server runs <b>cloudflared tunnel</b> with a public hostname like <b>ssh.example.com → ssh://localhost:22</b> (Cloudflare dashboard → Zero Trust → Networks → Tunnels). Port 22 can then be closed and the server IP stays hidden.<br><br>In the host put that hostname as the address and choose this route. ShellWarden starts <b>cloudflared access tcp</b> on this computer by itself. Free on the Cloudflare Free plan. <a href=\"https://developers.cloudflare.com/cloudflare-one/connections/connect-networks/use-cases/ssh/\">How to set up the server</a>")
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    AppField { id: cfPath; Layout.fillWidth: true; label: qsTr("Path to cloudflared (optional)"); mono: true
                               placeholder: qsTr("found automatically (winget, Program Files, PATH)") }
                    AppButton { Layout.alignment: Qt.AlignBottom; text: qsTr("Browse…"); glyph: Theme.iconFolder; onClicked: cfDlg.open() }
                }
                FileDialog {
                    id: cfDlg
                    title: qsTr("Choose cloudflared")
                    onAccepted: cfPath.text = decodeURIComponent(String(selectedFile).replace(/^file:\/{2,3}/, Qt.platform.os === "windows" ? "" : "/"))
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    font.pixelSize: 11
                    color: Theme.textFaint
                    text: qsTr("Cloudflare Access (optional): without a token a browser window asks you to log in the first time. A service token logs in without a browser.")
                }
            }

            // ---------------- Shadowsocks ----------------
            ColumnLayout {
                visible: root.isSs
                Layout.fillWidth: true
                spacing: 6
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.textFaint; font.pixelSize: 11
                    text: qsTr("Everything up to the Shadowsocks server is encrypted and looks like random bytes: the network in between sees neither which server you connect to nor that it is SSH. You need your own Shadowsocks server (for example Outline).")
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    AppField { id: ssLink; Layout.fillWidth: true; label: qsTr("ss:// link (optional)"); placeholder: "ss://…"; mono: true
                               onAccepted: ssFill.clicked() }
                    AppButton {
                        id: ssFill
                        Layout.alignment: Qt.AlignBottom
                        text: qsTr("Fill in"); compact: true
                        enabled: ssLink.text.trim() !== ""
                        onClicked: {
                            const r = App.parseShadowsocksLink(ssLink.text)
                            ssLinkErr.text = r.ok ? "" : r.error
                            if (!r.ok) return
                            host.text = r.host; port.text = String(r.port); pass.text = r.password
                            ssMethod.currentIndex = Math.max(0, root.ssMethods.indexOf(r.method))
                            if (name.text.trim() === "" && r.name) name.text = r.name
                            ssLink.text = ""
                        }
                    }
                }
                Text { id: ssLinkErr; visible: text !== ""; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.danger; font.pixelSize: 11 }
                Text { text: qsTr("Cipher"); color: Theme.textDim; font.pixelSize: 12; font.weight: Font.Medium }
                AppCombo { id: ssMethod; Layout.fillWidth: true; model: root.ssMethods }
            }

            RowLayout {
                visible: !root.isTor && !root.isCf
                Layout.fillWidth: true
                spacing: 10
                AppField { id: host; Layout.fillWidth: true; label: root.isSs ? qsTr("Server") : qsTr("Address"); placeholder: root.isSs ? "ss.example.com" : "proxy.example.com"; mono: true }
                AppField { id: port; Layout.preferredWidth: 90; label: qsTr("Port"); mono: true; field.validator: IntValidator { bottom: 1; top: 65535 } }
            }
            AppField { id: user; visible: !root.isTor && !root.isSs; Layout.fillWidth: true; label: root.isCf ? qsTr("Service token Client ID (optional)") : qsTr("Username (optional)"); mono: true }
            AppField { id: pass; visible: !root.isTor; Layout.fillWidth: true; label: root.isCf ? qsTr("Service token Client Secret (optional)") : root.isSs ? qsTr("Password") : qsTr("Password (optional)"); secret: true; hint: qsTr("Stored encrypted in the vault.") }

            // Test result
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: resCol.implicitHeight + 24
                radius: 8
                visible: root.testing || root.testResult !== null
                color: root.testing ? Theme.surface2 : (root.testResult && root.testResult.ok ? Theme.okDim : Theme.dangerDim)
                border.color: root.testing ? Theme.border : (root.testResult && root.testResult.ok ? Theme.ok : Theme.danger)
                ColumnLayout {
                    id: resCol
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 4
                    Text {
                        text: root.testing ? qsTr("Testing the proxy…")
                              : root.testResult && root.testResult.cloudflare ? (root.testResult.ok ? qsTr("cloudflared is ready") : qsTr("cloudflared is missing"))
                              : (root.testResult && root.testResult.ok ? qsTr("Proxy works") : qsTr("Proxy does not work"))
                        color: Theme.text; font.pixelSize: 13; font.weight: Font.DemiBold
                    }
                    Text {
                        visible: !root.testing && root.testResult !== null
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: Theme.text
                        font.pixelSize: 12
                        text: !root.testResult ? "" : root.testResult.cloudflare
                              ? (root.testResult.ok ? root.testResult.version + "\n" + root.testResult.exe : root.testResult.error)
                              : (root.testResult.ok
                              ? qsTr("Exit IP (what servers will see): %1\n%2Latency: %3 ms")
                                    .arg(root.testResult.ip)
                                    .arg(root.testResult.location ? root.testResult.location + "\n" : "")
                                    .arg(root.testResult.latencyMs)
                              : root.testResult.error)
                    }
                }
            }

            Text { id: err; visible: text !== ""; Layout.fillWidth: true; color: Theme.danger; font.pixelSize: 12; wrapMode: Text.WordWrap }
        }

        }
        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 16
            spacing: 8
            AppButton {
                visible: root.proxyId !== ""
                text: qsTr("Delete"); glyph: Theme.iconDelete; variant: "danger"
                onClicked: { App.deleteProxy(root.proxyId); root.close() }
            }
            Item { Layout.fillWidth: true }
            AppButton {
                text: qsTr("Test"); glyph: Theme.iconRefresh
                enabled: !root.testing
                onClicked: { root.testing = true; root.testResult = null; App.checkProxyDraft(root.draft()) }
            }
            AppButton {
                text: qsTr("Save"); variant: "primary"
                onClicked: {
                    const e = App.saveProxy(root.draft())
                    if (e !== "") { err.text = e; return }
                    pass.text = ""
                    root.close()
                }
            }
        }
    }

    onClosed: pass.text = ""
}
