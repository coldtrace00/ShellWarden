import QtQuick
import QtQuick.Controls
import ShellWarden

// ComboBox with a dark popup where the row under the mouse is the one that
// lights up (the Basic style highlights the current row instead, which is
// confusing on a dark palette). The selected row is marked with a check.
ComboBox {
    id: control
    implicitHeight: 40
    font.pixelSize: 13

    // Override to customise the text of a row (e.g. a font preview).
    property var rowFont: null

    delegate: ItemDelegate {
        id: row
        width: ListView.view ? ListView.view.width : control.width
        implicitHeight: 34
        highlighted: control.highlightedIndex === index
        hoverEnabled: true
        leftPadding: 12
        rightPadding: 28
        contentItem: Text {
            textFormat: Text.PlainText
            text: control.textAt(index)
            color: index === control.currentIndex ? Theme.accent : Theme.text
            font.pixelSize: 13
            font.family: control.rowFont ? control.rowFont(index) : control.font.family
            font.weight: index === control.currentIndex ? Font.DemiBold : Font.Normal
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: Theme.radiusSmall
            color: row.highlighted || row.hovered ? Theme.surfaceHover : "transparent"
        }
        IconLabel {
            visible: index === control.currentIndex
            anchors.right: parent.right
            anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            glyph: Theme.iconCheck
            size: 13
            color: Theme.accent
        }
    }

    popup: Popup {
        y: control.height + 4
        width: Math.max(control.width, 180)
        implicitHeight: Math.min(contentItem.implicitHeight + topPadding + bottomPadding, 360)
        padding: 4
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            boundsBehavior: Flickable.StopAtBounds
            ScrollIndicator.vertical: ScrollIndicator {}
        }
        background: Rectangle {
            radius: 8
            color: Theme.surface
            border.color: Theme.border
        }
    }
}
