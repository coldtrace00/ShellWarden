import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ShellWarden

// Put public keys from the keychain on servers (what ssh-copy-id does), for
// many servers at once: one key for all of them, or a different key per server.
// Logging in for the install uses what each host already has (password / key).
Popup {
    id: root
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(860, (parent ? parent.width : 860) - 32)
    height: Math.min(720, (parent ? parent.height : 720) - 32)
    modal: true
    focus: true
    padding: 20
    closePolicy: Popup.CloseOnEscape
    Overlay.modal: Rectangle { color: "#a0070910" }
    background: Rectangle { radius: 14; color: Theme.surface; border.color: Theme.border }

    property bool perHost: false          // false = one key for all servers
    property bool replaceMode: false      // replace oldKeyId with keyId (two safe steps)
    property string oldKeyId: ""
    property string keyId: ""             // the key in "one key" mode
    property var picked: ({})             // hostId -> true
    property var hostKey: ({})            // hostId -> keyId in "different keys" mode
    property bool showResults: false
    property string error: ""
    readonly property var batch: App.batch

    readonly property var keys: { App.keys.count; const a = []; for (let i = 0; i < App.keys.count; ++i) a.push(App.keys.get(i)); return a }
    readonly property var hosts: {
        App.hosts.count
        const out = []
        for (let i = 0; i < App.hosts.count; ++i) {
            const h = App.hosts.get(i)
            if (h.type === "serial" || h.type === "telnet") continue
            out.push({ id: h.id, label: h.label, addr: (h.username ? h.username + "@" : "") + h.hostname,
                       group: h.group || "", keyId: h.keyId || "", protected: App.hostIsProtected(h.id) })
        }
        return out
    }
    readonly property int pickedCount: { let n = 0; for (const k in picked) if (picked[k]) ++n; return n }
    readonly property bool needMaster: hosts.some(h => picked[h.id] && h.protected)
                                       || (replaceMode && App.keyIsProtected(keyId))
    function keyIndex(id) { return Math.max(0, keys.findIndex(k => k.id === id)) }
    function keyFor(hostId) { return perHost ? (hostKey[hostId] || (keys.length ? keys[0].id : "")) : keyId }

    function pickUsersOf(kid) {           // the servers that log in with this key now
        const p = {}
        for (const h of hosts) if (h.keyId === kid) p[h.id] = true
        picked = p
    }
    function rotate() {
        error = ""
        const ids = hosts.filter(h => picked[h.id]).map(h => h.id)
        if (ids.length === 0) { error = qsTr("Tick at least one server."); return }
        if (oldKeyId === keyId) { error = qsTr("The new key must be a different key."); return }
        error = secrets.prepare()
        if (error !== "") return
        error = App.rotateKey(oldKeyId, keyId, ids, needMaster ? secrets.masterPassword : "")
        secrets.clear()
        if (error === "") showResults = true
    }
    function openFor(id) {
        keyId = id || (keys.length ? keys[0].id : "")
        perHost = false
        replaceMode = false
        oldKeyId = keys.length ? keys[0].id : ""
        picked = ({}); hostKey = ({})
        showResults = false; error = ""
        secrets.clear()
        open()
    }
    function install() {
        error = ""
        const pairs = hosts.filter(h => picked[h.id]).map(h => ({ hostId: h.id, keyId: keyFor(h.id) }))
        if (pairs.length === 0) { error = qsTr("Tick at least one server."); return }
        error = secrets.prepare()
        if (error !== "") return
        error = App.installKeys(pairs, needMaster ? secrets.masterPassword : "", useNow.checked)
        secrets.clear()
        if (error === "") showResults = true
    }

    component Chip: AbstractButton {
        id: chip
        property bool on: false
        implicitHeight: 30
        implicitWidth: chipText.implicitWidth + 24
        hoverEnabled: true
        background: Rectangle { radius: 15; color: chip.on ? Theme.accentDim : (chip.hovered ? Theme.surface2 : "transparent"); border.color: chip.on ? Theme.accent : Theme.border }
        contentItem: Text { textFormat: Text.PlainText; id: chipText; text: chip.text; color: chip.on ? Theme.text : Theme.textDim; font.pixelSize: 12
                            horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
    }

    contentItem: ColumnLayout {
        spacing: 12
        RowLayout {
            Layout.fillWidth: true
            IconLabel { glyph: Theme.iconKey; size: 18; color: Theme.accent; Layout.alignment: Qt.AlignTop; Layout.topMargin: 4 }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 1
                Text { textFormat: Text.PlainText; text: root.replaceMode ? qsTr("Replace a key on servers") : qsTr("Install keys on servers"); color: Theme.text; font.pixelSize: 18; font.weight: Font.DemiBold }
                Text {
                    textFormat: Text.PlainText
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.textDim; font.pixelSize: 12
                    text: root.replaceMode
                          ? qsTr("Three steps on each server: 1) log in as now and add the new key; 2) log in with only the new key and, if that works, remove the old one; 3) log in once more with the new key and check that the old one is really gone. Where the new key does not work, the old key stays — you cannot lock yourself out. Only checked servers switch to the new key. Sessions that are open now stay open.")
                          : qsTr("The public key is added to ~/.ssh/authorized_keys on each server — no copying by hand. To get in this one time, each server uses what it already has (password or another key).")
                }
            }
            AppButton { glyph: Theme.iconClose; variant: "ghost"; compact: true; onClicked: root.close() }
        }

        // ============ setup ============
        ColumnLayout {
            visible: !root.showResults
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 10

            // the mode buttons wrap onto a second line when the words are long
            // (uk/ru/fr) or the window is narrow, instead of pushing the dialog wider
            Flow {
                Layout.fillWidth: true
                spacing: 8
                Text { textFormat: Text.PlainText; text: qsTr("Keys:"); color: Theme.textDim; font.pixelSize: 12
                       height: 30; verticalAlignment: Text.AlignVCenter }
                Chip { text: qsTr("One key for all servers"); on: !root.perHost && !root.replaceMode; onClicked: { root.perHost = false; root.replaceMode = false } }
                Chip { text: qsTr("A different key per server"); on: root.perHost && !root.replaceMode; onClicked: { root.perHost = true; root.replaceMode = false } }
                Chip { text: qsTr("Replace an old key"); on: root.replaceMode
                       onClicked: {
                           root.replaceMode = true; root.perHost = false
                           if (root.keyId === root.oldKeyId) {   // start with two different keys
                               const other = root.keys.find(k => k.id !== root.oldKeyId)
                               if (other) root.keyId = other.id
                           }
                           root.pickUsersOf(root.oldKeyId)
                       } }
            }
            RowLayout {
                visible: !root.perHost && !root.replaceMode
                Layout.fillWidth: true
                spacing: 8
                Text { textFormat: Text.PlainText; text: qsTr("Key to install:"); color: Theme.textDim; font.pixelSize: 12 }
                AppCombo {
                    Layout.fillWidth: true
                    Layout.maximumWidth: 420
                    model: root.keys.map(k => k.name + "  ·  " + k.type)
                    currentIndex: root.keyIndex(root.keyId)
                    onActivated: (i) => root.keyId = root.keys[i].id
                }
                Item { Layout.fillWidth: true }
            }
            RowLayout {
                visible: root.replaceMode
                Layout.fillWidth: true
                spacing: 8
                Text { textFormat: Text.PlainText; text: qsTr("Old key:"); color: Theme.textDim; font.pixelSize: 12 }
                AppCombo {
                    Layout.fillWidth: true
                    model: root.keys.map(k => k.name + "  ·  " + k.type)
                    currentIndex: root.keyIndex(root.oldKeyId)
                    onActivated: (i) => { root.oldKeyId = root.keys[i].id; root.pickUsersOf(root.oldKeyId) }
                }
                IconLabel { glyph: Theme.iconArrowRight; size: 16; color: Theme.accent }
                Text { textFormat: Text.PlainText; text: qsTr("New key:"); color: Theme.textDim; font.pixelSize: 12 }
                AppCombo {
                    Layout.fillWidth: true
                    model: root.keys.map(k => k.name + "  ·  " + k.type)
                    currentIndex: root.keyIndex(root.keyId)
                    onActivated: (i) => root.keyId = root.keys[i].id
                }
            }
            Text {
                textFormat: Text.PlainText
                visible: root.replaceMode && root.keys.length < 2
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: qsTr("You need two keys: create the new key first (Keychain → Generate).")
                color: Theme.warn; font.pixelSize: 12
            }
            Text {
                textFormat: Text.PlainText
                visible: root.keys.length === 0
                text: qsTr("The keychain is empty: create or import a key first.")
                color: Theme.warn; font.pixelSize: 12
            }

            RowLayout {
                Layout.fillWidth: true
                Text { textFormat: Text.PlainText; Layout.fillWidth: true; text: qsTr("Servers (%1 chosen)").arg(root.pickedCount); color: Theme.textDim; font.pixelSize: 12; font.weight: Font.Medium }
                AppButton { text: qsTr("All"); compact: true; variant: "ghost"; onClicked: { const p = {}; for (const h of root.hosts) p[h.id] = true; root.picked = p } }
                AppButton { text: qsTr("None"); compact: true; variant: "ghost"; onClicked: root.picked = ({}) }
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: 10
                color: Theme.bg
                border.color: Theme.border
                clip: true
                ListView {
                    id: hostList
                    anchors.fill: parent
                    anchors.margins: 4
                    clip: true
                    model: root.hosts
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {}
                    delegate: Rectangle {
                        id: hr
                        width: hostList.width
                        height: 46
                        radius: 8
                        readonly property bool on: !!root.picked[modelData.id]
                        color: on ? Theme.accentDim : (hrHover.hovered ? Theme.surface2 : "transparent")
                        HoverHandler { id: hrHover }
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 10; anchors.rightMargin: 8
                            spacing: 10
                            AppCheckBox {
                                checked: hr.on
                                onToggled: { const p = Object.assign({}, root.picked); p[modelData.id] = checked; root.picked = p }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 0
                                Text { textFormat: Text.PlainText; Layout.fillWidth: true; text: modelData.label; color: Theme.text; font.pixelSize: 13; elide: Text.ElideRight }
                                Text { textFormat: Text.PlainText; Layout.fillWidth: true; text: modelData.addr + (modelData.group ? " · " + modelData.group : ""); color: Theme.textFaint; font.pixelSize: 11; font.family: Theme.monoFont; elide: Text.ElideRight }
                            }
                            IconLabel { visible: modelData.protected; glyph: Theme.iconShield; size: 12; color: Theme.warn }
                            AppCombo {
                                visible: root.perHost
                                Layout.preferredWidth: 220
                                model: root.keys.map(k => k.name)
                                currentIndex: root.keyIndex(root.keyFor(modelData.id))
                                onActivated: (i) => {
                                    const m = Object.assign({}, root.hostKey); m[modelData.id] = root.keys[i].id; root.hostKey = m
                                    const p = Object.assign({}, root.picked); p[modelData.id] = true; root.picked = p
                                }
                            }
                        }
                        TapHandler { onTapped: { const p = Object.assign({}, root.picked); p[modelData.id] = !hr.on; root.picked = p } }
                    }
                }
            }
            AppCheckBox {
                id: useNow
                visible: !root.replaceMode
                checked: true
                text: qsTr("After it works, log in to these servers with the key (instead of the password)")
            }
            BatchSecrets {
                id: secrets
                Layout.fillWidth: true
                hostIds: root.hosts.filter(h => root.picked[h.id]).map(h => h.id)
                extraKeyIds: root.replaceMode ? [root.keyId] : []
                needMaster: root.needMaster
                onSubmit: root.install()
            }
            Text { textFormat: Text.PlainText; visible: root.error !== ""; Layout.fillWidth: true; wrapMode: Text.WordWrap; text: root.error; color: Theme.danger; font.pixelSize: 12 }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                AppButton { text: qsTr("Close"); variant: "ghost"; onClicked: root.close() }
                AppButton {
                    text: root.replaceMode ? qsTr("Replace on %n server(s)", "", root.pickedCount) : qsTr("Install on %n server(s)", "", root.pickedCount)
                    variant: "primary"; glyph: Theme.iconKey
                    enabled: root.pickedCount > 0 && root.keys.length > (root.replaceMode ? 1 : 0)
                    onClicked: root.replaceMode ? root.rotate() : root.install()
                }
            }
        }

        // ============ results ============
        ColumnLayout {
            visible: root.showResults
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 10
            RowLayout {
                Layout.fillWidth: true
                spacing: 14
                BusyIndicator { visible: root.batch.running; running: visible; implicitWidth: 22; implicitHeight: 22 }
                Text { textFormat: Text.PlainText; Layout.fillWidth: true; wrapMode: Text.WordWrap; text: root.replaceMode && App.rotatePhase === "adding" ? qsTr("Step 1 of 3: adding the new key… %1 of %2").arg(root.batch.doneCount).arg(root.batch.results.length)
                           : root.replaceMode && App.rotatePhase === "removing" ? qsTr("Step 2 of 3: logging in with the new key, removing the old one… %1 of %2").arg(root.batch.doneCount).arg(root.batch.results.length)
                           : root.replaceMode && App.rotatePhase === "verifying" ? qsTr("Step 3 of 3: a new login with the new key to check… %1 of %2").arg(root.batch.doneCount).arg(root.batch.results.length)
                           : root.batch.running ? qsTr("Installing… %1 of %2 done").arg(root.batch.doneCount).arg(root.batch.results.length) : qsTr("Finished")
                       color: Theme.text; font.pixelSize: 13; font.weight: Font.DemiBold }
                Text { textFormat: Text.PlainText; text: "✓ " + root.batch.okCount; color: Theme.ok; font.pixelSize: 13; font.weight: Font.DemiBold }
                Text { textFormat: Text.PlainText; text: "✗ " + root.batch.failCount; color: root.batch.failCount > 0 ? Theme.danger : Theme.textFaint; font.pixelSize: 13; font.weight: Font.DemiBold }
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                radius: 10
                color: Theme.bg
                border.color: Theme.border
                clip: true
                ListView {
                    id: resList
                    anchors.fill: parent
                    anchors.margins: 1
                    clip: true
                    model: root.batch.results
                    ScrollBar.vertical: ScrollBar {}
                    delegate: Rectangle {
                        width: resList.width
                        height: 48
                        color: index % 2 ? "transparent" : Theme.tint(Theme.surface2, "40")
                        readonly property color tint: modelData.state === "ok" ? Theme.ok : modelData.state === "running" ? Theme.accent
                                                     : modelData.state === "queued" ? Theme.textFaint : modelData.state === "skipped" ? Theme.warn : Theme.danger
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 12; anchors.rightMargin: 12
                            spacing: 10
                            Rectangle {
                                width: 22; height: 22; radius: 11
                                color: Theme.tint(parent.parent.tint, "2e")
                                IconLabel { anchors.centerIn: parent; visible: modelData.state !== "running" && modelData.state !== "queued"
                                            glyph: modelData.state === "ok" ? Theme.iconCheck : modelData.state === "skipped" ? Theme.iconWarning : Theme.iconClose
                                            size: 12; color: parent.parent.parent.tint }
                                BusyIndicator { anchors.centerIn: parent; visible: modelData.state === "running"; running: visible; width: 18; height: 18 }
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 0
                                Text { textFormat: Text.PlainText; Layout.fillWidth: true; text: modelData.label; color: Theme.text; font.pixelSize: 13; elide: Text.ElideRight }
                                Text {
                                    textFormat: Text.PlainText
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                    font.pixelSize: 11
                                    color: modelData.state === "ok" ? Theme.textDim : parent.parent.parent.tint
                                    text: modelData.state === "ok" ? (modelData.output.trim() === "the key was already there" ? qsTr("The key was already there")
                                                                      : modelData.output.trim() === "verified" ? qsTr("Checked: a new login with the new key works, the old key is gone")
                                                                      : modelData.output.trim() === "old key removed" ? qsTr("The new key works, the old key was removed")
                                                                      : modelData.output.trim() === "the old key was not there" ? qsTr("The new key works (the old key was not on this server)")
                                                                      : qsTr("Key added"))
                                          : modelData.state === "queued" ? qsTr("waiting…") : modelData.state === "running" ? qsTr("running…") : modelData.error
                                }
                            }
                        }
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                AppButton { text: qsTr("Back"); glyph: Theme.iconBack; variant: "ghost"
                            enabled: !root.batch.running && !(root.replaceMode && (App.rotatePhase === "adding" || App.rotatePhase === "removing" || App.rotatePhase === "verifying")); onClicked: root.showResults = false }
                Item { Layout.fillWidth: true }
                AppButton { visible: !root.batch.running && !(root.replaceMode && (App.rotatePhase === "adding" || App.rotatePhase === "removing" || App.rotatePhase === "verifying")); text: qsTr("Done"); variant: "primary"; onClicked: root.close() }
            }
        }
    }
}
