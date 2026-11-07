import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ShellWarden

Item {
    id: root
    property string filter: ""

    function matches(m) {
        if (root.filter === "") return true
        const f = root.filter.toLowerCase()
        return (m.label || "").toLowerCase().indexOf(f) >= 0
            || (m.hostname || "").toLowerCase().indexOf(f) >= 0
            || (m.group || "").toLowerCase().indexOf(f) >= 0
            || (m.username || "").toLowerCase().indexOf(f) >= 0
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 28
        spacing: 20

        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text { text: qsTr("Hosts"); color: Theme.text; font.pixelSize: 24; font.weight: Font.DemiBold }
                Text { text: qsTr("Click a host to connect"); color: Theme.textDim; font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.WordWrap }
            }
            TextField {
                id: search
                Layout.fillWidth: true
                Layout.maximumWidth: 260
                Layout.minimumWidth: 110
                implicitWidth: 240
                implicitHeight: 36
                placeholderText: qsTr("Search hosts")
                placeholderTextColor: Theme.textFaint
                color: Theme.text
                leftPadding: 32
                selectByMouse: true
                onTextChanged: root.filter = text
                background: Rectangle {
                    radius: Theme.radiusSmall
                    color: Theme.surface
                    border.color: search.activeFocus ? Theme.accent : Theme.border
                    IconLabel {
                        anchors.left: parent.left; anchors.leftMargin: 11
                        anchors.verticalCenter: parent.verticalCenter
                        glyph: Theme.iconSearch; size: 12; color: Theme.textDim
                    }
                }
            }
            AppButton {
                visible: App.hosts.count > 0
                text: root.width < 980 ? "" : qsTr("Run on hosts"); glyph: Theme.iconTerminal
                onClicked: batchDlg.openWith("", [])
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Run a command on several hosts at once")
            }
            AppButton {
                visible: App.hosts.count > 1
                glyph: Theme.iconSplit
                onClicked: diffDlg.openFor("")
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Compare a config file on two servers")
            }
            AppButton {
                text: root.width < 860 ? "" : qsTr("Import"); glyph: Theme.iconFolder
                onClicked: importDlg.openFresh()
                ToolTip.visible: hovered
                ToolTip.text: qsTr("Import hosts from ~/.ssh/config or PuTTY")
            }
            AppButton {
                visible: App.serialAvailable
                text: root.width < 760 ? "" : qsTr("Serial"); glyph: Theme.iconSerial
                onClicked: editor.openFor("", true)
                ToolTip.visible: hovered
                ToolTip.text: qsTr("New serial (COM) connection")
            }
            AppButton {
                text: root.width < 640 ? "" : qsTr("New host"); glyph: Theme.iconAdd; variant: "primary"
                onClicked: editor.openFor("")
            }
        }

        Flickable {
            id: grid
            visible: App.hosts.count > 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentHeight: flow.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}
            readonly property int columns: Math.max(1, Math.floor(width / 280))
            readonly property real cellWidth: Math.floor(width / columns)

            // Flow (unlike GridView) closes the gaps of filtered-out cards.
            Flow {
                id: flow
                width: grid.width
                Repeater {
                    model: App.hosts
                    delegate: Item {
                        id: cell
                        width: grid.cellWidth
                        height: 92
                        visible: root.matches(model)
                        Rectangle {
                            id: card
                            anchors.fill: parent
                            anchors.margins: 6
                            radius: Theme.radius
                            color: hover.hovered ? Theme.surfaceHover : Theme.surface
                            border.color: hover.hovered ? Theme.accent : Theme.border
                            border.width: 1
                            Behavior on color { ColorAnimation { duration: 90 } }

                            HoverHandler { id: hover }
                            TapHandler {
                                acceptedButtons: Qt.LeftButton
                                onTapped: App.openHost(model.id)
                            }
                            TapHandler {
                                acceptedButtons: Qt.RightButton
                                onTapped: {
                                    hostMenu.hostId = model.id
                                    hostMenu.hostType = model.type || "ssh"
                                    hostMenu.route = App.hostRoute(model.id)
                                    hostMenu.popup()
                                }
                            }

                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 14
                                spacing: 12

                                Rectangle {
                                    width: 44; height: 44; radius: 10
                                    color: Theme.tint(model.color ? model.color : Theme.avatarColor(model.label), "2e")
                                    Text {
                                        anchors.centerIn: parent
                                        text: model.type === "serial" ? "⏚" : Theme.initials(model.label)
                                        color: model.color ? model.color : Theme.avatarColor(model.label)
                                        font.pixelSize: 15
                                        font.weight: Font.Bold
                                    }
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 3
                                    Text {
                                        Layout.fillWidth: true
                                        text: model.label
                                        color: Theme.text
                                        font.pixelSize: 14
                                        font.weight: Font.DemiBold
                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: model.type === "serial" ? model.serialPort + " · " + (model.baud || 115200) + " baud"
                                              : model.type === "telnet" ? "telnet " + model.hostname : (model.username ? model.username + "@" : "") + model.hostname
                                              + (model.port !== 22 ? ":" + model.port : "")
                                        color: Theme.textDim
                                        font.pixelSize: 12
                                        font.family: Theme.monoFont
                                        elide: Text.ElideRight
                                    }
                                    RowLayout {
                                        spacing: 6
                                        IconLabel { visible: !!model.keyId; glyph: Theme.iconKey; size: 11; color: Theme.textDim }
                                        IconLabel { visible: model.hasPassword; glyph: Theme.iconLock; size: 11; color: Theme.textDim }
                                        IconLabel {
                                            visible: !!model.confirmMaster
                                            glyph: Theme.iconShieldCheck; size: 11; color: Theme.ok
                                            HoverHandler { id: tfaHover }
                                            ToolTip.visible: tfaHover.hovered
                                            ToolTip.text: qsTr("The master password is asked before connecting")
                                        }
                                        IconLabel { visible: !!model.proxyId; glyph: Theme.iconShield; size: 11; color: Theme.accent }
                                        IconLabel { visible: !!model.jumpHostId; glyph: Theme.iconHop; size: 11; color: Theme.blue }
                                        IconLabel { visible: !!model.forwards && model.forwards.length > 0; glyph: Theme.iconTunnel; size: 11; color: Theme.textDim }
                                        Text {
                                            visible: !!model.group
                                            text: model.group || ""
                                            color: Theme.textFaint
                                            font.pixelSize: 11
                                        }
                                    }
                                }

                                AbstractButton {
                                    width: 30; height: 30
                                    opacity: hover.hovered ? 1 : 0
                                    hoverEnabled: true
                                    onClicked: editor.openFor(model.id)
                                    background: Rectangle { radius: 6; color: parent.hovered ? Theme.surface2 : "transparent" }
                                    contentItem: IconLabel { glyph: Theme.iconEdit; size: 13; color: Theme.textDim }
                                    ToolTip.visible: hovered
                                    ToolTip.text: qsTr("Edit")
                                }
                            }
                        }
                    }   // cell
                }   // Repeater
            }   // Flow
        }   // Flickable


        // Empty state
        Item {
            visible: App.hosts.count === 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            ColumnLayout {
                anchors.centerIn: parent
                spacing: 12
                IconLabel { Layout.alignment: Qt.AlignHCenter; glyph: Theme.iconServer; size: 40; color: Theme.textFaint }
                Text { Layout.alignment: Qt.AlignHCenter; text: qsTr("No hosts yet"); color: Theme.text; font.pixelSize: 16 }
                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: qsTr("Add a server or use quick connect at the top.")
                    color: Theme.textDim; font.pixelSize: 12
                }
                Row {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: 8
                    AppButton { text: qsTr("Import from ~/.ssh/config or PuTTY"); glyph: Theme.iconFolder; onClicked: importDlg.openFresh() }
                    AppButton { text: qsTr("Add your first host"); glyph: Theme.iconAdd; variant: "primary"; onClicked: editor.openFor("") }
                }
            }
        }
    }

    AppMenu {
        id: hostMenu
        property string hostId: ""
        property string hostType: "ssh"
        property string route: "direct"      // the host's saved route: "direct" or a proxy id
        readonly property bool routable: hostType !== "serial"
        function routeName(r) {
            if (r === "direct") return qsTr("Direct")
            for (let i = 0; i < App.proxies.count; ++i) {
                const p = App.proxies.get(i)
                if (p.id === r) return p.name
            }
            return qsTr("Direct")
        }
        AppMenuItem { text: qsTr("Connect"); onTriggered: App.openHost(hostMenu.hostId) }
        // once, through another route (this tab only; reconnects keep it)
        AppMenu {
            id: viaMenu
            title: qsTr("Connect via")
            enabled: hostMenu.routable
            AppMenuItem {
                text: qsTr("Direct") + (hostMenu.route === "direct" ? "  ·  " + qsTr("saved") : "")
                glyph: Theme.iconGlobe
                onTriggered: { hostMenu.close(); App.openHostVia(hostMenu.hostId, "direct") }
            }
            Instantiator {
                model: App.proxies
                delegate: AppMenuItem {
                    text: model.name + (hostMenu.route === model.id ? "  ·  " + qsTr("saved") : "")
                    glyph: model.type === "tor" ? Theme.iconShield : Theme.iconGlobe
                    enabled: !(hostMenu.hostType === "telnet" && model.type === "cloudflare")
                    onTriggered: { hostMenu.close(); App.openHostVia(hostMenu.hostId, model.id) }
                }
                onObjectAdded: (index, object) => viaMenu.insertItem(index + 1, object)
                onObjectRemoved: (index, object) => viaMenu.removeItem(object)
            }
        }
        // keep it: the host connects this way from now on (same as "Proxy" in the host editor)
        AppMenu {
            id: routeMenu
            title: qsTr("Route: %1").arg(hostMenu.routeName(hostMenu.route))
            enabled: hostMenu.routable
            AppMenuItem {
                text: qsTr("Direct")
                glyph: hostMenu.route === "direct" ? Theme.iconCheck : ""
                onTriggered: { hostMenu.close(); App.setHostRoute(hostMenu.hostId, "direct") }
            }
            Instantiator {
                model: App.proxies
                delegate: AppMenuItem {
                    text: model.name
                    glyph: hostMenu.route === model.id ? Theme.iconCheck : ""
                    enabled: !(hostMenu.hostType === "telnet" && model.type === "cloudflare")
                    onTriggered: { hostMenu.close(); App.setHostRoute(hostMenu.hostId, model.id) }
                }
                onObjectAdded: (index, object) => routeMenu.insertItem(index + 1, object)
                onObjectRemoved: (index, object) => routeMenu.removeItem(object)
            }
        }
        MenuSeparator {}
        AppMenuItem { text: qsTr("Edit"); onTriggered: editor.openFor(hostMenu.hostId) }
        AppMenuItem { text: qsTr("Duplicate"); onTriggered: App.duplicateHost(hostMenu.hostId) }
        AppMenuItem { text: qsTr("Run a command…"); onTriggered: batchDlg.openWith("", [hostMenu.hostId]) }
        AppMenuItem { text: qsTr("Compare a file with another server…"); onTriggered: diffDlg.openFor(hostMenu.hostId) }
        MenuSeparator {}
        AppMenuItem { text: qsTr("Delete"); onTriggered: App.deleteHost(hostMenu.hostId) }
    }

    HostEditor { id: editor }
    ImportHostsDialog { id: importDlg }
    BatchRunDialog { id: batchDlg }
    DiffDialog { id: diffDlg }
}
