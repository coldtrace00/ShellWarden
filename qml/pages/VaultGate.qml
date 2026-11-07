import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ShellWarden

// First run: create the encrypted vault. Afterwards: unlock it.
Rectangle {
    id: root
    color: Theme.bg
    readonly property bool creating: !App.vaultExists

    Connections {
        target: App
        function onVaultError(message) { err.text = message }
        function onVaultStateChanged() { pw.text = ""; newPw.clear(); err.text = "" }
    }

    onVisibleChanged: if (visible) focusTimer.start()
    Component.onCompleted: focusTimer.start()
    Timer { id: focusTimer; interval: 50; onTriggered: root.creating ? newPw.focusInput() : pw.field.forceActiveFocus() }

    function submit() {
        err.text = ""
        if (root.creating) {
            if (!newPw.acceptable) return
            App.createVault(newPw.text, newPw.confirmText)
        } else {
            App.unlockVault(pw.text)
        }
    }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth

        Item {
            width: parent.width
            implicitHeight: Math.max(root.height, card.height + 80)

            Rectangle {
                id: card
                anchors.centerIn: parent
                width: Math.min(root.creating ? 520 : 440, root.width - 40)
                height: col.implicitHeight + 64
                radius: 18
                color: Theme.surface
                border.color: Theme.border

                ColumnLayout {
                    id: col
                    anchors.fill: parent
                    anchors.margins: 32
                    spacing: 16

                    Logo { Layout.alignment: Qt.AlignHCenter; size: 64 }

                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        text: root.creating ? qsTr("Create your vault") : qsTr("Welcome back")
                        color: Theme.text
                        font.pixelSize: 22
                        font.weight: Font.DemiBold
                    }
                    Text {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        color: Theme.textDim
                        font.pixelSize: 12
                        text: root.creating
                              ? qsTr("Hosts, passwords, keys and key passphrases are stored encrypted with this master password (Argon2id + XChaCha20-Poly1305). It cannot be recovered — don't lose it.")
                              : qsTr("Enter the master password to unlock your hosts and keys.")
                    }

                    // unlock
                    AppField {
                        id: pw
                        visible: !root.creating
                        Layout.fillWidth: true
                        label: qsTr("Master password")
                        secret: true
                        onAccepted: root.submit()
                    }

                    // create: generator + strength + consent for weak passwords
                    PasswordField {
                        lengthSlider: true
                        id: newPw
                        visible: root.creating
                        Layout.fillWidth: true
                        label: qsTr("Master password")
                        placeholder: qsTr("19+ characters, or press Generate")
                        generateLength: 24
                        onAccepted: root.submit()
                    }
                    Text {
                        visible: root.creating && newPw.generated
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: Theme.warn
                        font.pixelSize: 11
                        text: qsTr("Write the generated password down or store it in a password manager before continuing.")
                    }

                    Text {
                        id: err
                        visible: text !== ""
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: Theme.danger
                        font.pixelSize: 12
                    }

                    AppButton {
                        Layout.fillWidth: true
                        Layout.topMargin: 4
                        variant: "primary"
                        enabled: !App.busy && (root.creating ? newPw.acceptable : pw.text.length > 0)
                        text: App.busy ? qsTr("Deriving key…") : (root.creating ? qsTr("Create vault") : qsTr("Unlock"))
                        glyph: root.creating ? Theme.iconShield : Theme.iconUnlock
                        onClicked: root.submit()
                    }

                    AppButton {
                        visible: !root.creating
                        Layout.alignment: Qt.AlignHCenter
                        variant: "ghost"
                        compact: true
                        text: qsTr("Forgot the master password? Start over")
                        onClicked: startOverDlg.open()
                    }

                    BusyIndicator {
                        Layout.alignment: Qt.AlignHCenter
                        running: App.busy
                        visible: App.busy
                        implicitWidth: 28; implicitHeight: 28
                    }
                }
            }
        }
    }

    Modal {
        id: startOverDlg
        title: qsTr("Start over with a new vault?")
        subtitle: qsTr("The master password cannot be recovered — that is what keeps the vault safe. Your saved hosts, passwords and keys stay locked in the old vault.")
        onOpened: confirmBox.checked = false
        Text {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: Theme.textDim
            font.pixelSize: 12
            text: qsTr("The old vault is not deleted: it is moved to the backups folder (forgotten-<date>.swv). If you remember the password later, open it with Settings → Vault → Restore from backup. Known hosts and settings are kept.")
        }
        AppCheckBox { id: confirmBox; text: qsTr("I understand that hosts and keys must be added again") }
        Text { id: soErr; visible: text !== ""; color: Theme.danger; font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.WordWrap }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            AppButton { text: qsTr("Cancel"); variant: "ghost"; onClicked: startOverDlg.close() }
            AppButton {
                text: qsTr("Start over"); variant: "danger"
                enabled: confirmBox.checked
                onClicked: {
                    const e = App.startOver()
                    if (e !== "") { soErr.text = e; return }
                    startOverDlg.close()
                }
            }
        }
    }

    // language switch in the corner: works before the vault is unlocked
    AbstractButton {
        id: langBtn
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 16
        implicitHeight: 34
        implicitWidth: langRow.implicitWidth + 24
        hoverEnabled: true
        onClicked: langMenu.popup(langBtn, langBtn.width - langMenu.width, langBtn.height + 4)
        background: Rectangle { radius: 8; color: langBtn.hovered ? Theme.surface2 : Theme.surface; border.color: Theme.border }
        contentItem: Item {
            RowLayout {
                id: langRow
                anchors.centerIn: parent
                spacing: 8
                IconLabel { glyph: Theme.iconGlobe; size: 14; color: Theme.textDim }
                Text {
                    text: {
                        for (const l of App.languages) if (l.code === App.language) return l.name
                        return "Language"
                    }
                    color: Theme.text; font.pixelSize: 12
                }
                IconLabel { glyph: Theme.iconChevronDown; size: 11; color: Theme.textDim }
            }
        }
        ToolTip.visible: hovered
        ToolTip.text: "Language · Мова · Langue · Idioma"
    }
    AppMenu {
        id: langMenu
        Instantiator {
            model: App.languages
            delegate: AppMenuItem {
                text: modelData.name
                checkable: true
                checked: App.language === modelData.code
                onTriggered: App.setLanguage(modelData.code)
            }
            onObjectAdded: (i, o) => langMenu.insertItem(i, o)
            onObjectRemoved: (i, o) => langMenu.removeItem(o)
        }
    }
}
