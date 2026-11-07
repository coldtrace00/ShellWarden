import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import ShellWarden

// Activity log: connections, routes, IPs seen by servers, vault/key changes,
// kill switch events. Never contains secrets.
Item {
    id: root
    readonly property var log: App.log

    readonly property var filters: [
        { key: "", label: qsTr("All") },
        { key: "level:problems", label: qsTr("Problems") },
        { key: "cat:session", label: qsTr("Sessions") },
        { key: "cat:killswitch", label: qsTr("Kill switch") },
        { key: "cat:vault", label: qsTr("Vault") },
        { key: "cat:key", label: qsTr("Keys") },
        { key: "cat:sftp", label: qsTr("Files") },
        { key: "cat:tunnel", label: qsTr("Tunnels") }
    ]

    function levelColor(l) {
        switch (l) {
        case 1: return Theme.ok
        case 2: return Theme.warn
        case 3: return Theme.danger
        case 4: return Theme.blue
        default: return Theme.textFaint
        }
    }
    function catGlyph(c) {
        switch (c) {
        case "session": return Theme.iconTerminal
        case "killswitch": return Theme.iconShield
        case "vault": return Theme.iconLock
        case "key": return Theme.iconKey
        case "sftp": return Theme.iconFolder
        case "tunnel": return Theme.iconTunnel
        case "proxy": return Theme.iconGlobe
        case "host": return Theme.iconServer
        case "snippet": return Theme.iconSnippet
        default: return Theme.iconInfo
        }
    }

    FileDialog {
        id: exportDlg
        fileMode: FileDialog.SaveFile
        nameFilters: ["CSV (*.csv)"]
        selectedFile: App.suggestedFile("activity-log.csv")
        onAccepted: {
            const e = root.log.exportTo(selectedFile)
            if (e !== "") console.warn(e)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 28
        spacing: 16

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text { textFormat: Text.PlainText; text: qsTr("Activity log"); color: Theme.text; font.pixelSize: 24; font.weight: Font.DemiBold }
                Text {
                    textFormat: Text.PlainText
                    Layout.fillWidth: true
                    Layout.preferredWidth: 100
                    text: App.settings.logEnabled ? qsTr("Encrypted on disk, readable only while the vault is open; never contains passwords or keys: %1").arg(root.log.directory)
                                                  : qsTr("Logging is turned off in Settings.")
                    color: App.settings.logEnabled ? Theme.textDim : Theme.warn
                    font.pixelSize: 12
                    elide: Text.ElideMiddle
                }
            }
            TextField {
                id: search
                Layout.fillWidth: true
                Layout.maximumWidth: 220
                Layout.minimumWidth: 90
                implicitWidth: 220
                implicitHeight: 36
                placeholderText: qsTr("Search")
                placeholderTextColor: Theme.textFaint
                color: Theme.text
                leftPadding: 32
                onTextChanged: root.log.filter = text
                background: Rectangle {
                    radius: Theme.radiusSmall
                    color: Theme.surface
                    border.color: search.activeFocus ? Theme.accent : Theme.border
                    IconLabel { anchors.left: parent.left; anchors.leftMargin: 10; anchors.verticalCenter: parent.verticalCenter
                                glyph: Theme.iconSearch; size: 14; color: Theme.textDim }
                }
            }
            AppButton { text: root.width < 760 ? "" : qsTr("Export CSV"); glyph: Theme.iconSave; onClicked: exportDlg.open() }
            AppButton { glyph: Theme.iconDelete; variant: "ghost"; onClicked: clearDlg.open()
                        ToolTip.visible: hovered; ToolTip.text: qsTr("Clear log") }
        }

        Flow {
            Layout.fillWidth: true
            spacing: 6
            Repeater {
                model: root.filters
                delegate: AbstractButton {
                    id: chip
                    readonly property bool on: root.log.filter === modelData.key
                    implicitHeight: 28
                    implicitWidth: chipText.implicitWidth + 22
                    hoverEnabled: true
                    onClicked: { search.text = ""; root.log.filter = modelData.key }
                    background: Rectangle {
                        radius: 14
                        color: chip.on ? Theme.accentDim : (chip.hovered ? Theme.surface2 : Theme.surface)
                        border.color: chip.on ? Theme.accent : Theme.border
                    }
                    contentItem: Text { textFormat: Text.PlainText; id: chipText; text: modelData.label; color: chip.on ? Theme.text : Theme.textDim
                                        font.pixelSize: 12; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: Theme.radius
            color: Theme.surface
            border.color: Theme.border
            clip: true

            ListView {
                id: list
                anchors.fill: parent
                anchors.margins: 1
                model: root.log
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}
                delegate: Rectangle {
                    id: row
                    width: list.width
                    height: Math.max(36, body.implicitHeight + 14)
                    color: index % 2 ? "transparent" : Theme.tint(Theme.surface2, "40")
                    Rectangle {
                        x: 14; y: 6; width: 3; height: row.height - 12; radius: 2
                        color: root.levelColor(model.level)
                    }
                    Text {
                        textFormat: Text.PlainText
                        x: 29; anchors.verticalCenter: parent.verticalCenter
                        text: model.time; color: Theme.textFaint; font.pixelSize: 11; font.family: Theme.monoFont
                    }
                    IconLabel {
                        x: 181; anchors.verticalCenter: parent.verticalCenter
                        glyph: root.catGlyph(model.category); size: 14; color: root.levelColor(model.level)
                    }
                    Column {
                        id: body
                        x: 207
                        width: row.width - x - 14
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 1
                        Row {
                            spacing: 8
                            width: parent.width
                            Text {
                                textFormat: Text.PlainText
                                id: msg
                                width: Math.min(implicitWidth, parent.width - (hostTxt.visible ? hostTxt.implicitWidth + 8 : 0))
                                text: model.message; color: Theme.text; font.pixelSize: 12; elide: Text.ElideRight
                                font.weight: model.level >= 2 ? Font.DemiBold : Font.Normal
                            }
                            Text { textFormat: Text.PlainText; id: hostTxt; visible: model.host !== ""; text: model.host; color: Theme.accent; font.pixelSize: 11
                                   anchors.verticalCenter: msg.verticalCenter }
                        }
                        Text {
                            textFormat: Text.PlainText
                            visible: model.detail !== ""
                            width: parent.width
                            text: model.detail
                            color: Theme.textDim
                            font.pixelSize: 11
                            font.family: Theme.monoFont
                            wrapMode: Text.WrapAnywhere
                            maximumLineCount: 3
                            elide: Text.ElideRight
                        }
                    }
                }
            }
            Text {
                textFormat: Text.PlainText
                anchors.centerIn: parent
                visible: list.count === 0
                text: qsTr("Nothing here yet")
                color: Theme.textFaint
            }
        }
    }

    Modal {
        id: clearDlg
        title: qsTr("Clear the activity log?")
        subtitle: qsTr("All log files are deleted. This cannot be undone.")
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            AppButton { text: qsTr("Cancel"); variant: "ghost"; onClicked: clearDlg.close() }
            AppButton { text: qsTr("Clear"); variant: "danger"; onClicked: { root.log.clearAll(); clearDlg.close() } }
        }
    }
}
