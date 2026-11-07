import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ShellWarden

Modal {
    id: root
    signal created()
    title: qsTr("Generate SSH key")
    subtitle: qsTr("The private key is stored in the vault, encrypted with its passphrase (OpenSSH format).")
    width: Math.min(580, (parent ? parent.width : 580) - 40)

    function openFresh() {
        name.text = ""
        comment.text = ""
        pass.clear()
        typeBox.currentIndex = 0
        savePass.checked = true
        err.text = ""
        open()
        name.field.forceActiveFocus()
    }

    AppField { id: name; Layout.fillWidth: true; label: qsTr("Name"); placeholder: qsTr("e.g. Work laptop") }

    RowLayout {
        Layout.fillWidth: true
        spacing: 10
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 5
            Text { text: qsTr("Type"); color: Theme.textDim; font.pixelSize: 12; font.weight: Font.Medium }
            AppCombo {
                id: typeBox
                Layout.fillWidth: true
                model: [
                    qsTr("Ed25519 (recommended)"),
                    qsTr("ECDSA P-256"),
                    qsTr("ECDSA P-384"),
                    qsTr("ECDSA P-521"),
                    qsTr("RSA 3072"),
                    qsTr("RSA 4096 (slow to generate)")
                ]
            }
        }
        AppField {
            id: comment
            Layout.fillWidth: true
            label: qsTr("Comment")
            placeholder: qsTr("Defaults to the name")
        }
    }

    PasswordField {
        id: pass
        Layout.fillWidth: true
        label: qsTr("Passphrase")
        placeholder: qsTr("Strongly recommended")
        allowEmpty: true
        emptyWarning: qsTr("Without a passphrase anyone who gets the exported key file can use it.")
    }

    AppCheckBox {
        id: savePass
        visible: pass.text.length > 0
        text: qsTr("Save passphrase in the vault (no prompt on connect)")
        checked: true
    }

    Text { id: err; visible: text !== ""; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.danger; font.pixelSize: 12 }

    RowLayout {
        Layout.fillWidth: true
        Item { Layout.fillWidth: true }
        AppButton { text: qsTr("Cancel"); variant: "ghost"; onClicked: root.close() }
        AppButton {
            text: qsTr("Generate key"); glyph: Theme.iconKey; variant: "primary"
            enabled: pass.acceptable
            onClicked: {
                err.text = ""
                const label = name.text.trim()
                const e = App.generateKey(label, typeBox.currentIndex,
                                          comment.text.trim() !== "" ? comment.text.trim() : label,
                                          pass.text, pass.text.length > 0 ? pass.confirmText : "",
                                          savePass.checked)
                if (e !== "") { err.text = e; return }
                pass.clear()
                root.close()
                root.created()
            }
        }
    }

    onClosed: pass.clear()
}
