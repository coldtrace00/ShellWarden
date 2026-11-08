import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import ShellWarden

Item {
    id: root

    function baseName(type) {
        if (type === "ssh-ed25519") return "id_ed25519"
        if (type.indexOf("ecdsa") === 0) return "id_ecdsa"
        if (type === "ssh-rsa") return "id_rsa"
        return "id_key"
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 28
        spacing: 20

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text { textFormat: Text.PlainText; text: qsTr("Keychain"); color: Theme.text; font.pixelSize: 24; font.weight: Font.DemiBold }
                Text {
                    textFormat: Text.PlainText
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: qsTr("Public keys are saved as a single OpenSSH line — paste-ready for authorized_keys.")
                    color: Theme.textDim; font.pixelSize: 12
                }
            }
            AppButton { visible: App.keys.count > 0; text: root.width < 900 ? "" : qsTr("Install on servers"); glyph: Theme.iconServer; onClicked: deployDlg.openFor("")
                        ToolTip.visible: hovered; ToolTip.text: qsTr("Put public keys on servers (authorized_keys) — many at once") }
            AppButton { text: qsTr("Import"); glyph: Theme.iconFolder; onClicked: importDlg.openFresh() }
            AppButton { text: qsTr("Generate key"); glyph: Theme.iconAdd; variant: "primary"; onClicked: genDlg.openFresh() }
        }

        // SSH agent of this computer
        Rectangle {
            id: agentBar
            property var st: ({})
            function refresh() { st = App.agentStatus(true) }
            Component.onCompleted: refresh()
            visible: !!st.available   // nothing to show for people who do not use an agent
            Layout.fillWidth: true
            implicitHeight: agentRow.implicitHeight + 18
            radius: 8
            color: Theme.surface
            border.color: Theme.border
            RowLayout {
                id: agentRow
                anchors.fill: parent
                anchors.leftMargin: 14; anchors.rightMargin: 8
                spacing: 10
                Rectangle { width: 8; height: 8; radius: 4; color: agentBar.st.available ? (agentBar.st.keys > 0 ? Theme.ok : Theme.warn) : Theme.textFaint }
                Text {
                    textFormat: Text.PlainText
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    font.pixelSize: 12
                    color: Theme.textDim
                    text: !agentBar.st.available
                          ? qsTr("SSH agent: not running. Optional — start Pageant, the “OpenSSH Authentication Agent” service or ssh-agent to use keys kept there (also YubiKey / KeePassXC).")
                          : agentBar.st.error ? qsTr("SSH agent: %1 — %2").arg(agentBar.st.name).arg(agentBar.st.error)
                          : qsTr("SSH agent: %1 · keys: %2. Turn on “Use keys from the SSH agent” in a host to log in with them.").arg(agentBar.st.name).arg(agentBar.st.keys)
                }
                AppButton { glyph: Theme.iconRefresh; variant: "ghost"; compact: true; onClicked: agentBar.refresh()
                            ToolTip.visible: hovered; ToolTip.text: qsTr("Check again") }
            }
        }

        ListView {
            id: list
            visible: App.keys.count > 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            spacing: 10
            model: App.keys
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar {}

            delegate: Rectangle {
                width: list.width
                height: cardCol.implicitHeight + 32
                radius: Theme.radius
                color: Theme.surface
                border.color: Theme.border

                ColumnLayout {
                    id: cardCol
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 10

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 12
                        Rectangle {
                            width: 40; height: 40; radius: 10
                            color: Theme.accentDim
                            IconLabel { anchors.centerIn: parent; glyph: Theme.iconKey; size: 16; color: Theme.accent }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            Layout.preferredWidth: 100
                            spacing: 3
                            Flow {
                                Layout.fillWidth: true
                                spacing: 8
                                Text { textFormat: Text.PlainText; text: model.name; color: Theme.text; font.pixelSize: 15; font.weight: Font.DemiBold }
                                Rectangle {
                                    radius: 4; color: Theme.surface2
                                    implicitWidth: typeText.implicitWidth + 12; implicitHeight: 20
                                    Text { textFormat: Text.PlainText; id: typeText; anchors.centerIn: parent; text: model.type; color: Theme.textDim; font.pixelSize: 11; font.family: Theme.monoFont }
                                }
                                Rectangle {
                                    radius: 4
                                    color: !model.hasPrivateKey ? Theme.surface2 : !model.encrypted ? Theme.warnDim : (model.hasPassphrase ? Theme.okDim : Theme.surface2)
                                    implicitWidth: ppText.implicitWidth + 12; implicitHeight: 20
                                    Text {
                                        textFormat: Text.PlainText
                                        id: ppText
                                        anchors.centerIn: parent
                                        font.pixelSize: 11
                                        color: !model.hasPrivateKey ? Theme.textDim : !model.encrypted ? Theme.warn : (model.hasPassphrase ? Theme.ok : Theme.textDim)
                                        text: !model.hasPrivateKey ? qsTr("Only the public key")
                                              : !model.encrypted ? qsTr("No passphrase")
                                                               : (model.hasPassphrase ? qsTr("Passphrase saved") : qsTr("Passphrase asked on connect"))
                                    }
                                }
                                Rectangle {
                                    visible: !!model.protected
                                    radius: 4
                                    color: Theme.accentDim
                                    implicitWidth: protRow.implicitWidth + 12; implicitHeight: 20
                                    RowLayout {
                                        id: protRow
                                        anchors.centerIn: parent
                                        spacing: 4
                                        IconLabel { glyph: Theme.iconShield; size: 11; color: Theme.accent }
                                        Text { textFormat: Text.PlainText; text: qsTr("Master password"); color: Theme.accent; font.pixelSize: 11 }
                                    }
                                    HoverHandler { id: protHover }
                                    ToolTip.visible: protHover.hovered
                                    ToolTip.delay: 300
                                    ToolTip.text: qsTr("Used by a host that asks for the master password: the key stays encrypted with it and is opened only to connect. Every host that uses this key asks for the master password.")
                                }
                            }
                            Text {
                                textFormat: Text.PlainText
                                Layout.fillWidth: true
                                Layout.preferredWidth: 100
                                text: model.fingerprint
                                color: Theme.textDim
                                font.pixelSize: 12
                                font.family: Theme.monoFont
                                elide: Text.ElideMiddle
                            }
                        }
                        AppButton {
                            text: root.width < 760 ? "" : qsTr("Copy public key"); glyph: Theme.iconCopy; compact: true
                            onClicked: { App.copyText(App.publicKeyLine(model.id)); toastLocal.show(qsTr("Public key copied")) }
                        }
                        AppButton {
                            text: root.width < 760 ? "" : qsTr("Save .pub"); glyph: Theme.iconSave; compact: true
                            onClicked: {
                                saveDlg.mode = "pub"; saveDlg.keyId = model.id
                                saveDlg.selectedFile = App.suggestedFile(root.baseName(model.type) + ".pub")
                                saveDlg.open()
                            }
                        }
                        AppButton {
                            glyph: Theme.iconMore; compact: true; variant: "ghost"
                            onClicked: { keyMenu.keyId = model.id; keyMenu.keyType = model.type; keyMenu.keyName = model.name
                                         keyMenu.encrypted = model.encrypted; keyMenu.hasPassphrase = model.hasPassphrase
                                         keyMenu.hasPrivate = !!model.hasPrivateKey
                                         keyMenu.popup() }
                        }
                    }

                    // Public key preview, exactly what goes into authorized_keys
                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: pubText.implicitHeight + 16
                        radius: 6
                        color: Theme.terminalBg
                        border.color: Theme.border
                        TextEdit {
                            textFormat: TextEdit.PlainText
                            id: pubText
                            anchors.fill: parent
                            anchors.margins: 8
                            text: model.publicKey
                            readOnly: true
                            selectByMouse: true
                            wrapMode: TextEdit.WrapAnywhere
                            color: Theme.textDim
                            selectionColor: Theme.accentDim
                            font.pixelSize: 11
                            font.family: Theme.monoFont
                        }
                    }
                }
            }
        }

        Item {
            visible: App.keys.count === 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            ColumnLayout {
                anchors.centerIn: parent
                spacing: 12
                IconLabel { Layout.alignment: Qt.AlignHCenter; glyph: Theme.iconKey; size: 40; color: Theme.textFaint }
                Text { textFormat: Text.PlainText; Layout.alignment: Qt.AlignHCenter; text: qsTr("No keys yet"); color: Theme.text; font.pixelSize: 16 }
                Text { textFormat: Text.PlainText; Layout.alignment: Qt.AlignHCenter; text: qsTr("Generate an Ed25519 key protected by a passphrase, or import an existing one."); color: Theme.textDim; font.pixelSize: 12 }
                RowLayout {
                    Layout.alignment: Qt.AlignHCenter
                    AppButton { text: qsTr("Import"); glyph: Theme.iconFolder; onClicked: importDlg.openFresh() }
                    AppButton { text: qsTr("Generate key"); glyph: Theme.iconAdd; variant: "primary"; onClicked: genDlg.openFresh() }
                }
            }
        }
    }

    AppMenu {
        id: keyMenu
        property string keyId: ""
        property string keyType: ""
        property string keyName: ""
        property bool encrypted: false
        property bool hasPassphrase: false
        property bool hasPrivate: true
        AppMenuItem {
            text: qsTr("Save public key (SSH2 format)…")
            onTriggered: {
                saveDlg.mode = "rfc"; saveDlg.keyId = keyMenu.keyId
                saveDlg.selectedFile = App.suggestedFile(root.baseName(keyMenu.keyType) + "_ssh2.pub")
                saveDlg.open()
            }
        }
        AppMenuItem {
            glyph: Theme.iconServer
            text: qsTr("Install on servers…")
            onTriggered: deployDlg.openFor(keyMenu.keyId)
        }
        MenuSeparator {}
        AppMenuItem {
            enabled: keyMenu.hasPrivate
            text: qsTr("Save private key…")
            onTriggered: {
                saveDlg.mode = "priv"; saveDlg.keyId = keyMenu.keyId
                saveDlg.selectedFile = App.suggestedFile(root.baseName(keyMenu.keyType))
                saveDlg.open()
            }
        }
        MenuSeparator {}
        AppMenuItem {
            enabled: keyMenu.hasPrivate
            text: keyMenu.encrypted ? qsTr("Change or remove the key passphrase…") : qsTr("Add a passphrase to the key…")
            onTriggered: keyPassDlg.openFor(keyMenu.keyId, keyMenu.keyName)
        }
        AppMenuItem {
            enabled: keyMenu.encrypted
            text: keyMenu.hasPassphrase ? qsTr("Change saved passphrase…") : qsTr("Save passphrase in vault…")
            onTriggered: { ppDlg.keyId = keyMenu.keyId; ppDlg.openFresh() }
        }
        AppMenuItem {
            enabled: keyMenu.hasPassphrase
            text: qsTr("Forget saved passphrase")
            onTriggered: App.forgetKeyPassphrase(keyMenu.keyId)
        }
        AppMenuItem { text: qsTr("Rename…"); onTriggered: { renameDlg.keyId = keyMenu.keyId; renameField.text = keyMenu.keyName; renameDlg.open() } }
        MenuSeparator {}
        AppMenuItem { text: qsTr("Delete"); onTriggered: { delDlg.keyId = keyMenu.keyId; delDlg.keyName = keyMenu.keyName; delDlg.open() } }
    }

    FileDialog {
        id: saveDlg
        property string mode: "pub"
        property string keyId: ""
        fileMode: FileDialog.SaveFile
        title: mode === "priv" ? qsTr("Save private key") : qsTr("Save public key")
        nameFilters: mode === "priv" ? [qsTr("Private key (*)")] : [qsTr("Public key (*.pub)"), qsTr("All files (*)")]
        onAccepted: {
            let e = ""
            if (mode === "pub") e = App.exportPublicKey(keyId, selectedFile)
            else if (mode === "rfc") e = App.exportPublicKeyRfc4716(keyId, selectedFile)
            else if (App.keyIsProtected(keyId)) {
                const kid = keyId, file = selectedFile
                keyMaster.ask(qsTr("Export a protected key"),
                              qsTr("This key belongs to a host that asks for the master password, so it is kept encrypted with it. Enter the master password to export it."),
                              qsTr("Export"),
                              pw => { const r = App.exportPrivateKey(kid, file, pw); if (r === "") toastLocal.show(qsTr("Saved")); return r })
                return
            } else e = App.exportPrivateKey(keyId, selectedFile)
            toastLocal.show(e === "" ? qsTr("Saved") : e)
        }
    }
    MasterPrompt { id: keyMaster }

    KeyDeployDialog { id: deployDlg }
    KeyGenerateDialog { id: genDlg }
    KeyImportDialog { id: importDlg }

    Modal {
        id: ppDlg
        property string keyId: ""
        title: qsTr("Save key passphrase")
        subtitle: qsTr("Stored encrypted in the vault so you are not asked on every connection.")
        function openFresh() { ppField.text = ""; ppErr.text = ""; open(); ppField.field.forceActiveFocus() }
        AppField { id: ppField; Layout.fillWidth: true; label: qsTr("Passphrase"); secret: true; onAccepted: ppSave.clicked() }
        Text { textFormat: Text.PlainText; id: ppErr; visible: text !== ""; color: Theme.danger; font.pixelSize: 12 }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            AppButton { text: qsTr("Cancel"); variant: "ghost"; onClicked: ppDlg.close() }
            AppButton {
                id: ppSave
                text: qsTr("Verify & save"); variant: "primary"
                onClicked: {
                    const e = App.setKeyPassphrase(ppDlg.keyId, ppField.text, true)
                    if (e !== "") { ppErr.text = e; return }
                    ppField.text = ""
                    ppDlg.close()
                }
            }
        }
    }

    // the key's own passphrase: add, change, remove (the key file is written again)
    Modal {
        id: keyPassDlg
        property string keyId: ""
        property var st: ({})
        readonly property bool enc: !!st.encrypted
        title: enc ? qsTr("Key passphrase") : qsTr("Add a passphrase to the key")
        subtitle: enc ? qsTr("Change the passphrase of \"%1\", or leave the new one empty to remove it.").arg(keyName)
                      : qsTr("\"%1\" has no passphrase. With one, a copy of the key file is useless without it.").arg(keyName)
        property string keyName: ""
        function openFor(id, name) {
            keyId = id; keyName = name; st = App.keyPassphraseState(id)
            kpCurrent.text = ""; kpNew.text = ""; kpConfirm.text = ""; kpMaster.text = ""; kpErr.text = ""
            kpSave.checked = !!st.saved
            open()
            if (kpCurrent.visible) kpCurrent.field.forceActiveFocus(); else kpNew.field.forceActiveFocus()
        }
        AppField { id: kpCurrent; visible: keyPassDlg.enc && !keyPassDlg.st.saved; Layout.fillWidth: true
                   label: qsTr("Current passphrase"); secret: true; onAccepted: kpNew.field.forceActiveFocus() }
        AppField { id: kpNew; Layout.fillWidth: true; label: qsTr("New passphrase"); secret: true
                   hint: keyPassDlg.enc ? qsTr("Empty = no passphrase") : ""
                   onAccepted: kpConfirm.field.forceActiveFocus() }
        AppField { id: kpConfirm; Layout.fillWidth: true; label: qsTr("Repeat the new passphrase"); secret: true; onAccepted: kpApply.clicked() }
        AppCheckBox { id: kpSave; Layout.fillWidth: true; enabled: kpNew.text !== ""
                      text: qsTr("Save it in the vault (not asked on every connection)") }
        AppField { id: kpMaster; visible: !!keyPassDlg.st.protected; Layout.fillWidth: true
                   label: qsTr("Master password"); hint: qsTr("This key is protected by the master password.")
                   secret: true; onAccepted: kpApply.clicked() }
        Text { textFormat: Text.PlainText; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textFaint; font.pixelSize: 11
               text: qsTr("The key itself stays the same, so nothing changes on the servers. Copies saved earlier (exported files) keep their old passphrase.") }
        Text { textFormat: Text.PlainText; id: kpErr; visible: text !== ""; color: Theme.danger; font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.WordWrap }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            AppButton { text: qsTr("Cancel"); variant: "ghost"; onClicked: keyPassDlg.close() }
            AppButton {
                id: kpApply
                text: kpNew.text === "" && keyPassDlg.enc ? qsTr("Remove passphrase") : qsTr("Save")
                variant: kpNew.text === "" && keyPassDlg.enc ? "danger" : "primary"
                enabled: kpNew.text !== "" || keyPassDlg.enc
                onClicked: {
                    const e = App.changeKeyPassphrase(keyPassDlg.keyId, kpCurrent.text, kpNew.text, kpConfirm.text,
                                                      kpSave.checked, kpMaster.text)
                    kpCurrent.text = ""; kpMaster.text = ""
                    if (e !== "") { kpErr.text = e; return }
                    const msg = kpNew.text === "" ? qsTr("Passphrase removed") : (keyPassDlg.enc ? qsTr("Passphrase changed") : qsTr("Passphrase added"))
                    kpNew.text = ""; kpConfirm.text = ""
                    keyPassDlg.close()
                    toastLocal.show(msg)
                }
            }
        }
        onClosed: { kpCurrent.text = ""; kpNew.text = ""; kpConfirm.text = ""; kpMaster.text = "" }
    }

    Modal {
        id: renameDlg
        property string keyId: ""
        title: qsTr("Rename key")
        AppField { id: renameField; Layout.fillWidth: true; label: qsTr("Name"); onAccepted: renameOk.clicked() }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            AppButton { text: qsTr("Cancel"); variant: "ghost"; onClicked: renameDlg.close() }
            AppButton { id: renameOk; text: qsTr("Rename"); variant: "primary"; onClicked: { App.renameKey(renameDlg.keyId, renameField.text); renameDlg.close() } }
        }
    }

    Modal {
        id: delDlg
        property string keyId: ""
        property string keyName: ""
        title: qsTr("Delete \"%1\"?").arg(keyName)
        subtitle: qsTr("The private key is removed from the vault. Export it first if you still need it. Hosts using it will fall back to password login.")
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            AppButton { text: qsTr("Cancel"); variant: "ghost"; onClicked: delDlg.close() }
            AppButton { text: qsTr("Delete"); variant: "danger"; glyph: Theme.iconDelete; onClicked: { App.deleteKey(delDlg.keyId); delDlg.close() } }
        }
    }

    Rectangle {
        id: toastLocal
        function show(msg) { tlText.text = msg; opacity = 1; tlTimer.restart() }
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 24
        width: tlText.implicitWidth + 32; height: 36; radius: 8
        color: Theme.surface2; border.color: Theme.border
        opacity: 0; visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: 160 } }
        Text { textFormat: Text.PlainText; id: tlText; anchors.centerIn: parent; color: Theme.text; font.pixelSize: 12 }
        Timer { id: tlTimer; interval: 2500; onTriggered: toastLocal.opacity = 0 }
    }
}
