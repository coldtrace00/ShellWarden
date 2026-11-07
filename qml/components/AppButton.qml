import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ShellWarden

// variant: "primary" | "secondary" | "danger" | "ghost"
AbstractButton {
    id: root
    property string variant: "secondary"
    property string glyph: ""
    property bool compact: false

    implicitHeight: compact ? 30 : 36
    implicitWidth: Math.max(implicitHeight, row.implicitWidth + (compact ? 20 : 28))
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    opacity: enabled ? 1 : 0.45

    readonly property color fillColor: {
        switch (variant) {
        case "primary": return root.down ? Qt.darker(Theme.accent, 1.25) : (root.hovered ? Qt.lighter(Theme.accent, 1.1) : Theme.accent)
        case "danger": return root.down ? Qt.darker(Theme.danger, 1.3) : (root.hovered ? Theme.danger : Theme.dangerDim)
        case "ghost": return root.down ? Theme.surface2 : (root.hovered ? Theme.surfaceHover : "transparent")
        default: return root.down ? Theme.surface : (root.hovered ? Theme.surfaceHover : Theme.surface2)
        }
    }
    readonly property color labelColor: {
        switch (variant) {
        case "primary": return "#0b1214"
        case "danger": return root.hovered ? "#ffffff" : Theme.danger
        default: return Theme.text
        }
    }

    background: Rectangle {
        radius: Theme.radiusSmall
        color: root.fillColor
        border.width: root.visualFocus ? 2 : (root.variant === "secondary" ? 1 : 0)
        border.color: root.visualFocus ? Theme.accent : Theme.border
        Behavior on color { ColorAnimation { duration: 90 } }
    }

    contentItem: Item {
        RowLayout {
            id: row
            anchors.centerIn: parent
            spacing: 7
            IconLabel {
                visible: root.glyph !== ""
                glyph: root.glyph
                size: root.compact ? 12 : 13
                color: root.labelColor
            }
            Text {
                textFormat: Text.PlainText
                visible: root.text !== ""
                text: root.text
                color: root.labelColor
                font.pixelSize: root.compact ? 12 : 13
                font.weight: root.variant === "primary" ? Font.DemiBold : Font.Medium
            }
        }
    }
}
