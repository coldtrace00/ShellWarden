import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ShellWarden

AbstractButton {
    id: root
    property string glyph: ""
    property bool active: false
    property string badge: ""
    property bool compact: false      // collapsed sidebar: icon only

    implicitHeight: 40
    hoverEnabled: true
    Layout.fillWidth: true
    opacity: enabled ? 1 : 0.4

    ToolTip.visible: compact && hovered
    ToolTip.delay: 250
    ToolTip.text: text

    background: Rectangle {
        radius: 9
        color: root.active ? Theme.surface2 : (root.hovered ? Theme.surface : "transparent")
        Behavior on color { ColorAnimation { duration: 100 } }
        Rectangle {
            visible: root.active
            width: 3; height: 18; radius: 2
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            color: Theme.accent
        }
    }
    contentItem: Item {
        IconLabel {
            id: ic
            x: root.compact ? (parent.width - width) / 2 : 12
            anchors.verticalCenter: parent.verticalCenter
            glyph: root.glyph
            size: 18
            color: root.active ? Theme.accent : (root.hovered ? Theme.text : Theme.textDim)
            Behavior on x { NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
        }
        Text {
            anchors.left: ic.right
            anchors.leftMargin: 12
            anchors.right: badgeBox.visible ? badgeBox.left : parent.right
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            visible: !root.compact
            text: root.text
            color: root.active ? Theme.text : Theme.textDim
            font.pixelSize: 13
            font.weight: root.active ? Font.DemiBold : Font.Normal
            elide: Text.ElideRight
        }
        Rectangle {
            id: badgeBox
            visible: root.badge !== "" && !root.compact
            anchors.right: parent.right
            anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            radius: 8
            color: Theme.bg
            implicitWidth: badgeText.implicitWidth + 12
            implicitHeight: 18
            Text {
                id: badgeText
                anchors.centerIn: parent
                text: root.badge
                color: Theme.textDim
                font.pixelSize: 11
            }
        }
        // small dot for the badge when collapsed
        Rectangle {
            visible: root.badge !== "" && root.compact
            width: 6; height: 6; radius: 3
            color: Theme.accent
            anchors.top: ic.top
            anchors.left: ic.right
            anchors.leftMargin: -3
        }
    }
}
