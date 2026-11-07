import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ShellWarden

// Labelled text field. `secret: true` gives a password field with a
// show/hide toggle.
ColumnLayout {
    id: root
    property alias text: field.text
    property alias placeholder: field.placeholderText
    property alias field: field
    property string label: ""
    property string hint: ""
    property bool secret: false
    property bool mono: false
    property bool revealed: false
    property int extraRightPadding: 0
    signal accepted()

    spacing: 5

    Text {
        visible: root.label !== ""
        text: root.label
        color: Theme.textDim
        font.pixelSize: 12
        font.weight: Font.Medium
    }

    TextField {
        id: field
        Layout.fillWidth: true
        implicitHeight: 38
        color: Theme.text
        placeholderTextColor: Theme.textFaint
        selectionColor: Theme.accentDim
        selectedTextColor: Theme.text
        font.pixelSize: 13
        font.family: root.mono ? Theme.monoFont : Qt.application.font.family
        echoMode: root.secret && !root.revealed ? TextInput.Password : TextInput.Normal
        leftPadding: 12
        rightPadding: (root.secret ? 40 : 12) + root.extraRightPadding
        selectByMouse: true
        onAccepted: root.accepted()
        background: Rectangle {
            radius: Theme.radiusSmall
            color: Theme.surface2
            border.width: field.activeFocus ? 2 : 1
            border.color: field.activeFocus ? Theme.accent : Theme.border
        }

        AbstractButton {
            id: eyeBtn
            visible: root.secret
            anchors.right: parent.right
            anchors.rightMargin: 5
            anchors.verticalCenter: parent.verticalCenter
            width: 30; height: 28
            hoverEnabled: true
            focusPolicy: Qt.NoFocus
            onClicked: root.revealed = !root.revealed
            background: Rectangle {
                radius: 6
                color: eyeBtn.down ? Theme.surface : (eyeBtn.hovered ? Theme.surfaceHover : "transparent")
            }
            contentItem: Item {
                IconLabel {
                    anchors.centerIn: parent
                    glyph: root.revealed ? Theme.iconEyeOff : Theme.iconEye
                    size: 17
                    color: root.revealed ? Theme.accent : (eyeBtn.hovered ? Theme.text : Theme.textDim)
                }
            }
            ToolTip.visible: hovered
            ToolTip.delay: 400
            ToolTip.text: root.revealed ? qsTr("Hide") : qsTr("Show")
        }
    }

    Text {
        visible: root.hint !== ""
        text: root.hint
        color: Theme.textFaint
        font.pixelSize: 11
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
    }
}
