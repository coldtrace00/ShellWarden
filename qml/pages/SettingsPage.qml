import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import ShellWarden

Item {
    id: root

    component Section: Rectangle {
        id: sec
        default property alias content: inner.data
        property string title: ""
        property string glyph: ""
        Layout.fillWidth: true
        Layout.leftMargin: 28; Layout.rightMargin: 28
        implicitHeight: secCol.implicitHeight + 36
        radius: Theme.radius
        color: Theme.surface
        border.color: Theme.border
        ColumnLayout {
            id: secCol
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 18
            spacing: 14
            RowLayout {
                spacing: 10
                IconLabel { visible: sec.glyph !== ""; glyph: sec.glyph; size: 17; color: Theme.accent }
                Text { text: sec.title; color: Theme.text; font.pixelSize: 15; font.weight: Font.DemiBold }
            }
            ColumnLayout { id: inner; Layout.fillWidth: true; spacing: 12 }
        }
    }

    component SettingLabel: Item {
        property string label: ""
        property string hint: ""
        Layout.fillWidth: true
        Layout.preferredWidth: 200   // the hint wraps to whatever width is left
        Layout.minimumWidth: 120
        Layout.alignment: Qt.AlignVCenter
        implicitHeight: labelCol.implicitHeight
        Column {
            id: labelCol
            width: parent.width
            spacing: 2
            Text { width: parent.width; text: label; color: Theme.text; font.pixelSize: 13; wrapMode: Text.WordWrap }
            Text { width: parent.width; visible: hint !== ""; text: hint; color: Theme.textFaint; font.pixelSize: 11; wrapMode: Text.WordWrap }
        }
    }

    component Row_: RowLayout {
        Layout.fillWidth: true
        Layout.minimumHeight: 44
        spacing: 16
    }

    ThemeEditor { id: themeEditor }

    FileDialog {
        id: backupDlg
        fileMode: FileDialog.SaveFile
        nameFilters: [qsTr("Vault backup (*.swv)")]
        selectedFile: App.suggestedFile("vault-backup.swv")
        onAccepted: vaultMsg.show(App.backupVault(selectedFile), qsTr("Backup saved."))
    }
    FileDialog {
        id: restoreDlg
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("Vault backup (*.swv)"), qsTr("All files (*)")]
        onAccepted: { restorePopup.file = selectedFile; restorePopup.open() }
    }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            width: parent.width
            spacing: 18

            Item { height: 8 }
            Text {
                Layout.leftMargin: 28
                text: qsTr("Settings"); color: Theme.text; font.pixelSize: 24; font.weight: Font.DemiBold
            }

            // ---------------------------------------------------------
            Section {
                title: qsTr("Interface")
                glyph: Theme.iconSettings
                Row_ {
                    SettingLabel { label: qsTr("Language"); hint: qsTr("Applies immediately.") }
                    AppCombo {
                        Layout.preferredWidth: 240
                        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                        textRole: "name"
                        model: [{ code: "", name: qsTr("System default") }].concat(App.languages)
                        currentIndex: {
                            const c = App.settings.language || ""
                            for (let i = 0; i < model.length; ++i)
                                if (model[i].code === c) return i
                            return 0
                        }
                        onActivated: (i) => App.setLanguage(model[i].code)
                    }
                }
                Row_ {
                    SettingLabel { label: qsTr("Compact side menu"); hint: qsTr("Icons only. Ctrl+Shift+B toggles it anywhere.") }
                    AppSwitch { Layout.alignment: Qt.AlignRight | Qt.AlignVCenter; checked: App.settings.sidebarCollapsed
                                onToggled: App.setSetting("sidebarCollapsed", checked) }
                }
            }

            // ---------------------------------------------------------
            Section {
                title: qsTr("Terminal")
                glyph: Theme.iconTerminal
                Row_ {
                    SettingLabel { label: qsTr("Colour theme"); hint: qsTr("Default for all tabs. A host can have its own theme; the palette button in a tab changes it for that tab.") }
                    AppButton {
                        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                        visible: App.settings.terminalTheme !== "default"
                        text: qsTr("Back to default"); glyph: Theme.iconRefresh; variant: "ghost"
                        onClicked: App.setSetting("terminalTheme", "default")
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Switch back to the standard \"Default Dark\" theme")
                    }
                    AppButton {
                        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                        text: qsTr("Create your own theme"); glyph: Theme.iconAdd; variant: "primary"
                        onClicked: themeEditor.openNew()
                    }
                }
                Flow {
                    id: themeFlow
                    Layout.fillWidth: true
                    spacing: 10
                    // cards stretch so every row fills the width evenly
                    readonly property int cols: Math.max(1, Math.floor((width + spacing) / (180 + spacing)))
                    readonly property real cardW: Math.floor((width - (cols - 1) * spacing) / cols)
                    // "create your own" card
                    AbstractButton {
                        id: newThemeCard
                        width: themeFlow.cardW; height: 90
                        hoverEnabled: true
                        onClicked: themeEditor.openNew()
                        background: Rectangle { radius: 10; color: newThemeCard.hovered ? Theme.surfaceHover : Theme.surface2
                                                border.color: newThemeCard.hovered ? Theme.accent : Theme.border; border.width: 1 }
                        // the control resizes contentItem, so centre a Column inside an Item
                        contentItem: Item {
                            Column {
                                anchors.centerIn: parent
                                spacing: 6
                                IconLabel { anchors.horizontalCenter: parent.horizontalCenter; glyph: Theme.iconAdd; size: 18; color: Theme.accent }
                                Text { anchors.horizontalCenter: parent.horizontalCenter; text: qsTr("Create your own"); color: Theme.text; font.pixelSize: 12; font.weight: Font.DemiBold }
                                Text { anchors.horizontalCenter: parent.horizontalCenter; text: qsTr("colours, picture"); color: Theme.textFaint; font.pixelSize: 10 }
                            }
                        }
                    }
                    Repeater {
                        model: App.terminalThemes
                        delegate: ThemePreview {
                            width: themeFlow.cardW
                            theme: modelData
                            onEditRequested: themeEditor.openEdit(modelData)
                            selected: App.settings.terminalTheme === modelData.id
                            onClicked: App.setSetting("terminalTheme", modelData.id)
                        }
                    }
                }
                Row_ {
                    SettingLabel { label: qsTr("Local terminal"); hint: qsTr("Extra: a tab with a shell of this computer (PowerShell, cmd, WSL, Git Bash…) — handy for git or ping. A button appears next to quick connect.") }
                    AppCombo {
                        id: shellBox
                        Layout.preferredWidth: 220
                        Layout.alignment: Qt.AlignVCenter
                        visible: !!App.settings.localTerminal
                        readonly property var shells: App.localShells()
                        model: shells.map(s => s.name)
                        currentIndex: Math.max(0, shells.findIndex(s => s.id === App.settings.localShell))
                        onActivated: (i) => App.setSetting("localShell", shells[i].id)
                    }
                    AppSwitch { Layout.alignment: Qt.AlignRight | Qt.AlignVCenter; checked: !!App.settings.localTerminal; onToggled: App.setSetting("localTerminal", checked) }
                }
                Row_ {
                    SettingLabel { label: qsTr("Server monitor"); hint: qsTr("A strip under the terminal with CPU, memory, disk, network and uptime of the server (Linux servers). The gear on the strip chooses what is shown.") }
                    AppSpin {
                        Layout.alignment: Qt.AlignVCenter
                        enabled: App.settings.monitorEnabled
                        from: 2; to: 300; editable: true
                        value: App.settings.monitorInterval || 5
                        onValueModified: App.setSetting("monitorInterval", value)
                    }
                    AppSwitch { Layout.alignment: Qt.AlignRight | Qt.AlignVCenter; checked: App.settings.monitorEnabled; onToggled: App.setSetting("monitorEnabled", checked) }
                }
                Row_ {
                    SettingLabel { label: qsTr("Font"); hint: qsTr("Installed monospace fonts. Default: %1").arg(App.defaultFont) }
                    AppCombo {
                        id: fontBox
                        Layout.preferredWidth: 260
                        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                        model: [qsTr("Default (%1)").arg(App.defaultFont)].concat(App.monospaceFonts)
                        currentIndex: Math.max(0, App.monospaceFonts.indexOf(App.settings.fontFamily) + 1)
                        onActivated: (i) => App.setSetting("fontFamily", i === 0 ? "" : App.monospaceFonts[i - 1])
                        rowFont: (i) => i === 0 ? App.defaultFont : App.monospaceFonts[i - 1]
                    }
                }
                // live preview of the chosen font
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 54
                    radius: 8
                    color: Theme.terminalBg
                    border.color: Theme.border
                    Text {
                        anchors.fill: parent
                        anchors.margins: 12
                        verticalAlignment: Text.AlignVCenter
                        text: "user@server:~$ ls -la /etc  # 0O 1lI |  Привіт → ✓"
                        color: Theme.text
                        font.family: App.settings.fontFamily || App.defaultFont
                        font.pointSize: App.settings.fontSize
                        elide: Text.ElideRight
                    }
                }
                Row_ {
                    SettingLabel { label: qsTr("Font size"); hint: qsTr("Ctrl + mouse wheel or Ctrl +/- in a terminal also works.") }
                    AppSpin { Layout.alignment: Qt.AlignRight | Qt.AlignVCenter; from: 6; to: 40; value: App.settings.fontSize; editable: true; onValueModified: App.setSetting("fontSize", value) }
                }
                Row_ {
                    SettingLabel { label: qsTr("Scrollback lines") }
                    AppSpin { Layout.alignment: Qt.AlignRight | Qt.AlignVCenter; from: 0; to: 200000; stepSize: 1000; value: App.settings.scrollback; editable: true; onValueModified: App.setSetting("scrollback", value) }
                }
                Row_ {
                    SettingLabel { label: qsTr("Copy on select"); hint: qsTr("Like PuTTY: selected text goes straight to the clipboard.") }
                    AppSwitch { Layout.alignment: Qt.AlignRight | Qt.AlignVCenter; checked: App.settings.copyOnSelect; onToggled: App.setSetting("copyOnSelect", checked) }
                }
                Row_ {
                    SettingLabel { label: qsTr("Right click pastes"); hint: qsTr("Like PuTTY. Ctrl+Shift+V and Shift+Insert always paste.") }
                    AppSwitch { Layout.alignment: Qt.AlignRight | Qt.AlignVCenter; checked: App.settings.rightClickPaste; onToggled: App.setSetting("rightClickPaste", checked) }
                }
                Row_ {
                    SettingLabel { label: qsTr("Highlight output"); hint: qsTr("Colours errors, warnings, OK, IP addresses, URLs, paths and dates in plain text. What the server already colours, and full-screen programs (vim, mc, htop), are left as they are.") }
                    AppSwitch { Layout.alignment: Qt.AlignRight | Qt.AlignVCenter; checked: App.settings.highlightOutput; onToggled: App.setSetting("highlightOutput", checked) }
                }
                Row_ {
                    SettingLabel { label: qsTr("Terminal bell"); hint: qsTr("What happens when the server rings the bell, e.g. Backspace on an empty line or Tab with no completion.") }
                    AppCombo {
                        Layout.preferredWidth: 220
                        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                        readonly property var keys: ["off", "flash", "sound"]
                        model: App.platform === "windows" ? [qsTr("Nothing"), qsTr("Flash the screen"), qsTr("System sound")]
                                                         : [qsTr("Nothing"), qsTr("Flash the screen")]
                        currentIndex: Math.max(0, keys.indexOf(App.settings.bell))
                        onActivated: (i) => App.setSetting("bell", keys[i])
                    }
                }
            }

            // ---------------------------------------------------------
            Section {
                title: qsTr("Connection")
                glyph: Theme.iconPlug
                Row_ {
                    SettingLabel { label: qsTr("Keepalive interval (seconds)"); hint: qsTr("Keeps NAT/firewalls from dropping idle sessions. 0 = off. Applies to new sessions.") }
                    AppSpin { Layout.alignment: Qt.AlignRight | Qt.AlignVCenter; from: 0; to: 600; stepSize: 5; value: App.settings.keepaliveSeconds; editable: true; onValueModified: App.setSetting("keepaliveSeconds", value) }
                }
                Row_ {
                    SettingLabel { label: qsTr("Reconnect automatically"); hint: qsTr("If an open connection drops (Wi-Fi, sleep, server restart), try again after 3, 5, 10, 20 and 30 s. Not after a wrong password or a kill-switch block.") }
                    AppSwitch { Layout.alignment: Qt.AlignRight | Qt.AlignVCenter; checked: App.settings.autoReconnect; onToggled: App.setSetting("autoReconnect", checked) }
                }
                Row_ {
                    SettingLabel { label: qsTr("Kill switch check interval (seconds)"); hint: qsTr("How often a proxy carrying sessions is checked. A failed check closes its sessions.") }
                    AppSpin { Layout.alignment: Qt.AlignRight | Qt.AlignVCenter; from: 1; to: 60; value: App.settings.killSwitchInterval; editable: true; onValueModified: App.setSetting("killSwitchInterval", value) }
                }
            }

            // ---------------------------------------------------------
            Section {
                title: qsTr("Vault")
                glyph: Theme.iconDatabase
                id: vaultSection

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: pathCol.implicitHeight + 20
                    radius: 8
                    color: Theme.bg
                    border.color: Theme.border
                    ColumnLayout {
                        id: pathCol
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 4
                        RowLayout {
                            spacing: 8
                            Text { text: App.portable ? qsTr("Portable mode") : qsTr("Installed mode"); color: Theme.text; font.pixelSize: 12; font.weight: Font.DemiBold }
                            Text {
                                text: App.portable ? qsTr("(data next to the program — nothing written to the PC)")
                                                   : qsTr("(create an empty file \"portable\" next to the .exe to switch)")
                                color: Theme.textFaint; font.pixelSize: 11
                            }
                        }
                        Text { text: qsTr("Vault file: %1").arg(App.vaultPath); color: Theme.textDim; font.pixelSize: 11; font.family: Theme.monoFont; Layout.fillWidth: true; elide: Text.ElideMiddle }
                        Text {
                            visible: App.portable
                            Layout.fillWidth: true
                            wrapMode: Text.WordWrap
                            color: Theme.textFaint; font.pixelSize: 11
                            text: qsTr("Everything is in this folder: vault.swv (hosts, passwords, keys — encrypted), settings.ini, known_hosts, logs, backups, keys (exported keys), updates, cache.")
                        }
                        Text { text: qsTr("Encryption: Argon2id (256 MiB, 3 passes) → XChaCha20-Poly1305, authenticated header."); color: Theme.textFaint; font.pixelSize: 11 }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    AppButton { text: qsTr("Open data folder"); glyph: Theme.iconFolder; onClicked: App.openDataFolder() }
                    AppButton { text: qsTr("Save backup…"); glyph: Theme.iconSave; enabled: App.vaultExists; onClicked: backupDlg.open() }
                    AppButton { text: qsTr("Restore from backup…"); glyph: Theme.iconUpload; onClicked: restoreDlg.open() }
                    Item { Layout.fillWidth: true }
                }
                Text {
                    id: vaultMsg
                    visible: text !== ""
                    function show(err, ok) { color = err === "" ? Theme.ok : Theme.danger; text = err === "" ? ok : err }
                    font.pixelSize: 12
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                }
                SettingLabel { label: qsTr("Automatic daily backups"); hint: qsTr("An encrypted copy is kept every day you change something (last 14 days). They need the master password to open.") }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    Repeater {
                        model: App.vaultUnlocked ? App.backups() : []
                        delegate: RowLayout {
                            Layout.fillWidth: true
                            visible: index < 5
                            spacing: 10
                            IconLabel { glyph: Theme.iconClock; size: 13; color: Theme.textDim }
                            Text { text: modelData.date; color: Theme.textDim; font.pixelSize: 11; font.family: Theme.monoFont }
                            Text { text: modelData.size; color: Theme.textFaint; font.pixelSize: 11 }
                            Item { Layout.fillWidth: true }
                            AppButton { text: qsTr("Restore"); compact: true; variant: "ghost"
                                        onClicked: { restorePopup.file = modelData.url; restorePopup.open() } }
                        }
                    }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

                ColumnLayout {
                    id: lockRow
                    Layout.fillWidth: true
                    spacing: 8
                    // "Never lock" keeps the last real value, so unticking it restores minutes/hours.
                    readonly property int minutes: App.settings.autoLockMinutes > 0 ? App.settings.autoLockMinutes
                                                                                  : (App.settings.lastLockMinutes || 15)
                    readonly property bool inHours: minutes % 60 === 0
                    function apply() {
                        if (neverLock.checked) { App.setSetting("autoLockMinutes", 0); return }
                        const m = Math.max(1, lockValue.value) * (lockUnit.currentIndex === 1 ? 60 : 1)
                        App.setSetting("lastLockMinutes", m)
                        App.setSetting("autoLockMinutes", m)
                    }
                    SettingLabel { label: qsTr("Lock the vault after inactivity"); hint: qsTr("Any key press or click resets the timer. Open sessions keep running while the vault is locked.") }
                    RowLayout {
                    spacing: 10
                    AppSpin {
                        id: lockValue
                        Layout.alignment: Qt.AlignVCenter
                        enabled: !neverLock.checked
                        from: 1; to: 999
                        editable: true
                        value: lockRow.inHours ? lockRow.minutes / 60 : lockRow.minutes
                        onValueModified: lockRow.apply()
                    }
                    AppCombo {
                        id: lockUnit
                        Layout.preferredWidth: 120
                        Layout.alignment: Qt.AlignVCenter
                        enabled: !neverLock.checked
                        model: [qsTr("minutes"), qsTr("hours")]
                        currentIndex: lockRow.inHours ? 1 : 0
                        onActivated: lockRow.apply()
                    }
                    AppCheckBox {
                        id: neverLock
                        Layout.alignment: Qt.AlignVCenter
                        text: qsTr("Never lock")
                        checked: App.settings.autoLockMinutes <= 0
                        onToggled: lockRow.apply()
                    }
                    Item { Layout.fillWidth: true }
                    }
                }
                Row_ {
                    SettingLabel { label: qsTr("Check new host keys in DNS (SSHFP)"); hint: qsTr("When a server's key is new or changed, ask its domain's DNS (over HTTPS, through the same proxy) whether the key is published there. Needs SSHFP records on the domain.") }
                    AppSwitch { Layout.alignment: Qt.AlignRight | Qt.AlignVCenter; checked: App.settings.sshfpCheck; onToggled: App.setSetting("sshfpCheck", checked) }
                }
                Row_ {
                    SettingLabel { label: qsTr("Show which hosts are online"); hint: qsTr("While the host list is open, every 5 minutes one short connection to each host (through its own proxy) shows a green or red dot on its card. Hosts via Tor, Cloudflare or a jump host, and direct ones while the kill switch is on, are not checked.") }
                    AppSwitch { Layout.alignment: Qt.AlignRight | Qt.AlignVCenter; checked: App.settings.hostProbe; onToggled: { App.setSetting("hostProbe", checked); App.probeHosts() } }
                }
                Row_ {
                    SettingLabel { label: qsTr("Lock together with the computer"); hint: qsTr("When you lock Windows (Win+L) or the computer goes to sleep, the vault locks too.") }
                    AppSwitch { Layout.alignment: Qt.AlignRight | Qt.AlignVCenter; checked: App.settings.lockOnSystemLock; onToggled: App.setSetting("lockOnSystemLock", checked) }
                }

                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }

                Text { text: qsTr("Change master password"); color: Theme.text; font.pixelSize: 13; font.weight: Font.Medium }
                AppField { id: oldPw; Layout.fillWidth: true; label: qsTr("Current master password"); secret: true; enabled: App.vaultUnlocked }
                PasswordField {
                    lengthSlider: true
                    id: newPw
                    Layout.fillWidth: true
                    enabled: App.vaultUnlocked
                    label: qsTr("New master password")
                    placeholder: qsTr("19+ characters, or press Generate")
                }
                RowLayout {
                    Layout.fillWidth: true
                    Text { id: pwMsg; Layout.fillWidth: true; font.pixelSize: 12; wrapMode: Text.WordWrap }
                    AppButton {
                        text: qsTr("Change password"); variant: "primary"
                        enabled: App.vaultUnlocked && oldPw.text !== "" && newPw.acceptable && newPw.text.length > 0
                        onClicked: {
                            const e = App.changeMasterPassword(oldPw.text, newPw.text, newPw.confirmText)
                            pwMsg.color = e === "" ? Theme.ok : Theme.danger
                            pwMsg.text = e === "" ? qsTr("Master password changed.") : e
                            if (e === "") { oldPw.text = ""; newPw.clear() }
                        }
                    }
                }
            }

            // ---------------------------------------------------------
            Section {
                title: qsTr("Activity log")
                glyph: Theme.iconLog
                Row_ {
                    SettingLabel { label: qsTr("Keep an activity log"); hint: qsTr("Connections, routes, IP seen by servers, vault and key changes, kill switch events. Never passwords or keys. 12 months are kept.") }
                    AppSwitch { Layout.alignment: Qt.AlignRight | Qt.AlignVCenter; checked: App.settings.logEnabled; onToggled: App.setSetting("logEnabled", checked) }
                }
            }

            // ---------------------------------------------------------
            Section {
                title: qsTr("Updates")
                glyph: Theme.iconUpdate
                readonly property var up: App.updater
                Row_ {
                    SettingLabel { label: qsTr("GitHub repository"); hint: qsTr("owner/name of the project. Releases must contain SHA256SUMS — downloads are verified before installing.") }
                    TextField {
                        Layout.preferredWidth: 260
                        Layout.alignment: Qt.AlignRight | Qt.AlignVCenter
                        text: App.settings.updateRepo
                        placeholderText: "coldtrace00/ShellWarden"
                        placeholderTextColor: Theme.textFaint
                        color: Theme.text
                        font.family: Theme.monoFont
                        onEditingFinished: App.setSetting("updateRepo", text.trim())
                    }
                }
                Row_ {
                    SettingLabel { label: qsTr("Check for updates on start") }
                    AppSwitch { Layout.alignment: Qt.AlignRight | Qt.AlignVCenter; checked: App.settings.autoCheckUpdates; onToggled: App.setSetting("autoCheckUpdates", checked) }
                }
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: upCol.implicitHeight + 20
                    radius: 8
                    color: App.updater.state === "available" || App.updater.state === "ready" ? Theme.accentDim
                           : App.updater.state === "error" ? Theme.dangerDim : Theme.bg
                    border.color: Theme.border
                    ColumnLayout {
                        id: upCol
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 8
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10
                            Text {
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                color: Theme.text
                                font.pixelSize: 12
                                text: {
                                    switch (App.updater.state) {
                                    case "checking": return qsTr("Checking…")
                                    case "uptodate": return qsTr("You have the latest version (%1).").arg(App.version)
                                    case "available": return qsTr("Version %1 is available (you have %2).").arg(App.updater.latestVersion).arg(App.version)
                                    case "downloading": return qsTr("Downloading… %1%").arg(Math.round(App.updater.progress * 100))
                                    case "ready": return qsTr("Downloaded and verified (SHA-256 matches).")
                                    case "error": return App.updater.error
                                    default: return qsTr("Current version: %1").arg(App.version)
                                    }
                                }
                            }
                            AppButton { visible: App.updater.state !== "downloading"; text: qsTr("Check now"); glyph: Theme.iconRefresh; compact: true; onClicked: App.checkForUpdates() }
                            AppButton { visible: App.updater.state === "available"; text: qsTr("Download"); glyph: Theme.iconSave; variant: "primary"; compact: true; onClicked: App.updater.download() }
                            AppButton { visible: App.updater.state === "ready"; text: qsTr("Install"); glyph: Theme.iconUpdate; variant: "primary"; compact: true; onClicked: App.updater.install() }
                            AppButton {
                                visible: App.updater.localizedNotes !== "" && App.updater.state !== "downloading"
                                text: qsTr("What's new"); compact: true; variant: "ghost"
                                onClicked: App.showUpdateDetails()
                            }
                            AppButton { visible: App.updater.pageUrl !== ""; text: qsTr("Release page"); compact: true; variant: "ghost"; onClicked: App.updater.openPage() }
                        }
                        ProgressBar { visible: App.updater.state === "downloading"; Layout.fillWidth: true; value: App.updater.progress }
                    }
                }
            }

            // ---------------------------------------------------------
            Section {
                title: qsTr("Keyboard shortcuts")
                glyph: Theme.iconTerminal
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.textFaint
                    font.pixelSize: 11
                    text: qsTr("They use Ctrl+Shift, so plain Ctrl+C, Ctrl+K and the like still go to the server.")
                }
                GridLayout {
                    id: keysGrid
                    Layout.fillWidth: true
                    columns: root.width > 1100 ? 2 : 1
                    columnSpacing: 24
                    rowSpacing: 4
                    Repeater {
                        model: [
                            ["Ctrl+Shift+K", qsTr("Quick search: hosts, tabs, snippets, pages")],
                            ["Ctrl+Shift+F", qsTr("Find text in the terminal")],
                            ["Ctrl+Tab", qsTr("Next tab")],
                            ["Ctrl+Shift+Tab", qsTr("Previous tab")],
                            ["Ctrl+Shift+W", qsTr("Close the tab")],
                            ["Ctrl+Shift+T", qsTr("Back to the vault (hosts)")],
                            ["Ctrl+Shift+D", qsTr("Split view")],
                            ["Ctrl+Shift+B", qsTr("Collapse / expand the side menu")],
                            ["Ctrl+Shift+C", qsTr("Copy")],
                            ["Ctrl+Shift+V", qsTr("Paste")],
                            ["Shift+PgUp", qsTr("Scroll up")],
                            ["Shift+PgDn", qsTr("Scroll down")],
                            ["Ctrl+Shift+L", qsTr("Lock the vault")]
                        ]
                        delegate: RowLayout {
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1
                            spacing: 12
                            // key caps, fixed column so all descriptions start at one line
                            Row {
                                Layout.preferredWidth: 170
                                Layout.alignment: Qt.AlignVCenter
                                spacing: 3
                                Repeater {
                                    model: modelData[0].split("+")
                                    delegate: Row {
                                        spacing: 3
                                        Text { visible: index > 0; text: "+"; color: Theme.textFaint; font.pixelSize: 11; anchors.verticalCenter: parent.verticalCenter }
                                        Rectangle {
                                            implicitWidth: capText.implicitWidth + 12
                                            implicitHeight: 22
                                            radius: 5
                                            color: Theme.surface2
                                            border.color: Theme.border
                                            Rectangle { anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.right: parent.right; height: 2; radius: 5; color: Theme.border }
                                            Text { id: capText; anchors.centerIn: parent; text: modelData; color: Theme.text; font.pixelSize: 11; font.family: Theme.monoFont }
                                        }
                                    }
                                }
                            }
                            Text { Layout.fillWidth: true; Layout.alignment: Qt.AlignVCenter; text: modelData[1]; color: Theme.textDim; font.pixelSize: 12; wrapMode: Text.WordWrap }
                        }
                    }
                }
            }
            // ---------------------------------------------------------
            Section {
                title: qsTr("About")
                glyph: Theme.iconInfo
                RowLayout {
                    spacing: 14
                    Logo { size: 48 }
                    ColumnLayout {
                        spacing: 2
                        Text { text: App.appName + " " + App.version; color: Theme.text; font.pixelSize: 15; font.weight: Font.DemiBold }
                        Text { text: qsTr("Open-source SSH client · MIT licence"); color: Theme.textDim; font.pixelSize: 12 }
                        Text { text: "libssh · libvterm · libsodium · OpenSSL · Qt"; color: Theme.textFaint; font.pixelSize: 11 }
                    }
                }
            }

            Item { height: 20 }
        }
    }

    Modal {
        id: restorePopup
        property url file
        title: qsTr("Restore vault from backup")
        subtitle: qsTr("The current vault is kept in the backups folder first. Enter the master password of the backup.")
        onOpened: { restorePw.text = ""; restoreErr.text = ""; restorePw.field.forceActiveFocus() }
        Text { text: String(restorePopup.file).replace("file:///", ""); color: Theme.textDim; font.pixelSize: 11; font.family: Theme.monoFont; Layout.fillWidth: true; elide: Text.ElideMiddle }
        AppField { id: restorePw; Layout.fillWidth: true; label: qsTr("Master password of the backup"); secret: true; onAccepted: restoreOk.clicked() }
        Text { id: restoreErr; visible: text !== ""; color: Theme.danger; font.pixelSize: 12 }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            AppButton { text: qsTr("Cancel"); variant: "ghost"; onClicked: restorePopup.close() }
            AppButton {
                id: restoreOk
                text: qsTr("Restore"); variant: "danger"
                onClicked: {
                    const e = App.restoreVault(restorePopup.file, restorePw.text)
                    if (e !== "") { restoreErr.text = e; return }
                    restorePopup.close()
                    vaultMsg.show("", qsTr("Vault restored."))
                }
            }
        }
    }
}
