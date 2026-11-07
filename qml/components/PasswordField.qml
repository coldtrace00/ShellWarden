import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ShellWarden

// Password / passphrase input with:
//  • generator (cryptographically random, 19+ characters)
//  • strength meter
//  • weak-password policy: manual passwords may be 8+ characters, but anything
//    under 19 characters or weak needs an explicit "I accept the risk".
// `acceptable` tells the parent whether the value may be used.
ColumnLayout {
    id: root
    property alias text: field.text
    property string label: ""
    property string placeholder: ""
    property bool allowEmpty: false          // e.g. key without passphrase
    property string emptyWarning: ""
    property int generateLength: 24
    property bool lengthSlider: true         // let the user pick the generated length (19–64)
    readonly property int minLength: 8
    readonly property int goodLength: 19
    property bool showConfirm: true
    readonly property alias confirmText: confirm.text
    property bool consent: false
    property bool generated: false
    signal accepted()

    readonly property var strength: field.text.length > 0 ? App.evaluatePassword(field.text) : ({ score: 0, reliable: false, hints: "", label: "" })
    readonly property bool tooShort: field.text.length > 0 && field.text.length < 8
    readonly property bool weak: field.text.length > 0 && !strength.reliable && !generated
    readonly property bool matches: !showConfirm || confirm.text === field.text
    readonly property bool needsConsent: (weak && !tooShort) || (field.text.length === 0 && allowEmpty)
    readonly property bool acceptable: {
        if (field.text.length === 0)
            return allowEmpty && consent
        if (tooShort || !matches)
            return false
        return !weak || consent
    }

    function generate() {
        const pw = App.generatePassword(Math.max(19, generateLength), true)
        field.text = pw
        confirm.text = pw
        field.revealed = true
        root.generated = true
        root.consent = false
    }
    function focusInput() { field.field.forceActiveFocus() }
    function clear() {
        field.text = ""
        confirm.text = ""
        field.revealed = false
        generated = false
        consent = false
    }

    spacing: 8

    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        AppField {
            id: field
            Layout.fillWidth: true
            label: root.label
            placeholder: root.placeholder
            secret: true
            mono: field.revealed
            onAccepted: root.showConfirm ? confirm.field.forceActiveFocus() : root.accepted()
        }
        Connections {
            target: field.field
            function onTextEdited() { root.generated = false; root.consent = false }
        }
        AppButton {
            Layout.alignment: Qt.AlignBottom
            glyph: Theme.iconGenerate
            text: qsTr("Generate")
            onClicked: root.generate()
            ToolTip.visible: hovered
            ToolTip.delay: 400
            ToolTip.text: qsTr("Random %1-character password").arg(Math.max(19, root.generateLength))
        }
        AppButton {
            Layout.alignment: Qt.AlignBottom
            visible: field.text.length > 0
            glyph: Theme.iconCopy
            variant: "ghost"
            onClicked: App.copyText(field.text, true)
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Copy (clipboard is cleared after 30 s)")
        }
    }

    // length of generated passwords (optional): a plain number
    RowLayout {
        Layout.fillWidth: true
        visible: root.lengthSlider
        spacing: 10
        Text { text: qsTr("Length for \"Generate\":"); color: Theme.textDim; font.pixelSize: 12 }
        AppSpin {
            id: genLen
            from: 19; to: 64
            editable: true
            value: root.generateLength
            onValueModified: { root.generateLength = value; if (root.generated) root.generate() }
        }
        Text { text: qsTr("characters (19–64)"); color: Theme.textFaint; font.pixelSize: 12; Layout.fillWidth: true }
    }

    // strength meter
    ColumnLayout {
        Layout.fillWidth: true
        visible: field.text.length > 0
        spacing: 4
        RowLayout {
            Layout.fillWidth: true
            spacing: 4
            Repeater {
                model: 5
                Rectangle {
                    Layout.fillWidth: true
                    height: 4
                    radius: 2
                    color: index <= root.strength.score
                           ? [Theme.danger, Theme.danger, Theme.warn, Theme.accent, Theme.ok][root.strength.score]
                           : Theme.surface2
                    Behavior on color { ColorAnimation { duration: 150 } }
                }
            }
        }
        Text {
            readonly property int len: field.text.length
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            font.pixelSize: 12
            color: root.generated || len >= root.goodLength ? Theme.ok : (len < root.minLength ? Theme.danger : Theme.warn)
            text: root.generated
                  ? qsTr("Generated: %1 characters. Save it somewhere safe.").arg(len)
                  : len < root.minLength
                    ? qsTr("%1 characters — too short, the minimum is %2.").arg(len).arg(root.minLength)
                    : len < root.goodLength
                      ? qsTr("%1 characters — unsafe. A safe password has %2 or more characters.").arg(len).arg(root.goodLength)
                      : qsTr("%1 characters — safe length.").arg(len) + (root.strength.hints ? " " + root.strength.hints : "")
        }
    }

    AppField {
        id: confirm
        visible: root.showConfirm && field.text.length > 0
        Layout.fillWidth: true
        label: qsTr("Repeat")
        secret: true
        revealed: field.revealed
        onAccepted: root.accepted()
    }
    Text {
        visible: root.showConfirm && confirm.text.length > 0 && !root.matches
        text: qsTr("Does not match.")
        color: Theme.danger
        font.pixelSize: 11
    }

    // Weak password: explicit consent
    Rectangle {
        Layout.fillWidth: true
        visible: root.needsConsent
        radius: 8
        color: Theme.warnDim
        border.color: Theme.warn
        implicitHeight: warnCol.implicitHeight + 20
        ColumnLayout {
            id: warnCol
            anchors.fill: parent
            anchors.margins: 10
            spacing: 8
            RowLayout {
                spacing: 8
                Layout.fillWidth: true
                IconLabel { glyph: Theme.iconWarning; color: Theme.warn; size: 16 }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.text
                    font.pixelSize: 12
                    text: field.text.length === 0
                          ? (root.emptyWarning || qsTr("No password: anyone with the file can use it."))
                          : qsTr("Unsafe password. A safe password has 19 or more characters.")
                }
            }
            AppButton {
                text: root.consent ? qsTr("✓ I accept the risk") : qsTr("I accept the risk")
                variant: root.consent ? "secondary" : "danger"
                compact: true
                onClicked: root.consent = !root.consent
            }
        }
    }
}
