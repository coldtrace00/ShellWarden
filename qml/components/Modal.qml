import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ShellWarden

// Centered dialog card with a title and a content column.
Popup {
    id: root
    property string title: ""
    property string subtitle: ""
    default property alias content: body.data

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(520, (parent ? parent.width : 520) - 40)
    modal: true
    focus: true
    padding: 24
    closePolicy: Popup.CloseOnEscape

    Overlay.modal: Rectangle { color: "#a0070910" }

    background: Rectangle {
        radius: 14
        color: Theme.surface
        border.color: Theme.border
        border.width: 1
    }

    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 120 }
        NumberAnimation { property: "scale"; from: 0.97; to: 1; duration: 120; easing.type: Easing.OutCubic }
    }

    contentItem: ColumnLayout {
        spacing: 16
        ColumnLayout {
            spacing: 4
            Layout.fillWidth: true
            Text {
                text: root.title
                color: Theme.text
                font.pixelSize: 18
                font.weight: Font.DemiBold
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
            Text {
                visible: root.subtitle !== ""
                text: root.subtitle
                color: Theme.textDim
                font.pixelSize: 12
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
        }
        ColumnLayout {
            id: body
            spacing: 12
            Layout.fillWidth: true
        }
    }
}
