import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import ShellWarden

Modal {
    id: root
    signal created()
    title: qsTr("Import private key")
    subtitle: qsTr("OpenSSH, PEM and PuTTY .ppk (v2/v3) keys are supported. The key keeps its own passphrase; .ppk is converted to OpenSSH.")
    width: Math.min(600, (parent ? parent.width : 600) - 40)
    property bool needPassphrase: false

    function openFresh() {
        name.text = ""
        keyText.text = ""
        pass.text = ""
        savePass.checked = true
        needPassphrase = false
        err.text = ""
        open()
    }

    AppField { id: name; Layout.fillWidth: true; label: qsTr("Name"); placeholder: qsTr("e.g. Server deploy key") }

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 5
        RowLayout {
            Layout.fillWidth: true
            Text { text: qsTr("Private key"); color: Theme.textDim; font.pixelSize: 12; font.weight: Font.Medium; Layout.fillWidth: true }
            AppButton { text: qsTr("Load from file…"); glyph: Theme.iconFolder; compact: true; onClicked: openDlg.open() }
        }
        ScrollView {
            Layout.fillWidth: true
            Layout.preferredHeight: 160
            TextArea {
                id: keyText
                placeholderText: "-----BEGIN OPENSSH PRIVATE KEY-----\n…"
                placeholderTextColor: Theme.textFaint
                color: Theme.text
                font.family: Theme.monoFont
                font.pixelSize: 11
                wrapMode: TextEdit.WrapAnywhere
                selectByMouse: true
                background: Rectangle { radius: Theme.radiusSmall; color: Theme.surface2; border.color: keyText.activeFocus ? Theme.accent : Theme.border }
            }
        }
    }

    AppField {
        id: pass
        visible: root.needPassphrase
        Layout.fillWidth: true
        label: qsTr("Key passphrase")
        secret: true
        onAccepted: importBtn.clicked()
    }
    AppCheckBox {
        id: savePass
        visible: root.needPassphrase
        text: qsTr("Save passphrase in the vault")
        checked: true
    }

    Text { id: err; visible: text !== ""; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.danger; font.pixelSize: 12 }

    RowLayout {
        Layout.fillWidth: true
        Item { Layout.fillWidth: true }
        AppButton { text: qsTr("Cancel"); variant: "ghost"; onClicked: root.close() }
        AppButton {
            id: importBtn
            text: qsTr("Import"); glyph: Theme.iconKey; variant: "primary"
            enabled: keyText.text.trim().length > 0
            onClicked: {
                err.text = ""
                const r = App.importKey(name.text, keyText.text, pass.text, savePass.checked)
                if (r.needPassphrase) {
                    root.needPassphrase = true
                    err.text = r.error
                    pass.field.forceActiveFocus()
                    return
                }
                if (r.error) { err.text = r.error; return }
                keyText.text = ""; pass.text = ""
                root.close()
                root.created()
            }
        }
    }

    FileDialog {
        id: openDlg
        title: qsTr("Choose a private key")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("Private keys (id_* *.pem *.key *.ppk)"), qsTr("All files (*)")]
        currentFolder: App.suggestedFile("")
        onAccepted: {
            const t = App.readTextFile(selectedFile)
            if (t === "") { err.text = qsTr("Could not read the file."); return }
            keyText.text = t
            if (name.text === "") {
                const path = String(selectedFile)
                name.text = decodeURIComponent(path.substring(path.lastIndexOf("/") + 1))
            }
        }
    }

    onClosed: { keyText.text = ""; pass.text = "" }
}
