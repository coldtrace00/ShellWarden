import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ShellWarden

// Asks for the master password for one action. The action is a function that
// gets the password and returns an error text ("" = done, the box closes).
//   masterPrompt.ask(qsTr("Export protected key"), qsTr("…"), qsTr("Export"), pw => App.doIt(pw))
Modal {
    id: root
    property var action: null
    property string button: ""
    closePolicy: Popup.CloseOnEscape

    function ask(title, subtitle, button, action) {
        root.title = title
        root.subtitle = subtitle
        root.button = button
        root.action = action
        mpField.text = ""
        mpErr.text = ""
        open()
        mpField.field.forceActiveFocus()
    }
    function submit() {
        if (!root.action)
            return
        const e = root.action(mpField.text)
        mpField.text = ""
        if (e && e !== "") {
            mpErr.text = e
            mpField.field.forceActiveFocus()
            return
        }
        close()
    }
    onClosed: { mpField.text = ""; root.action = null }

    AppField {
        id: mpField
        Layout.fillWidth: true
        label: qsTr("Master password")
        secret: true
        onAccepted: root.submit()
    }
    Text { textFormat: Text.PlainText; id: mpErr; visible: text !== ""; color: Theme.danger; font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.WordWrap }
    RowLayout {
        Layout.fillWidth: true
        Item { Layout.fillWidth: true }
        AppButton { text: qsTr("Cancel"); variant: "ghost"; onClicked: root.close() }
        AppButton { text: root.button; variant: "primary"; onClicked: root.submit() }
    }
}
