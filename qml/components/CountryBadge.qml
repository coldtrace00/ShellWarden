import QtQuick
import QtQuick.Layouts
import ShellWarden

// Small flag + country name for an ISO country code ("UA", "DE", …).
RowLayout {
    id: root
    property string code: ""
    property bool showName: true
    property color textColor: Theme.textDim
    property int pixelSize: 11
    readonly property int flag: App.countryFlag(code)   // index in the flag sheet
    readonly property string name: { App.language; return App.countryName(code) }
    visible: code !== "" && (flag >= 0 || name !== "")
    spacing: 5

    Rectangle {
        visible: root.flag >= 0
        implicitWidth: 18; implicitHeight: 13
        radius: 2
        color: "transparent"
        border.width: 1
        border.color: "#30ffffff"
        clip: true
        Image {
            anchors.fill: parent
            anchors.margins: 1
            // one sheet with every flag: 16 per row, 40x30 each
            source: "qrc:/assets/flags.png"
            sourceClipRect: Qt.rect((root.flag % 16) * 40, Math.floor(root.flag / 16) * 30, 40, 30)
            smooth: true
            mipmap: true
        }
    }
    Text {
        textFormat: Text.PlainText
        visible: root.showName && root.name !== ""
        text: root.name
        color: root.textColor
        font.pixelSize: root.pixelSize
    }
}
