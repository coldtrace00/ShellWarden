import QtQuick
import QtQuick.Controls
import ShellWarden

Switch {
    id: root
    implicitWidth: 44
    implicitHeight: 26
    padding: 0
    spacing: 0
    indicator: Rectangle {
        implicitWidth: 44
        implicitHeight: 24
        x: root.leftPadding
        y: (root.height - height) / 2
        radius: 12
        color: root.checked ? Theme.accent : Theme.surface2
        border.color: root.checked ? Theme.accent : Theme.border
        Behavior on color { ColorAnimation { duration: 120 } }
        Rectangle {
            width: 18; height: 18; radius: 9
            y: 3
            x: root.checked ? parent.width - width - 3 : 3
            color: root.checked ? "#0b1214" : Theme.textDim
            Behavior on x { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
        }
    }
    contentItem: Item {}
}
