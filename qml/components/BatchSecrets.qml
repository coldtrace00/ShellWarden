import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ShellWarden

// What a batch run may need from you once, before it starts:
//  - the master password, if some chosen servers ask for it on connect
//  - the passphrase of each key that is not saved in the vault
// Nothing typed here is saved; it is used for this run only.
ColumnLayout {
    id: root
    property var hostIds: []
    property bool needMaster: false
    readonly property var keyNeeds: { App.keys.count; return App.batchKeyNeeds(hostIds) }
    readonly property bool needed: needMaster || keyNeeds.length > 0
    property string masterPassword: masterField.text
    signal submit()
    visible: needed
    spacing: 6

    // returns "" when everything needed is filled in and correct
    function prepare() {
        if (needMaster && masterField.text === "")
            return qsTr("Some chosen servers ask for the master password: enter it once.")
        const map = {}
        for (let i = 0; i < keyNeeds.length; ++i) {
            const f = passRepeater.itemAt(i)
            if (f && f.text !== "") map[keyNeeds[i].keyId] = f.text
        }
        return App.setBatchPassphrases(map)
    }
    function clear() {
        masterField.text = ""
        for (let i = 0; i < passRepeater.count; ++i) { const f = passRepeater.itemAt(i); if (f) f.text = "" }
    }

    Text {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        text: qsTr("Needed once for this run (not saved):")
        color: Theme.textDim; font.pixelSize: 12; font.weight: Font.Medium
    }
    AppField {
        id: masterField
        visible: root.needMaster
        Layout.fillWidth: true
        label: qsTr("Master password (some servers ask for it)")
        secret: true
        onAccepted: root.submit()
    }
    Repeater {
        id: passRepeater
        model: root.keyNeeds
        delegate: AppField {
            Layout.fillWidth: true
            label: qsTr("Passphrase of the key \"%1\"").arg(modelData.name)
            placeholder: qsTr("leave empty to skip servers that use this key")
            secret: true
            onAccepted: root.submit()
        }
    }
}
