import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ShellWarden

Item {
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 28
        spacing: 20

        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text { text: qsTr("Known hosts"); color: Theme.text; font.pixelSize: 24; font.weight: Font.DemiBold }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WrapAnywhere
                    text: qsTr("Trusted server keys (OpenSSH format): %1").arg(App.knownHostsPath)
                    color: Theme.textDim; font.pixelSize: 12
                }
            }
            AppButton { text: qsTr("Refresh"); glyph: Theme.iconRefresh; onClicked: App.refreshKnownHosts() }
        }

        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: App.knownHosts.length > 0
            clip: true
            spacing: 8
            model: App.knownHosts
            ScrollBar.vertical: ScrollBar {}
            delegate: Rectangle {
                width: ListView.view.width
                height: 64
                radius: Theme.radius
                color: Theme.surface
                border.color: Theme.border
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 14
                    IconLabel { glyph: Theme.iconFingerprint; size: 18; color: Theme.accent }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 3
                        Text {
                            Layout.fillWidth: true
                            text: modelData.hosts.indexOf("|1|") === 0 ? qsTr("(hashed host name)") : modelData.hosts
                            color: Theme.text; font.pixelSize: 13; font.family: Theme.monoFont
                            elide: Text.ElideRight
                        }
                        Text {
                            text: modelData.keyType + "   " + modelData.fingerprint
                            color: Theme.textDim; font.pixelSize: 11; font.family: Theme.monoFont
                        }
                    }
                    AppButton {
                        text: qsTr("Remove"); glyph: Theme.iconDelete; variant: "danger"; compact: true
                        onClicked: App.removeKnownHost(modelData.line)
                    }
                }
            }
        }

        Item {
            visible: App.knownHosts.length === 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            ColumnLayout {
                anchors.centerIn: parent
                spacing: 10
                IconLabel { Layout.alignment: Qt.AlignHCenter; glyph: Theme.iconFingerprint; size: 40; color: Theme.textFaint }
                Text { Layout.alignment: Qt.AlignHCenter; text: qsTr("No trusted servers yet"); color: Theme.text; font.pixelSize: 15 }
                Text { Layout.alignment: Qt.AlignHCenter; text: qsTr("Keys are added here after you confirm a server's fingerprint."); color: Theme.textDim; font.pixelSize: 12 }
            }
        }
    }
}
