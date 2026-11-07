import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ShellWarden

// Number field with − / + that looks the same everywhere (the stock SpinBox
// turns white inside popups on Windows). Same API as SpinBox for what we use.
Rectangle {
    id: root
    property int from: 0
    property int to: 99
    property int stepSize: 1
    property int value: 0
    property bool editable: true
    signal valueModified()

    function setValue(v) {
        v = Math.max(from, Math.min(to, v))
        if (v !== value) { value = v; valueModified() }
        field.text = String(value)
    }
    onValueChanged: if (!field.activeFocus) field.text = String(value)

    implicitWidth: Math.max(112, field.implicitWidth + 64)
    implicitHeight: 32
    radius: 8
    color: Theme.surface2
    border.color: field.activeFocus ? Theme.accent : Theme.border
    opacity: enabled ? 1 : 0.5

    component StepBtn: AbstractButton {
        id: sb
        property string glyph: ""
        Layout.fillHeight: true
        implicitWidth: 30
        hoverEnabled: true
        autoRepeat: true
        background: Rectangle { radius: 7; color: sb.pressed ? Theme.surfaceHover : sb.hovered ? Theme.tint(Theme.surfaceHover, "aa") : "transparent" }
        contentItem: Text { textFormat: Text.PlainText; text: sb.glyph; color: sb.enabled ? Theme.text : Theme.textFaint; font.pixelSize: 15
                            horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 1
        spacing: 0
        StepBtn { glyph: "−"; enabled: root.value > root.from; onClicked: root.setValue(root.value - root.stepSize) }
        TextField {
            id: field
            Layout.fillWidth: true
            Layout.fillHeight: true
            readOnly: !root.editable
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            color: Theme.text
            font.pixelSize: 13
            font.family: Theme.monoFont
            text: String(root.value)
            selectByMouse: true
            validator: IntValidator { bottom: root.from; top: root.to }
            background: null
            padding: 0
            onEditingFinished: root.setValue(parseInt(text) || root.from)
            Keys.onUpPressed: root.setValue(root.value + root.stepSize)
            Keys.onDownPressed: root.setValue(root.value - root.stepSize)
            WheelHandler { onWheel: (e) => root.setValue(root.value + (e.angleDelta.y > 0 ? root.stepSize : -root.stepSize)) }
        }
        StepBtn { glyph: "+"; enabled: root.value < root.to; onClicked: root.setValue(root.value + root.stepSize) }
    }
}
