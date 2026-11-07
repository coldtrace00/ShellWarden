import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ShellWarden

// Import hosts from ~/.ssh/config or PuTTY saved sessions.
Popup {
    id: root
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(640, (parent ? parent.width : 640) - 32)
    height: Math.min(620, (parent ? parent.height : 620) - 32)
    modal: true
    focus: true
    padding: 20
    closePolicy: Popup.CloseOnEscape
    Overlay.modal: Rectangle { color: "#a0070910" }
    background: Rectangle { radius: 14; color: Theme.surface; border.color: Theme.border }

    property string source: "ssh"
    property var items: []
    property var picked: ({})
    property string result: ""

    function load(src) {
        source = src
        items = App.importCandidates(src)
        const p = {}
        for (let i = 0; i < items.length; ++i) p[i] = !items[i].exists
        picked = p
        result = ""
    }
    function openFresh() { load("ssh"); open() }
    readonly property int pickedCount: { let n = 0; for (const k in picked) if (picked[k]) ++n; return n }

    contentItem: ColumnLayout {
        spacing: 12
        RowLayout {
            Layout.fillWidth: true
            Text { textFormat: Text.PlainText; Layout.fillWidth: true; text: qsTr("Import hosts"); color: Theme.text; font.pixelSize: 18; font.weight: Font.DemiBold }
            AppButton { glyph: Theme.iconClose; variant: "ghost"; compact: true; onClicked: root.close() }
        }
        Row {
            spacing: 6
            Repeater {
                model: [{ k: "ssh", t: qsTr("OpenSSH config") }, { k: "putty", t: qsTr("PuTTY sessions") }]
                delegate: AbstractButton {
                    id: seg
                    readonly property bool on: root.source === modelData.k
                    implicitHeight: 32
                    implicitWidth: segText.implicitWidth + 24
                    hoverEnabled: true
                    onClicked: root.load(modelData.k)
                    background: Rectangle { radius: 8; color: seg.on ? Theme.accentDim : (seg.hovered ? Theme.surface2 : "transparent"); border.color: seg.on ? Theme.accent : Theme.border }
                    contentItem: Text { textFormat: Text.PlainText; id: segText; text: modelData.t; color: seg.on ? Theme.text : Theme.textDim; font.pixelSize: 12
                                        horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                }
            }
        }
        Text {
            textFormat: Text.PlainText
            Layout.fillWidth: true
            wrapMode: Text.WrapAnywhere
            color: Theme.textFaint
            font.pixelSize: 11
            text: root.source === "ssh"
                  ? qsTr("Reads %1 (Host, HostName, Port, User, IdentityFile, ProxyJump). Keys without a passphrase are added to the keychain.").arg(App.sshConfigPath())
                  : qsTr("Reads the sessions saved in PuTTY (SSH only): host, port, user and the .ppk key.")
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: 10
            color: Theme.bg
            border.color: Theme.border
            clip: true
            ListView {
                id: list
                anchors.fill: parent
                anchors.margins: 1
                model: root.items
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}
                delegate: Rectangle {
                    width: list.width
                    height: 46
                    color: index % 2 ? "transparent" : Theme.tint(Theme.surface2, "40")
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 8; anchors.rightMargin: 12
                        spacing: 8
                        AppCheckBox {
                            checked: !!root.picked[index]
                            enabled: !modelData.exists
                            onToggled: { const p = Object.assign({}, root.picked); p[index] = checked; root.picked = p }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1
                            Text { textFormat: Text.PlainText; Layout.fillWidth: true; text: modelData.label; color: modelData.exists ? Theme.textFaint : Theme.text; font.pixelSize: 13; elide: Text.ElideRight }
                            Text {
                                textFormat: Text.PlainText
                                Layout.fillWidth: true
                                elide: Text.ElideMiddle
                                color: Theme.textDim; font.pixelSize: 11; font.family: Theme.monoFont
                                text: (modelData.username ? modelData.username + "@" : "") + modelData.hostname + ":" + modelData.port
                                      + (modelData.jump ? "  via " + modelData.jump : "")
                                      + (modelData.keyFile ? "  🔑 " + modelData.keyFile.split(/[\\/]/).pop() + (modelData.keyExists ? "" : " (?)") : "")
                            }
                        }
                        Text { textFormat: Text.PlainText; visible: !!modelData.exists; text: qsTr("already added"); color: Theme.textFaint; font.pixelSize: 11 }
                    }
                }
            }
            Text {
                textFormat: Text.PlainText
                anchors.centerIn: parent
                visible: root.items.length === 0
                width: parent.width - 40
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                text: root.source === "ssh" ? qsTr("No hosts found in the OpenSSH config.") : qsTr("No PuTTY sessions found.")
                color: Theme.textFaint
            }
        }

        Text { textFormat: Text.PlainText; Layout.fillWidth: true; visible: root.result !== ""; text: root.result; color: Theme.ok; font.pixelSize: 12; wrapMode: Text.WordWrap }

        RowLayout {
            Layout.fillWidth: true
            AppButton {
                text: qsTr("Select all"); variant: "ghost"; compact: true
                enabled: root.items.length > 0
                onClicked: { const p = {}; for (let i = 0; i < root.items.length; ++i) p[i] = !root.items[i].exists; root.picked = p }
            }
            Item { Layout.fillWidth: true }
            AppButton { text: qsTr("Close"); variant: "ghost"; onClicked: root.close() }
            AppButton {
                text: qsTr("Import %1").arg(root.pickedCount); variant: "primary"; glyph: Theme.iconAdd
                enabled: root.pickedCount > 0
                onClicked: {
                    const sel = []
                    for (let i = 0; i < root.items.length; ++i) if (root.picked[i]) sel.push(root.items[i])
                    const summary = App.importHosts(sel)
                    root.load(root.source)
                    root.result = summary
                }
            }
        }
    }
}
