import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ShellWarden

// Small terminal mock-up in a given colour scheme.
Rectangle {
    id: root
    property var theme: ({})
    property bool selected: false
    property bool editable: true
    signal clicked()
    signal editRequested()

    implicitWidth: 196
    implicitHeight: 90
    radius: 10
    color: theme.bg || "#000"
    border.width: selected ? 2 : 1
    border.color: selected ? Theme.accent : Theme.border

    clip: true
    // own theme: background picture under a layer of the background colour
    Image {
        anchors.fill: parent
        anchors.margins: 1
        visible: !!root.theme.bgImage
        source: !root.theme.bgImage ? ""
                : String(root.theme.bgImage).startsWith(":/") ? "qrc" + root.theme.bgImage          // built-in picture
                : String(root.theme.bgImage).startsWith("file:") ? root.theme.bgImage
                : "file:///" + String(root.theme.bgImage).replace(/^\/+/, "")
        fillMode: root.theme.bgFit ? Image.PreserveAspectFit : Image.PreserveAspectCrop
        horizontalAlignment: root.theme.bgFit ? Image.AlignRight : Image.AlignHCenter
        verticalAlignment: root.theme.bgFit ? Image.AlignVCenter : Image.AlignTop
        asynchronous: true
        sourceSize.width: 400
    }
    Rectangle { anchors.fill: parent; anchors.margins: 1; radius: root.radius; visible: !!root.theme.bgImage; color: root.theme.bg || "#000"; opacity: (root.theme.bgDim || 60) / 100 }

    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.clicked() }

    Column {
        anchors.fill: parent
        anchors.margins: 9
        spacing: 3
        Row {
            spacing: 0
            Text { textFormat: Text.PlainText; text: "user@srv"; color: root.theme.ansi ? root.theme.ansi[10] : "#0f0"; font.family: Theme.monoFont; font.pixelSize: 10; font.bold: true }
            Text { textFormat: Text.PlainText; text: ":"; color: root.theme.fg || "#fff"; font.family: Theme.monoFont; font.pixelSize: 10 }
            Text { textFormat: Text.PlainText; text: "~$ "; color: root.theme.ansi ? root.theme.ansi[12] : "#00f"; font.family: Theme.monoFont; font.pixelSize: 10; font.bold: true }
            Text { textFormat: Text.PlainText; text: "ls -la"; color: root.theme.input || root.theme.fg || "#fff"; font.family: Theme.monoFont; font.pixelSize: 10 }
        }
        Row {
            spacing: 8
            Text { textFormat: Text.PlainText; text: "docs/"; color: root.theme.ansi ? root.theme.ansi[4] : "#00f"; font.family: Theme.monoFont; font.pixelSize: 10; font.bold: true }
            Text { textFormat: Text.PlainText; text: "run.sh"; color: root.theme.ansi ? root.theme.ansi[2] : "#0f0"; font.family: Theme.monoFont; font.pixelSize: 10 }
            Text { textFormat: Text.PlainText; text: "err.log"; color: root.theme.ansi ? root.theme.ansi[1] : "#f00"; font.family: Theme.monoFont; font.pixelSize: 10 }
        }
        Row {
            spacing: 2
            Repeater {
                model: 8
                Rectangle { width: 9; height: 9; radius: 2; color: root.theme.ansi ? root.theme.ansi[index + 8] : "#888" }
            }
        }
        Item { width: 1; height: 2 }
        Text {
            textFormat: Text.PlainText
            text: root.theme.name || ""
            color: root.theme.fg || "#fff"
            font.pixelSize: 11
            font.weight: Font.DemiBold
        }
    }
    Rectangle {
        visible: root.selected
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 6
        width: 18; height: 18; radius: 9
        color: Theme.accent
        IconLabel { anchors.centerIn: parent; glyph: Theme.iconCheck; size: 11; color: "#0b1214" }
    }
    // edit button for own themes
    AbstractButton {
        id: editBtn
        visible: root.editable && !!root.theme.custom
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 6
        width: 24; height: 24
        hoverEnabled: true
        onClicked: root.editRequested()
        ToolTip.visible: hovered
        ToolTip.text: qsTr("Edit theme")
        background: Rectangle { radius: 6; color: editBtn.hovered ? Theme.surfaceHover : Theme.surface2; border.color: Theme.border }
        contentItem: Item { IconLabel { anchors.centerIn: parent; glyph: Theme.iconEdit; size: 12; color: Theme.text } }
    }
}
