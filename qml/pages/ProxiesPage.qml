import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ShellWarden

Item {
    id: root

    function proxyIndexFor(id) {
        for (let i = 0; i < App.proxies.count; ++i)
            if (App.proxies.get(i).id === id) return i + 1
        return 0
    }

    Component.onCompleted: {
        if (!App.directIp.ok && !App.directIp.checking && App.settings.killSwitch)
            App.checkDirectIp()
    }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            width: parent.width
            spacing: 20

            Item { height: 8 }

            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 28; Layout.rightMargin: 28
                spacing: 10
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Text { text: qsTr("Proxies & kill switch"); color: Theme.text; font.pixelSize: 24; font.weight: Font.DemiBold }
                    Text { text: qsTr("SOCKS5, HTTP CONNECT, Shadowsocks, Tor and Cloudflare Tunnel. Host names are resolved by the proxy, not locally — .onion addresses work through Tor."); color: Theme.textDim; font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                }
                AppButton { text: qsTr("Add proxy"); glyph: Theme.iconAdd; variant: "primary"; onClicked: editor.openFor("") }
            }

            // ---------------- Kill switch card ----------------
            Rectangle {
                Layout.fillWidth: true
                Layout.leftMargin: 28; Layout.rightMargin: 28
                implicitHeight: ksCol.implicitHeight + 36
                radius: Theme.radius
                color: Theme.surface
                border.color: App.settings.killSwitch ? Theme.ok : Theme.border

                ColumnLayout {
                    id: ksCol
                    anchors.fill: parent
                    anchors.margins: 18
                    spacing: 14

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 14
                        Rectangle {
                            width: 44; height: 44; radius: 12
                            color: App.settings.killSwitch ? Theme.okDim : Theme.surface2
                            IconLabel { anchors.centerIn: parent; glyph: App.settings.killSwitch ? Theme.iconShieldCheck : Theme.iconShield; size: 20; color: App.settings.killSwitch ? Theme.ok : Theme.textDim }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 3
                            Text { text: qsTr("Kill switch"); color: Theme.text; font.pixelSize: 16; font.weight: Font.DemiBold }
                            Text {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                color: Theme.textDim
                                font.pixelSize: 12
                                text: qsTr("Protects your real IP: while it is on, %1 never connects to a server directly — only through a proxy.").arg(App.appName)
                            }
                            Repeater {
                                model: [
                                    qsTr("Before connecting, the proxy is checked. If it does not answer, the connection is not made."),
                                    qsTr("While connected, the proxy is checked every %1 s and its exit IP every minute.").arg(App.settings.killSwitchInterval),
                                    qsTr("If the proxy stops working or starts showing your real IP, its sessions are closed at once."),
                                    qsTr("There is never a fallback to a direct connection.")
                                ]
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 6
                                    Text { text: "•"; color: Theme.accent; font.pixelSize: 12; Layout.alignment: Qt.AlignTop }
                                    Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: modelData; color: Theme.textDim; font.pixelSize: 12 }
                                }
                            }
                        }
                        AppSwitch {
                            checked: App.settings.killSwitch
                            onToggled: App.setSetting("killSwitch", checked)
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12
                        opacity: App.settings.killSwitch ? 1 : 0.6
                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.preferredWidth: 200
                            spacing: 2
                            Text { Layout.fillWidth: true; text: qsTr("Kill switch: proxy for hosts without one"); color: Theme.text; font.pixelSize: 13; wrapMode: Text.WordWrap }
                            Text {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                color: Theme.textFaint
                                font.pixelSize: 11
                                text: qsTr("Only used while the kill switch is on, because then nothing may connect directly. Hosts with no proxy of their own (or whose proxy is turned off) go through the proxy chosen here; with \"None\" they are blocked. With the kill switch off such hosts simply connect directly.")
                            }
                        }
                        AppCombo {
                            id: defBox
                            Layout.preferredWidth: 280
                            model: {
                                const items = [qsTr("None — block those hosts")]
                                for (let i = 0; i < App.proxies.count; ++i) items.push(App.proxies.get(i).name)
                                return items
                            }
                            currentIndex: root.proxyIndexFor(App.settings.defaultProxyId)
                            onActivated: (i) => App.setSetting("defaultProxyId", i > 0 ? App.proxies.get(i - 1).id : "")
                        }
                    }

                    Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        IconLabel { glyph: Theme.iconGlobe; color: Theme.textDim }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 8
                                Text { text: qsTr("Your real (direct) IP:"); color: Theme.textDim; font.pixelSize: 12; Layout.alignment: Qt.AlignTop }
                                Text {
                                    Layout.fillWidth: !App.directIp.ok
                                    wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                                    text: App.directIp.checking ? qsTr("checking…")
                                          : (App.directIp.ok ? App.directIp.ip
                                                             : (App.directIp.error ? qsTr("unavailable: ") + App.directIp.error : qsTr("not checked")))
                                    color: Theme.text
                                    font.pixelSize: App.directIp.ok || !App.directIp.error ? 13 : 12
                                    font.family: Theme.monoFont
                                    font.bold: !!App.directIp.ok
                                }
                                CountryBadge { code: App.directIp.ok ? (App.directIp.country || "") : "" }
                            }
                            Text {
                                visible: !!App.directIp.ok && !!App.directIp.location
                                Layout.fillWidth: true
                                text: App.directIp.location || ""
                                color: Theme.textDim
                                font.pixelSize: 11
                                wrapMode: Text.WordWrap
                            }
                        }
                        AppButton { text: qsTr("Check"); glyph: Theme.iconRefresh; compact: true; onClicked: App.checkDirectIp() }
                    }
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: Theme.textFaint
                        font.pixelSize: 11
                        text: qsTr("Used for the leak check: a proxy whose exit IP equals this address is treated as broken.")
                    }
                }
            }

            // ---------------- Firewall lock (Windows) ----------------
            Rectangle {
                visible: App.firewallSupported
                Layout.fillWidth: true
                Layout.leftMargin: 28; Layout.rightMargin: 28
                implicitHeight: fwCol.implicitHeight + 36
                radius: Theme.radius
                color: Theme.surface
                border.color: App.firewallActive ? Theme.ok : Theme.border

                ColumnLayout {
                    id: fwCol
                    anchors.fill: parent
                    anchors.margins: 18
                    spacing: 12
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 14
                        Rectangle {
                            width: 44; height: 44; radius: 12
                            color: App.firewallActive ? Theme.okDim : Theme.surface2
                            IconLabel { anchors.centerIn: parent; glyph: Theme.iconLock; size: 20; color: App.firewallActive ? Theme.ok : Theme.textDim }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 3
                            RowLayout {
                                spacing: 8
                                Text { text: qsTr("Firewall lock"); color: Theme.text; font.pixelSize: 16; font.weight: Font.DemiBold }
                                Rectangle {
                                    radius: 4
                                    color: App.firewallActive ? Theme.okDim : Theme.surface2
                                    implicitWidth: fwState.implicitWidth + 12; implicitHeight: 20
                                    Text { id: fwState; anchors.centerIn: parent; font.pixelSize: 11
                                           text: App.firewallActive ? qsTr("active") : qsTr("off")
                                           color: App.firewallActive ? Theme.ok : Theme.textDim }
                                }
                            }
                            Text {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                color: Theme.textDim
                                font.pixelSize: 12
                                text: qsTr("The strongest protection: a Windows Defender Firewall rule lets %1 connect ONLY to your proxies (and localhost). "
                                         + "Even a bug in the program could not reveal your real IP — Windows drops the packets. "
                                         + "Needs administrator rights (UAC). Update the rule after adding or changing proxies.").arg(App.appName)
                            }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Item { Layout.fillWidth: true }
                        Text { id: fwErr; visible: text !== ""; color: Theme.danger; font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                        AppButton {
                            visible: App.firewallActive
                            text: qsTr("Remove rule"); variant: "ghost"
                            onClicked: fwErr.text = App.removeFirewall()
                        }
                        AppButton {
                            text: App.firewallActive ? qsTr("Update rule") : qsTr("Enable firewall lock")
                            glyph: Theme.iconShieldCheck
                            variant: App.firewallActive ? "secondary" : "primary"
                            enabled: App.proxies.count > 0
                            onClicked: fwErr.text = App.applyFirewall()
                        }
                    }
                }
            }

            // ---------------- Proxy list ----------------
            Repeater {
                model: App.proxies
                delegate: Rectangle {
                    id: pcard
                    Layout.fillWidth: true
                    Layout.leftMargin: 28; Layout.rightMargin: 28
                    implicitHeight: Math.max(76, pcardRow.implicitHeight + 32)
                    radius: Theme.radius
                    color: Theme.surface
                    border.color: Theme.border

                    opacity: model.disabled ? 0.75 : 1
                    readonly property color stateColor: model.disabled ? Theme.textFaint
                                                       : model.checkState === "ok" ? Theme.ok
                                                       : model.checkState === "fail" ? Theme.danger
                                                       : model.checkState === "checking" ? Theme.warn : Theme.textFaint

                    RowLayout {
                        id: pcardRow
                        anchors.fill: parent
                        anchors.margins: 16
                        spacing: 14

                        Rectangle {
                            width: 40; height: 40; radius: 10
                            color: Theme.surface2
                            IconLabel { anchors.centerIn: parent; glyph: Theme.iconGlobe; size: 16; color: Theme.accent }
                            Rectangle {
                                width: 12; height: 12; radius: 6
                                anchors.right: parent.right; anchors.bottom: parent.bottom
                                anchors.margins: -2
                                color: pcard.stateColor
                                border.width: 2; border.color: Theme.surface
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 3
                            RowLayout {
                                spacing: 8
                                Text { text: model.name; color: Theme.text; font.pixelSize: 14; font.weight: Font.DemiBold }
                                Rectangle {
                                    radius: 4; color: Theme.surface2
                                    implicitWidth: ptype.implicitWidth + 12; implicitHeight: 20
                                    Text { id: ptype; anchors.centerIn: parent; text: model.type === "http" ? "HTTP" : model.type === "tor" ? "Tor" : model.type === "cloudflare" ? "Cloudflare" : model.type === "shadowsocks" ? "Shadowsocks" : "SOCKS5"; color: model.type === "tor" ? "#c58cff" : model.type === "cloudflare" ? "#f6a04d" : model.type === "shadowsocks" ? "#5ec8ff" : Theme.textDim; font.pixelSize: 11 }
                                }
                                Rectangle {
                                    visible: App.settings.defaultProxyId === model.id
                                    radius: 4; color: Theme.accentDim
                                    implicitWidth: pdef.implicitWidth + 12; implicitHeight: 20
                                    Text { id: pdef; anchors.centerIn: parent; text: qsTr("default"); color: Theme.accent; font.pixelSize: 11 }
                                }
                                Text { text: model.type === "cloudflare" ? qsTr("cloudflared access tcp → the host's address") : model.host + ":" + model.port; color: Theme.textDim; font.pixelSize: 12; font.family: Theme.monoFont }
                            }
                            RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            CountryBadge { code: model.checkState === "ok" ? (model.exitCountry || "") : ""; showName: false }
                            Text {
                                Layout.fillWidth: true
                                // a failure keeps its whole text (e.g. both IP services' errors)
                                wrapMode: model.checkState === "fail" ? Text.WrapAtWordBoundaryOrAnywhere : Text.NoWrap
                                maximumLineCount: 3
                                elide: Text.ElideRight
                                font.pixelSize: 12
                                color: pcard.stateColor
                                text: {
                                    if (model.disabled)
                                        return App.settings.killSwitch
                                            ? qsTr("Turned off · its hosts use the default proxy; with no default proxy they are blocked (kill switch is on)")
                                            : qsTr("Turned off · its hosts connect directly, from your real IP")
                                    switch (model.checkState) {
                                    case "ok":
                                        if (model.type === "cloudflare") return qsTr("Ready · %1").arg(model.location)
                                        return qsTr("Working · exit IP %1%2 · %3 ms · checked %4")
                                               .arg(model.exitIp)
                                               .arg(model.location ? " (" + model.location + ")" : "")
                                               .arg(model.latencyMs).arg(model.checkedAt)
                                    case "fail": return qsTr("Not working: %1 · checked %2").arg(model.checkError).arg(model.checkedAt)
                                    case "checking": return model.type === "tor" && App.tor.state === "starting" ? App.tor.message : qsTr("Checking…")
                                    default: return qsTr("Not checked yet")
                                    }
                                }
                            }
                            }
                        }

                        AppButton {
                            visible: model.type === "tor" && !model.disabled
                            text: root.width < 900 ? "" : qsTr("New identity"); glyph: Theme.iconRefresh; compact: true
                            onClicked: App.torNewIdentity(model.id)
                            ToolTip.visible: hovered
                            ToolTip.text: qsTr("Switch to new Tor circuits: a different exit IP for new connections")
                        }
                        AppButton { visible: !model.disabled; text: qsTr("Check"); glyph: Theme.iconRefresh; compact: true; onClicked: App.checkProxy(model.id) }
                        AppSwitch {
                            checked: !model.disabled
                            onToggled: App.setProxyEnabled(model.id, checked)
                            HoverHandler { id: onHover }
                            ToolTip.visible: onHover.hovered
                            ToolTip.delay: 300
                            ToolTip.text: checked ? qsTr("Turn this proxy off without deleting it: hosts that use it will connect directly (with the kill switch on — through the default proxy, never directly). Open sessions are not touched.")
                                                  : qsTr("Turn this proxy back on")
                        }
                        AppButton { glyph: Theme.iconEdit; compact: true; variant: "ghost"; onClicked: editor.openFor(model.id) }
                    }
                }
            }

            ColumnLayout {
                visible: App.proxies.count === 0
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: 30
                spacing: 10
                Text { Layout.alignment: Qt.AlignHCenter; text: qsTr("No proxies yet"); color: Theme.text; font.pixelSize: 15 }
                Text { Layout.alignment: Qt.AlignHCenter; text: qsTr("Add a SOCKS5, HTTP, Shadowsocks or Tor proxy, then assign it to hosts or make it the default."); color: Theme.textDim; font.pixelSize: 12 }
            }

            Item { height: 20 }
        }
    }

    ProxyEditor { id: editor }
}
