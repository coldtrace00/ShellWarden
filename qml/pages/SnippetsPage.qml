import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import ShellWarden

// Saved commands. Send to the current tab or to every open session.
Item {
    id: root

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 28
        spacing: 20

        RowLayout {
            Layout.fillWidth: true
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text { textFormat: Text.PlainText; text: qsTr("Snippets"); color: Theme.text; font.pixelSize: 24; font.weight: Font.DemiBold }
                Text { textFormat: Text.PlainText; text: qsTr("Saved commands and scripts — send them from the terminal toolbar or from here. Stored encrypted in the vault, newest first."); color: Theme.textDim; font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.WordWrap }
            }
            AppButton { text: qsTr("New snippet"); glyph: Theme.iconAdd; variant: "primary"; onClicked: editor.openFor("") }
        }

        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: App.snippets.count > 0
            clip: true
            spacing: 8
            model: App.snippets
            ScrollBar.vertical: ScrollBar {}
            delegate: Rectangle {
                width: ListView.view.width
                implicitHeight: col.implicitHeight + 24
                radius: Theme.radius
                color: hov.hovered ? Theme.surfaceHover : Theme.surface
                border.color: Theme.border
                HoverHandler { id: hov }
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 12
                    Rectangle {
                        width: 36; height: 36; radius: 9
                        color: Theme.accentDim
                        IconLabel { anchors.centerIn: parent; glyph: Theme.iconSnippet; size: 16; color: Theme.accent }
                    }
                    ColumnLayout {
                        id: col
                        Layout.fillWidth: true
                        spacing: 4
                        RowLayout {
                            spacing: 8
                            Text { textFormat: Text.PlainText; text: model.name; color: Theme.text; font.pixelSize: 14; font.weight: Font.DemiBold }
                            Rectangle {
                                visible: !!model.group
                                radius: 4; color: Theme.surface2
                                implicitWidth: grp.implicitWidth + 10; implicitHeight: 18
                                Text { textFormat: Text.PlainText; id: grp; anchors.centerIn: parent; text: model.group || ""; color: Theme.textDim; font.pixelSize: 10 }
                            }
                            Rectangle {
                                visible: !!model.script
                                radius: 4; color: Theme.accentDim
                                implicitWidth: scr.implicitWidth + 10; implicitHeight: 18
                                Text { textFormat: Text.PlainText; id: scr; anchors.centerIn: parent; text: qsTr("script"); color: Theme.accent; font.pixelSize: 10 }
                            }
                            Text { textFormat: Text.PlainText; visible: model.autoRun && !model.script; text: "↵ " + qsTr("runs on send"); color: Theme.textFaint; font.pixelSize: 10 }
                        }
                        Text {
                            textFormat: Text.PlainText
                            Layout.fillWidth: true
                            text: model.command
                            color: Theme.textDim
                            font.family: Theme.monoFont
                            font.pixelSize: 11
                            elide: Text.ElideRight
                            maximumLineCount: 3
                            wrapMode: Text.WrapAnywhere
                        }
                    }
                    AppButton {
                        text: root.width < 700 ? "" : qsTr("Send to all"); glyph: Theme.iconTerminal; compact: true
                        enabled: App.sessions.count > 0
                        onClicked: App.runSnippet(model.id, -1, true)
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Send to every connected session")
                    }
                    AppButton {
                        text: root.width < 820 ? "" : qsTr("Run on hosts…"); glyph: Theme.iconServer; compact: true
                        enabled: App.hosts.count > 0
                        onClicked: batchDlg.openWith(model.command, [])
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Run on several saved hosts and see which succeeded")
                    }
                    AppButton { glyph: Theme.iconCopy; compact: true; variant: "ghost"; onClicked: App.copyText(model.command) }
                    AppButton { glyph: Theme.iconEdit; compact: true; variant: "ghost"; onClicked: editor.openFor(model.id) }
                    AppButton { glyph: Theme.iconDelete; compact: true; variant: "ghost"; onClicked: App.deleteSnippet(model.id) }
                }
            }
        }

        Item {
            visible: App.snippets.count === 0
            Layout.fillWidth: true
            Layout.fillHeight: true
            ColumnLayout {
                anchors.centerIn: parent
                spacing: 12
                IconLabel { Layout.alignment: Qt.AlignHCenter; glyph: Theme.iconSnippet; size: 40; color: Theme.textFaint }
                Text { textFormat: Text.PlainText; Layout.alignment: Qt.AlignHCenter; text: qsTr("No snippets yet"); color: Theme.text; font.pixelSize: 16 }
                Text { textFormat: Text.PlainText; Layout.alignment: Qt.AlignHCenter; text: qsTr("e.g. \"sudo apt update && sudo apt upgrade -y\" or \"docker ps -a\""); color: Theme.textDim; font.pixelSize: 12 }
                AppButton { Layout.alignment: Qt.AlignHCenter; text: qsTr("New snippet"); glyph: Theme.iconAdd; variant: "primary"; onClicked: editor.openFor("") }
            }
        }
    }

    FileDialog {
        id: shDlg
        nameFilters: [qsTr("Shell scripts (*.sh *.bash)"), qsTr("All files (*)")]
        onAccepted: {
            const t = App.readTextFile(selectedFile)
            if (t === "") return
            cmd.text = t
            scriptMode.checked = true
            if (nameField.text.trim() === "") {
                const f = decodeURIComponent(selectedFile.toString().split("/").pop())
                nameField.text = f
            }
        }
    }

    Modal {
        id: editor
        property string snippetId: ""
        title: snippetId ? qsTr("Edit snippet") : qsTr("New snippet")
        width: Math.min(620, (parent ? parent.width : 620) - 40)
        function openFor(id) {
            snippetId = id
            const s = id ? App.snippet(id) : ({})
            nameField.text = s.name || ""
            groupField.text = s.group || ""
            cmd.text = s.command || ""
            autoRun.checked = s.autoRun !== undefined ? s.autoRun : true
            scriptMode.checked = !!s.script
            err.text = ""
            open()
            nameField.field.forceActiveFocus()
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            AppField { id: nameField; Layout.fillWidth: true; Layout.preferredWidth: 400; label: qsTr("Name") }
            AppField { id: groupField; Layout.preferredWidth: 170; Layout.maximumWidth: 170; label: qsTr("Group") }
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 5
            RowLayout {
                Layout.fillWidth: true
                Text { textFormat: Text.PlainText; Layout.fillWidth: true; text: scriptMode.checked ? qsTr("Script") : qsTr("Command"); color: Theme.textDim; font.pixelSize: 12; font.weight: Font.Medium }
                AppButton { text: qsTr("Import .sh file…"); glyph: Theme.iconUpload; compact: true; variant: "ghost"; onClicked: shDlg.open() }
            }
            ScrollView {
                Layout.fillWidth: true
                Layout.preferredHeight: scriptMode.checked ? 240 : 140
                TextArea {
                    textFormat: TextArea.PlainText
                    id: cmd
                    color: Theme.text
                    font.family: Theme.monoFont
                    font.pixelSize: 12
                    wrapMode: TextEdit.WrapAnywhere
                    selectByMouse: true
                    placeholderText: "sudo systemctl restart nginx"
                    placeholderTextColor: Theme.textFaint
                    background: Rectangle { radius: Theme.radiusSmall; color: Theme.surface2; border.color: cmd.activeFocus ? Theme.accent : Theme.border }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text { textFormat: Text.PlainText; Layout.fillWidth: true; text: qsTr("Run as a script"); color: Theme.text; font.pixelSize: 13 }
                Text {
                    textFormat: Text.PlainText
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    text: qsTr("For multi-line scripts (.sh): the whole text is sent in one go and run by the interpreter from its #! line (sh if there is none). Off: the text is typed into the terminal as is.")
                    color: Theme.textFaint; font.pixelSize: 11
                }
            }
            AppSwitch { id: scriptMode }
        }
        AppCheckBox { id: autoRun; visible: !scriptMode.checked; text: qsTr("Press Enter after sending (run immediately)"); checked: true }
        Text { textFormat: Text.PlainText; id: err; visible: text !== ""; color: Theme.danger; font.pixelSize: 12 }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            AppButton { text: qsTr("Cancel"); variant: "ghost"; onClicked: editor.close() }
            AppButton {
                text: qsTr("Save"); variant: "primary"
                onClicked: {
                    const e = App.saveSnippet({ id: editor.snippetId, name: nameField.text.trim(), group: groupField.text.trim(),
                                                command: cmd.text, autoRun: autoRun.checked, script: scriptMode.checked })
                    if (e !== "") { err.text = e; return }
                    editor.close()
                }
            }
        }
    }
    BatchRunDialog { id: batchDlg }
}
