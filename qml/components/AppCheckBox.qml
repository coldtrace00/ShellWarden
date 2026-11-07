import QtQuick
import QtQuick.Controls
import ShellWarden

// Dark check box in the app style (the stock one is a white square on Windows).
CheckBox {
    id: root
    spacing: 10
    padding: 2
    indicator: Rectangle {
        x: root.leftPadding
        y: root.topPadding + (root.availableHeight - height) / 2
        width: 18; height: 18; radius: 5
        color: root.checked ? Theme.accent : Theme.surface2
        border.color: root.checked ? Theme.accent : (root.hovered ? Theme.textDim : Theme.border)
        opacity: root.enabled ? 1 : 0.5
        IconLabel { anchors.centerIn: parent; visible: root.checked; glyph: Theme.iconCheck; size: 12; color: "#0b1214" }
    }
    contentItem: Text {
        textFormat: Text.PlainText
        leftPadding: root.indicator.width + root.spacing
        text: root.text
        color: root.enabled ? Theme.text : Theme.textFaint
        font.pixelSize: 13
        wrapMode: Text.WordWrap
        verticalAlignment: Text.AlignVCenter
    }
}
