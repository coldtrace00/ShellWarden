import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ShellWarden

// Menu item with a fixed slot on the left (icon or check mark), so plain and
// checkable items line up in one column.
MenuItem {
    id: item
    property string glyph: ""
    property string shortcutText: ""
    implicitHeight: 36
    leftPadding: 10
    rightPadding: 12
    indicator: null
    arrow: null
    implicitWidth: row.implicitWidth + leftPadding + rightPadding
    contentItem: RowLayout {
        id: row
        spacing: 10
        Item {
            implicitWidth: 16; implicitHeight: 16
            IconLabel {
                anchors.centerIn: parent
                visible: (item.checkable && item.checked) || item.glyph !== ""
                glyph: item.checkable && item.checked ? Theme.iconCheck : item.glyph
                size: 13
                color: item.checkable && item.checked ? Theme.accent : Theme.textDim
            }
        }
        Text {
            textFormat: Text.PlainText
            Layout.fillWidth: true
            text: item.text
            color: item.enabled ? Theme.text : Theme.textFaint
            font.pixelSize: 13
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }
        Text { textFormat: Text.PlainText;   // submenu arrow
            visible: item.subMenu !== null
            text: "›"
            color: Theme.textDim
            font.pixelSize: 16
            Layout.leftMargin: 12
        }
        Text {
            textFormat: Text.PlainText
            visible: item.shortcutText !== ""
            text: item.shortcutText
            color: Theme.textFaint
            font.pixelSize: 11
            Layout.leftMargin: 12
        }
    }
    background: Rectangle {
        radius: 6
        color: item.highlighted ? Theme.surfaceHover : "transparent"
    }
}
