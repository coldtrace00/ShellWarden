import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import ShellWarden

ApplicationWindow {
    id: win
    width: 1320
    height: 820
    // Small enough for Windows Snap (half / quarter screen) at 125–150 % scaling;
    // if the minimum is larger than the snapped size, Windows cuts the content off.
    minimumWidth: 420
    minimumHeight: 380
    visible: true
    title: currentSession ? currentSession.label + " — " + App.appName : App.appName
    color: Theme.bg

    // -1 = vault pages, otherwise index of the session tab
    property int currentTab: -1
    property int splitTab: -1          // second pane (split view), -1 = none
    property string page: "hosts"
    readonly property var currentSession: currentTab >= 0 ? App.sessions.at(currentTab) : null
    // Narrow window (half screen, high DPI): icons-only side menu automatically.
    readonly property bool collapsed: App.settings.sidebarCollapsed || width < 1000
    readonly property bool splitActive: currentTab >= 0 && splitTab >= 0 && splitTab !== currentTab
                                        && splitTab < App.sessions.count

    palette.window: Theme.bg
    palette.windowText: Theme.text
    palette.base: Theme.surface2
    palette.alternateBase: Theme.surface
    palette.text: Theme.text
    palette.button: Theme.surface2
    palette.buttonText: Theme.text
    palette.highlight: Theme.accent
    palette.highlightedText: "#0b1214"
    palette.placeholderText: Theme.textFaint
    palette.light: Theme.surfaceHover
    palette.midlight: Theme.surface2
    palette.mid: Theme.border
    palette.dark: Theme.border
    palette.shadow: "#000000"
    palette.toolTipBase: Theme.surface2
    palette.toolTipText: Theme.text
    palette.brightText: "#ffffff"

    font.pixelSize: 13

    Connections {
        target: App
        function onSessionOpened(index) { win.currentTab = index }
        function onNotify(message, isError) { toast.show(message, isError) }
        function onVaultStateChanged() { if (!App.vaultUnlocked) win.page = "hosts" }
        function onConnectChallenge(hostId, label) { challenge.ask(hostId, label) }
    }

    // Keyboard shortcuts use Ctrl+Shift so that plain Ctrl+<key> always
    // reaches the remote shell.
    Shortcut { sequence: "Ctrl+Shift+T"; onActivated: win.currentTab = -1 }
    Shortcut { sequence: "Ctrl+Shift+W"; enabled: win.currentTab >= 0; onActivated: win.closeTab(win.currentTab) }
    Shortcut { sequence: "Ctrl+Tab"; onActivated: win.cycleTab(1) }
    Shortcut { sequence: "Ctrl+Shift+Tab"; onActivated: win.cycleTab(-1) }
    Shortcut { sequence: "Ctrl+Shift+L"; enabled: App.vaultUnlocked; onActivated: App.lockVault() }
    Shortcut { sequence: "Ctrl+Shift+B"; onActivated: App.setSetting("sidebarCollapsed", !win.collapsed) }
    Shortcut { sequence: "Ctrl+Shift+D"; enabled: win.currentTab >= 0; onActivated: win.toggleSplit(win.currentTab) }
    // quick search: Ctrl+K outside the terminal (the shell uses Ctrl+K), Ctrl+Shift+K everywhere
    Shortcut { sequences: ["Ctrl+Shift+K", "Ctrl+K"]; enabled: App.vaultUnlocked; onActivated: quickSearch.open() }
    QuickSearch {
        id: quickSearch
        currentTab: win.currentTab
        onOpenTab: (i) => { win.currentTab = i; App.sessions.at(i).detached = false }
        onOpenPage: (p) => { if (p === "known") App.refreshKnownHosts(); win.page = p; win.currentTab = -1 }
    }

    function cycleTab(dir) {
        const n = App.sessions.count
        if (n === 0) return
        let t = win.currentTab + dir
        if (t >= n) t = -1
        if (t < -1) t = n - 1
        win.currentTab = t
    }

    function closeTab(index) {
        if (win.splitTab === index) win.splitTab = -1
        else if (win.splitTab > index) win.splitTab -= 1
        App.closeSession(index)
        if (win.currentTab >= App.sessions.count)
            win.currentTab = App.sessions.count - 1
        else if (win.currentTab > index)
            win.currentTab -= 1
    }

    function moveTab(from, to) {
        if (to < 0 || to >= App.sessions.count || from === to) return
        const cur = win.currentTab === from ? to : (win.currentTab === to ? from : win.currentTab)
        const spl = win.splitTab === from ? to : (win.splitTab === to ? from : win.splitTab)
        App.moveSession(from, to)
        win.currentTab = cur
        win.splitTab = spl
    }

    // Split: show `index` next to the current tab (or pick a partner).
    function toggleSplit(index) {
        if (win.splitActive) { win.splitTab = -1; return }
        if (App.sessions.count < 2) { toast.show(qsTr("Open a second session to split the view."), false); return }
        win.splitTab = index !== win.currentTab ? index : (win.currentTab === 0 ? 1 : 0)
        if (win.currentTab < 0) win.currentTab = 0
    }

    // ---------------------------------------------------------------------
    // Vault gate (create / unlock)
    // ---------------------------------------------------------------------
    VaultGate {
        anchors.fill: parent
        visible: !App.vaultUnlocked && App.sessions.count === 0
        z: 10
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0
        visible: App.vaultUnlocked || App.sessions.count > 0

        // -----------------------------------------------------------------
        // Sidebar (collapsible)
        // -----------------------------------------------------------------
        Rectangle {
            id: sidebar
            Layout.fillHeight: true
            Layout.preferredWidth: win.collapsed ? 64 : 236
            color: Theme.sidebar
            clip: true
            Behavior on Layout.preferredWidth { NumberAnimation { duration: 180; easing.type: Easing.OutCubic } }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 3

                RowLayout {
                    Layout.fillWidth: true
                    Layout.topMargin: 6
                    Layout.bottomMargin: 14
                    spacing: 10
                    Logo {
                        size: 34
                        Layout.alignment: Qt.AlignVCenter
                        Layout.leftMargin: win.collapsed ? 5 : 4
                        MouseArea {
                            anchors.fill: parent
                            enabled: win.collapsed
                            cursorShape: Qt.PointingHandCursor
                            onClicked: App.setSetting("sidebarCollapsed", false)
                        }
                    }
                    ColumnLayout {
                        visible: !win.collapsed
                        spacing: 0
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignVCenter
                        Text { textFormat: Text.PlainText; text: App.appName; color: Theme.text; font.pixelSize: 16; font.weight: Font.DemiBold }
                        Text { textFormat: Text.PlainText; text: "v" + App.version + (App.portable ? " · portable" : ""); color: Theme.textFaint; font.pixelSize: 10 }
                    }
                    AbstractButton {
                        id: collapseBtn
                        visible: !win.collapsed
                        // same height as the logo, centred on it
                        implicitWidth: 34; implicitHeight: 34
                        Layout.alignment: Qt.AlignVCenter
                        hoverEnabled: true
                        onClicked: App.setSetting("sidebarCollapsed", true)
                        background: Rectangle { radius: 8; color: collapseBtn.hovered ? Theme.surface2 : "transparent"
                                                border.color: collapseBtn.hovered ? Theme.border : "transparent" }
                        contentItem: Item { IconLabel { anchors.centerIn: parent; glyph: Theme.iconSidebar; size: 19; color: collapseBtn.hovered ? Theme.text : Theme.textDim } }
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Hide menu (Ctrl+Shift+B)")
                    }
                }

                SideNavButton {
                    text: qsTr("Hosts"); glyph: Theme.iconServer; compact: win.collapsed
                    active: win.currentTab < 0 && win.page === "hosts"
                    badge: App.vaultUnlocked && App.hosts.count > 0 ? App.hosts.count : ""
                    enabled: App.vaultUnlocked
                    onClicked: { win.page = "hosts"; win.currentTab = -1 }
                }
                SideNavButton {
                    text: qsTr("Keychain"); glyph: Theme.iconKey; compact: win.collapsed
                    active: win.currentTab < 0 && win.page === "keys"
                    badge: App.vaultUnlocked && App.keys.count > 0 ? App.keys.count : ""
                    enabled: App.vaultUnlocked
                    onClicked: { win.page = "keys"; win.currentTab = -1 }
                }
                SideNavButton {
                    text: qsTr("Proxies"); glyph: Theme.iconGlobe; compact: win.collapsed
                    active: win.currentTab < 0 && win.page === "proxies"
                    badge: App.vaultUnlocked && App.proxies.count > 0 ? App.proxies.count : ""
                    enabled: App.vaultUnlocked
                    onClicked: { win.page = "proxies"; win.currentTab = -1 }
                }
                SideNavButton {
                    text: qsTr("Snippets"); glyph: Theme.iconSnippet; compact: win.collapsed
                    active: win.currentTab < 0 && win.page === "snippets"
                    badge: App.vaultUnlocked && App.snippets.count > 0 ? App.snippets.count : ""
                    enabled: App.vaultUnlocked
                    onClicked: { win.page = "snippets"; win.currentTab = -1 }
                }
                SideNavButton {
                    text: qsTr("Known hosts"); glyph: Theme.iconFingerprint; compact: win.collapsed
                    active: win.currentTab < 0 && win.page === "known"
                    onClicked: { App.refreshKnownHosts(); win.page = "known"; win.currentTab = -1 }
                }
                SideNavButton {
                    text: qsTr("Activity log"); glyph: Theme.iconLog; compact: win.collapsed
                    active: win.currentTab < 0 && win.page === "log"
                    onClicked: { win.page = "log"; win.currentTab = -1 }
                }
                SideNavButton {
                    text: qsTr("Settings"); glyph: Theme.iconSettings; compact: win.collapsed
                    active: win.currentTab < 0 && win.page === "settings"
                    badge: App.updater.state === "available" ? "1" : ""
                    onClicked: { win.page = "settings"; win.currentTab = -1 }
                }

                Item { Layout.fillHeight: true }

                // Update available
                Rectangle {
                    visible: App.updater.state === "available" && !win.collapsed
                    Layout.fillWidth: true
                    implicitHeight: updRow.implicitHeight + 16
                    radius: 9
                    color: Theme.accentDim
                    border.color: Theme.accent
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: updateDialog.show("available") }
                    RowLayout {
                        id: updRow
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 8
                        IconLabel { glyph: Theme.iconUpdate; color: Theme.accent; size: 16 }
                        Text {
                            textFormat: Text.PlainText
                            Layout.fillWidth: true
                            text: qsTr("Update %1 available").arg(String(App.updater.latestVersion).replace(/^v/i, ""))
                            color: Theme.text; font.pixelSize: 12; elide: Text.ElideRight
                        }
                    }
                }

                // Kill switch status
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: win.collapsed ? 40 : ksRow.implicitHeight + 16
                    radius: 9
                    color: App.settings.killSwitch ? Theme.okDim : Theme.surface
                    border.color: App.settings.killSwitch ? Theme.ok : Theme.border
                    border.width: 1
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        enabled: App.vaultUnlocked
                        onClicked: { win.page = "proxies"; win.currentTab = -1 }
                        hoverEnabled: true
                        ToolTip.visible: win.collapsed && containsMouse
                        ToolTip.text: qsTr("Kill switch") + ": " + (App.settings.killSwitch ? qsTr("On · proxy only") : qsTr("Off"))
                    }
                    IconLabel {
                        visible: win.collapsed
                        anchors.centerIn: parent
                        glyph: App.settings.killSwitch ? Theme.iconShieldCheck : Theme.iconShield
                        size: 18
                        color: App.settings.killSwitch ? Theme.ok : Theme.textFaint
                    }
                    RowLayout {
                        id: ksRow
                        visible: !win.collapsed
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 9
                        IconLabel { glyph: App.settings.killSwitch ? Theme.iconShieldCheck : Theme.iconShield; size: 18
                                    color: App.settings.killSwitch ? Theme.ok : Theme.textFaint }
                        ColumnLayout {
                            spacing: 0
                            Layout.fillWidth: true
                            Text { textFormat: Text.PlainText; text: qsTr("Kill switch"); color: Theme.text; font.pixelSize: 12; font.weight: Font.Medium }
                            Text {
                                textFormat: Text.PlainText
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                                text: App.settings.killSwitch ? (App.firewallActive ? qsTr("On · firewall lock") : qsTr("On · proxy only")) : qsTr("Off")
                                color: App.settings.killSwitch ? Theme.ok : Theme.textFaint
                                font.pixelSize: 11
                            }
                        }
                    }
                }

                SideNavButton {
                    compact: win.collapsed
                    text: App.vaultUnlocked ? qsTr("Lock") : qsTr("Unlock")
                    glyph: App.vaultUnlocked ? Theme.iconLock : Theme.iconUnlock
                    onClicked: {
                        if (App.vaultUnlocked) App.lockVault()
                        else unlockPopup.open()
                    }
                }
            }
            Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.border; opacity: 0.6 }
        }

        // -----------------------------------------------------------------
        // Content
        // -----------------------------------------------------------------
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // Tab bar
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 52
                color: Theme.bg

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    spacing: 6

                    AbstractButton {
                        id: homeTab
                        implicitHeight: 40
                        implicitWidth: homeRow.implicitWidth + 24
                        hoverEnabled: true
                        onClicked: win.currentTab = -1
                        background: Rectangle {
                            radius: 9
                            color: win.currentTab < 0 ? Theme.surface2 : (homeTab.hovered ? Theme.surface : "transparent")
                            border.width: win.currentTab < 0 ? 1 : 0
                            border.color: Theme.border
                        }
                        contentItem: Item {
                            RowLayout {
                                id: homeRow
                                anchors.centerIn: parent
                                spacing: 8
                                IconLabel { glyph: App.vaultUnlocked ? Theme.iconUnlock : Theme.iconLock; size: 15; color: Theme.accent }
                                Text { textFormat: Text.PlainText; text: qsTr("Vault"); color: Theme.text; font.pixelSize: 12 }
                            }
                        }
                    }

                    Rectangle { width: 1; height: 24; color: Theme.border; visible: App.sessions.count > 0 }

                    ListView {
                        id: tabsView
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        orientation: ListView.Horizontal
                        spacing: 4
                        clip: true
                        model: App.sessions
                        boundsBehavior: Flickable.StopAtBounds
                        interactive: contentWidth > width
                        currentIndex: win.currentTab
                        highlightFollowsCurrentItem: false
                        onCurrentIndexChanged: if (currentIndex >= 0) positionViewAtIndex(currentIndex, ListView.Contain)
                        move: Transition { NumberAnimation { properties: "x"; duration: 160; easing.type: Easing.OutCubic } }
                        displaced: Transition { NumberAnimation { properties: "x"; duration: 160; easing.type: Easing.OutCubic } }
                        add: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 160 } }
                        delegate: SessionTabButton {
                            anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                            session: model.session
                            active: win.currentTab === index
                            split: win.splitActive && win.splitTab === index
                            onClicked: win.currentTab = index
                            onCloseRequested: win.closeTab(index)
                            onDragStep: (dir) => win.moveTab(index, index + dir)
                            onContextRequested: { tabMenu.index = index; tabMenu.popup() }
                        }
                        WheelHandler {
                            onWheel: (e) => tabsView.flick(e.angleDelta.y * 6, 0)
                        }
                    }

                    AbstractButton {
                        id: splitBtn
                        visible: App.sessions.count > 1
                        implicitWidth: 36; implicitHeight: 34
                        hoverEnabled: true
                        onClicked: win.toggleSplit(win.currentTab)
                        background: Rectangle { radius: 8; color: win.splitActive ? Theme.accentDim : (splitBtn.hovered ? Theme.surface : "transparent") }
                        contentItem: Item { IconLabel { anchors.centerIn: parent; glyph: Theme.iconSplit; size: 17; color: win.splitActive ? Theme.accent : Theme.textDim } }
                        ToolTip.visible: hovered
                        ToolTip.text: win.splitActive ? qsTr("Close split view (Ctrl+Shift+D)") : qsTr("Split view: two sessions side by side (Ctrl+Shift+D)")
                    }

                    // local terminal (only when turned on in Settings)
                    AbstractButton {
                        id: localBtn
                        visible: !!App.settings.localTerminal
                        implicitWidth: 36; implicitHeight: 34
                        hoverEnabled: true
                        onClicked: App.openLocalTerminal("")
                        background: Rectangle { radius: 8; color: localBtn.hovered ? Theme.surface : "transparent"; border.color: localBtn.hovered ? Theme.border : "transparent" }
                        contentItem: Item { IconLabel { anchors.centerIn: parent; glyph: Theme.iconTerminal; size: 17; color: Theme.textDim } }
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Terminal of this computer")
                    }

                    // Quick connect
                    TextField {
                        id: quick
                        Layout.preferredWidth: 250
                        implicitHeight: 34
                        placeholderText: qsTr("user@host:port · Ctrl+Shift+K search")
                        placeholderTextColor: Theme.textFaint
                        color: Theme.text
                        font.pixelSize: 12
                        leftPadding: 32
                        selectByMouse: true
                        background: Rectangle {
                            radius: 9
                            color: Theme.surface
                            border.width: 1
                            border.color: quick.activeFocus ? Theme.accent : Theme.border
                            IconLabel {
                                anchors.left: parent.left
                                anchors.leftMargin: 10
                                anchors.verticalCenter: parent.verticalCenter
                                glyph: Theme.iconTerminal; size: 14; color: Theme.textDim
                            }
                        }
                        onAccepted: {
                            const err = App.quickConnect(text)
                            if (err !== "") toast.show(err, true)
                            else text = ""
                        }
                    }
                }
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.border }
            }

            // Pages + terminals
            Item {
                id: contentArea
                Layout.fillWidth: true
                Layout.fillHeight: true

                Loader {
                    anchors.fill: parent
                    visible: win.currentTab < 0
                    active: App.vaultUnlocked || win.page === "known" || win.page === "settings" || win.page === "log"
                    sourceComponent: {
                        switch (win.page) {
                        case "keys": return keysPage
                        case "proxies": return proxiesPage
                        case "snippets": return snippetsPage
                        case "known": return knownPage
                        case "log": return logPage
                        case "settings": return settingsPage
                        default: return hostsPage
                        }
                    }
                }

                Column {
                    anchors.centerIn: parent
                    visible: win.currentTab < 0 && !App.vaultUnlocked && ["known", "settings", "log"].indexOf(win.page) < 0
                    spacing: 14
                    IconLabel { anchors.horizontalCenter: parent.horizontalCenter; glyph: Theme.iconLock; size: 38; color: Theme.textFaint }
                    Text { textFormat: Text.PlainText; text: qsTr("The vault is locked. Open sessions keep running."); color: Theme.textDim; font.pixelSize: 14 }
                    AppButton {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: qsTr("Unlock"); variant: "primary"; glyph: Theme.iconUnlock
                        onClicked: unlockPopup.open()
                    }
                }

                Repeater {
                    model: App.sessions
                    delegate: Item {
                        id: pane
                        required property var model
                        required property int index
                        readonly property var session: model.session
                        readonly property bool isLeft: win.currentTab === index
                        readonly property bool isRight: win.splitActive && win.splitTab === index
                        visible: (isLeft || isRight) && session && !session.detached
                        x: isRight ? contentArea.width / 2 + 1 : 0
                        y: 0
                        width: win.splitActive && (isLeft || isRight) ? contentArea.width / 2 - 1 : contentArea.width
                        height: contentArea.height

                        TerminalPage {
                            anchors.fill: parent
                            session: pane.session
                            highlighted: win.splitActive && pane.isLeft
                            primary: !pane.isRight
                            onCloseRequested: win.closeTab(pane.index)
                            onDetachRequested: pane.session.detached = true
                            // the right pane keeps its place; only the left one is "the current tab"
                            onFocusRequested: if (!pane.isRight) win.currentTab = pane.index
                        }
                    }
                }
                // split divider
                Rectangle {
                    visible: win.splitActive
                    x: contentArea.width / 2 - 1
                    width: 2
                    height: contentArea.height
                    color: Theme.border
                }

                // the current tab lives in its own window
                Column {
                    anchors.centerIn: parent
                    visible: win.currentSession && win.currentSession.detached && !win.splitActive
                    spacing: 14
                    IconLabel { anchors.horizontalCenter: parent.horizontalCenter; glyph: Theme.iconPopout; size: 38; color: Theme.textFaint }
                    Text { textFormat: Text.PlainText; anchors.horizontalCenter: parent.horizontalCenter; text: qsTr("This session is open in a separate window."); color: Theme.textDim; font.pixelSize: 14 }
                    AppButton {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: qsTr("Bring back"); glyph: Theme.iconDock; variant: "primary"
                        onClicked: win.currentSession.detached = false
                    }
                }
            }
        }
    }

    // Detached terminal windows
    Instantiator {
        model: App.sessions
        delegate: Window {
            id: dw
            required property var model
            required property int index
            readonly property var session: model.session
            visible: session && session.detached
            width: 1000
            height: 640
            minimumWidth: 480
            minimumHeight: 300
            title: session ? session.label + " — " + App.appName : App.appName
            color: Theme.terminalBg
            // a separate window does not inherit the main window's dark palette
            palette.window: Theme.bg
            palette.windowText: Theme.text
            palette.base: Theme.surface2
            palette.alternateBase: Theme.surface
            palette.text: Theme.text
            palette.button: Theme.surface2
            palette.buttonText: Theme.text
            palette.highlight: Theme.accent
            palette.highlightedText: "#0b1214"
            palette.placeholderText: Theme.textFaint
            palette.light: Theme.surfaceHover
            palette.midlight: Theme.surface2
            palette.mid: Theme.border
            palette.dark: Theme.border
            palette.toolTipBase: Theme.surface2
            palette.toolTipText: Theme.text
            onVisibleChanged: if (visible) { requestActivate(); raise() }
            onClosing: (close) => { close.accepted = false; session.detached = false }
            Loader {
                anchors.fill: parent
                active: dw.visible
                sourceComponent: TerminalPage {
                    session: dw.session
                    inWindow: true
                    onDockRequested: dw.session.detached = false
                    onCloseRequested: { dw.session.detached = false; win.closeTab(dw.index) }
                }
            }
        }
    }

    AppMenu {
        id: tabMenu
        property int index: -1
        AppMenuItem { text: qsTr("Duplicate session"); onTriggered: App.duplicateSession(tabMenu.index) }
        AppMenuItem { text: qsTr("Open in a separate window"); onTriggered: App.sessions.at(tabMenu.index).detached = true }
        AppMenuItem { text: qsTr("Show in split view"); enabled: tabMenu.index !== win.currentTab; onTriggered: { win.splitTab = tabMenu.index } }
        AppMenuItem { text: qsTr("Reconnect"); onTriggered: App.sessions.at(tabMenu.index).reconnect() }
        MenuSeparator {}
        AppMenuItem { text: qsTr("Move left"); enabled: tabMenu.index > 0; onTriggered: win.moveTab(tabMenu.index, tabMenu.index - 1) }
        AppMenuItem { text: qsTr("Move right"); enabled: tabMenu.index < App.sessions.count - 1; onTriggered: win.moveTab(tabMenu.index, tabMenu.index + 1) }
        MenuSeparator {}
        AppMenuItem { text: qsTr("Close"); onTriggered: win.closeTab(tabMenu.index) }
    }

    Component { id: hostsPage; HostsPage {} }
    Component { id: keysPage; KeychainPage {} }
    Component { id: proxiesPage; ProxiesPage {} }
    Component { id: snippetsPage; SnippetsPage {} }
    Component { id: knownPage; KnownHostsPage {} }
    Component { id: logPage; LogPage {} }
    Component { id: settingsPage; SettingsPage {} }

    // "Ask the master password to connect" (host option)
    Modal {
        id: challenge
        property string hostId: ""
        property string label: ""
        property bool done: false
        closePolicy: Popup.NoAutoClose
        title: qsTr("Confirm connection to %1").arg(label)
        subtitle: qsTr("This host asks for the master password before every connection.")
        function ask(id, l) {
            hostId = id; label = l; done = false
            masterField.text = ""; chErr.text = ""
            open()
            masterField.field.forceActiveFocus()
        }
        function submit() {
            const e = App.answerChallenge(hostId, masterField.text)
            if (e !== "") { chErr.text = e; masterField.text = ""; masterField.field.forceActiveFocus(); return }
            done = true
            close()
        }
        onClosed: if (!done) App.cancelChallenge(hostId)

        AppField {
            id: masterField
            Layout.fillWidth: true
            label: qsTr("Master password")
            secret: true
            onAccepted: challenge.submit()
        }
        Text { textFormat: Text.PlainText; id: chErr; visible: text !== ""; color: Theme.danger; font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.WordWrap }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            AppButton { text: qsTr("Cancel"); variant: "ghost"; onClicked: challenge.close() }
            AppButton { text: qsTr("Connect"); variant: "primary"; glyph: Theme.iconPlug; onClicked: challenge.submit() }
        }
    }

    // Unlock while sessions are open
    Modal {
        id: unlockPopup
        title: qsTr("Unlock vault")
        onOpened: { unlockField.text = ""; unlockErr.text = ""; unlockField.field.forceActiveFocus() }
        AppField {
            id: unlockField
            Layout.fillWidth: true
            label: qsTr("Master password")
            secret: true
            onAccepted: App.unlockVault(text)
        }
        Text { textFormat: Text.PlainText; id: unlockErr; color: Theme.danger; visible: text !== ""; font.pixelSize: 12 }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            AppButton { text: qsTr("Cancel"); variant: "ghost"; onClicked: unlockPopup.close() }
            AppButton {
                text: App.busy ? qsTr("Unlocking…") : qsTr("Unlock")
                variant: "primary"; enabled: !App.busy
                onClicked: App.unlockVault(unlockField.text)
            }
        }
        Connections {
            target: App
            function onVaultError(message) { if (unlockPopup.opened) unlockErr.text = message }
            function onVaultStateChanged() { if (App.vaultUnlocked) unlockPopup.close() }
        }
    }

    // New version found at start, or "what's new" after an update
    Modal {
        id: updateDialog
        property string mode: "available"     // available | updated | current
        readonly property bool aboutThis: mode === "updated" || mode === "current"
        property string pending: ""           // asked for while the vault was locked
        readonly property var up: App.updater
        readonly property string latest: String(up.latestVersion).replace(/^v/i, "")
        title: aboutThis ? qsTr("What's new in ShellWarden %1").arg(App.version)
                                  : qsTr("ShellWarden %1 is available").arg(latest)
        subtitle: mode === "updated" ? qsTr("ShellWarden was updated to version %1.").arg(App.version)
                  : mode === "current" ? qsTr("You have the latest version (%1).").arg(App.version)
                                     : qsTr("You have version %1. The download is checked before it is installed.").arg(App.version)
        width: Math.min(560, (parent ? parent.width : 560) - 40)
        function show(m) { pending = ""; mode = m; open() }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(notesText.implicitHeight + 24, 320)
            radius: 9
            color: Theme.bg
            border.color: Theme.border
            ScrollView {
                anchors.fill: parent
                anchors.margins: 12
                clip: true
                contentWidth: availableWidth
                Text {
                    id: notesText
                    width: parent.width
                    text: updateDialog.up.localizedNotes !== "" ? updateDialog.up.localizedNotes
                                                                : qsTr("No description for this release.")
                    textFormat: Text.MarkdownText
                    wrapMode: Text.WordWrap
                    color: Theme.text
                    linkColor: Theme.accent
                    font.pixelSize: 13
                    onLinkActivated: (link) => { if (String(link).indexOf("https://") === 0) Qt.openUrlExternally(link) }
                }
            }
        }
        ProgressBar {
            visible: updateDialog.up.state === "downloading"
            Layout.fillWidth: true
            value: updateDialog.up.progress
        }
        Text {
            textFormat: Text.PlainText
            visible: updateDialog.mode === "available" && (updateDialog.up.state === "downloading" || updateDialog.up.state === "error")
            text: updateDialog.up.state === "error" ? updateDialog.up.error
                                                    : qsTr("Downloading… %1%").arg(Math.round(updateDialog.up.progress * 100))
            color: updateDialog.up.state === "error" ? Theme.danger : Theme.textDim
            font.pixelSize: 12
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            AppButton {
                visible: updateDialog.mode === "available" && updateDialog.up.state === "available"
                text: qsTr("Skip this version"); variant: "ghost"; compact: true
                onClicked: { App.skipUpdate(updateDialog.up.latestVersion); updateDialog.close() }
            }
            AppButton {
                visible: updateDialog.up.pageUrl !== ""
                text: qsTr("Release page"); variant: "ghost"; compact: true
                onClicked: updateDialog.up.openPage()
            }
            Item { Layout.fillWidth: true }
            AppButton {
                visible: updateDialog.mode === "available"
                text: qsTr("Later"); variant: "ghost"
                onClicked: updateDialog.close()
            }
            AppButton {
                visible: updateDialog.mode === "available" && (updateDialog.up.state === "available" || updateDialog.up.state === "error")
                text: qsTr("Update now"); variant: "primary"; glyph: Theme.iconUpdate
                onClicked: updateDialog.up.download()
            }
            AppButton {
                visible: updateDialog.mode === "available" && updateDialog.up.state === "ready"
                text: qsTr("Install"); variant: "primary"; glyph: Theme.iconUpdate
                onClicked: updateDialog.up.install()
            }
            AppButton {
                visible: updateDialog.aboutThis
                text: qsTr("OK"); variant: "primary"
                onClicked: updateDialog.close()
            }
        }
        Connections {
            target: App
            // shown after the vault is unlocked, so it never gets in the way of the password
            function onUpdateDialogRequested(mode) {
                if (App.vaultUnlocked) updateDialog.show(mode)
                else updateDialog.pending = mode
            }
            function onVaultStateChanged() {
                if (App.vaultUnlocked && updateDialog.pending !== "") updateDialog.show(updateDialog.pending)
            }
        }
    }

    // Toast
    Rectangle {
        id: toast
        property bool isError: false
        function show(msg, err) {
            toastText.text = msg
            isError = err
            opacity = 1
            toastTimer.restart()
        }
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 24
        width: Math.min(toastText.implicitWidth + 40, win.width - 80)
        height: toastText.implicitHeight + 22
        radius: 10
        color: isError ? Theme.dangerDim : Theme.surface2
        border.color: isError ? Theme.danger : Theme.border
        opacity: 0
        visible: opacity > 0
        z: 1000
        Behavior on opacity { NumberAnimation { duration: 180 } }
        Text {
            textFormat: Text.PlainText
            id: toastText
            anchors.centerIn: parent
            width: parent.width - 40
            color: Theme.text
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
            font.pixelSize: 13
        }
        Timer { id: toastTimer; interval: 4500; onTriggered: toast.opacity = 0 }
    }
}
