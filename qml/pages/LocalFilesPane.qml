import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ShellWarden

// Files of this computer, the left half of the two-pane SFTP view.
// Click selects, Ctrl+click adds to the selection, double-click opens a folder.
// Server files dropped here (or sent with "← To this PC") are saved into this folder.
Item {
    id: root
    property string path: ""
    property string display: ""
    property string parentPath: ""
    property var entries: []
    property string error: ""
    property var picked: ({})          // index -> true
    property int anchorIndex: -1
    readonly property var pickedFiles: {
        const out = []
        for (const k in picked) if (picked[k] && entries[k] && !entries[k].isDir) out.push(entries[k])
        return out
    }
    property var dnd: null                    // the SFTP panel: draws the dragged item, see beginDrag()
    signal remoteDropped(var remotePaths)   // dragged from the server list

    function load(p) {
        const r = App.listLocal(p)
        if (r.error) { error = r.error; return }
        error = ""
        path = r.path; display = r.display; parentPath = r.parent
        entries = r.entries
        picked = ({}); anchorIndex = -1
        pathField.text = display
        try { App.setSetting("sftpLocalFolder", path) } catch (e) {}
    }
    function refresh() { load(path) }
    function pick(i, ctrl, shift) {
        const p = ctrl ? Object.assign({}, picked) : ({})
        if (shift && anchorIndex >= 0) {
            const a = Math.min(anchorIndex, i), b = Math.max(anchorIndex, i)
            for (let k = a; k <= b; ++k) p[k] = true
        } else {
            p[i] = ctrl ? !picked[i] : true
            anchorIndex = i
        }
        picked = p
    }

    Component.onCompleted: load(App.settings.sftpLocalFolder || "")

    ColumnLayout {
        anchors.fill: parent
        spacing: 4

        RowLayout {
            Layout.fillWidth: true
            spacing: 6
            IconLabel { glyph: Theme.iconMonitor; size: 15; color: Theme.blue }
            Text { text: qsTr("This computer"); color: Theme.text; font.pixelSize: 13; font.weight: Font.DemiBold; Layout.fillWidth: true }
            AppCombo {
                id: places
                implicitWidth: 150
                model: App.localPlaces()
                textRole: "name"
                displayText: qsTr("Places")
                onActivated: (i) => root.load(model[i].path)
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 4
            AppButton { glyph: Theme.iconArrowUp; compact: true; variant: "ghost"; enabled: root.parentPath !== ""
                        onClicked: root.load(root.parentPath)
                        ToolTip.visible: hovered; ToolTip.text: qsTr("Up one folder") }
            TextField {
                id: pathField
                Layout.fillWidth: true
                implicitHeight: 30
                color: Theme.text
                font.family: Theme.monoFont
                font.pixelSize: 11
                selectByMouse: true
                onAccepted: root.load(text)
                Keys.onEscapePressed: text = root.display
                background: Rectangle { radius: 6; color: Theme.surface2; border.color: pathField.activeFocus ? Theme.accent : Theme.border }
            }
            AppButton { glyph: Theme.iconRefresh; compact: true; variant: "ghost"; onClicked: root.refresh() }
        }
        Text {
            visible: root.error !== ""
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            text: root.error
            color: Theme.danger
            font.pixelSize: 12
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Rectangle { anchors.fill: parent; radius: 8; color: Theme.bg; border.color: Theme.border }
            ListView {
                id: list
                anchors.fill: parent
                anchors.margins: 1
                clip: true
                model: root.entries
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}
                delegate: Rectangle {
                    id: row
                    width: list.width
                    height: 30
                    color: root.picked[index] ? Theme.surface2 : (hov.hovered ? Theme.surfaceHover : "transparent")
                    HoverHandler { id: hov }
                    TapHandler {
                        onTapped: (ev) => root.pick(index, ev.modifiers & Qt.ControlModifier, ev.modifiers & Qt.ShiftModifier)
                        onDoubleTapped: if (modelData.isDir) root.load(modelData.path)
                    }
                    // drag files to the server list on the right
                    DragHandler {
                        target: null
                        enabled: !modelData.isDir && root.dnd !== null
                        onActiveChanged: {
                            if (active) {
                                const files = root.picked[index] ? root.pickedFiles : [modelData]
                                root.dnd.beginDrag("sw-local", files.map(e => e.path),
                                                   files.length > 1 ? qsTr("%n files", "", files.length) : modelData.name, centroid.scenePosition)
                            } else {
                                root.dnd.endDrag()
                            }
                        }
                        onCentroidChanged: if (active) root.dnd.moveDrag(centroid.scenePosition)
                    }
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        spacing: 8
                        IconLabel { glyph: modelData.isDir ? Theme.iconFolder : Theme.iconFile; size: 14
                                    color: modelData.isDir ? Theme.blue : Theme.textDim }
                        Text { Layout.fillWidth: true; text: modelData.name; color: Theme.text; font.pixelSize: 12; elide: Text.ElideMiddle }
                        Text { text: modelData.isDir ? "" : App.formatBytes(modelData.size); color: Theme.textDim; font.pixelSize: 11 }
                        Text { visible: list.width > 360; text: modelData.mtime; color: Theme.textFaint; font.pixelSize: 10; font.family: Theme.monoFont }
                    }
                }
            }
            Text {
                anchors.centerIn: parent
                visible: root.entries.length === 0 && root.error === ""
                text: qsTr("Empty folder")
                color: Theme.textFaint
            }
            // server files dragged from the right
            DropArea {
                id: drop
                anchors.fill: parent
                keys: ["sw-remote"]
                onDropped: (d) => {
                    if (d.source && d.source.payload) root.remoteDropped(d.source.payload)
                    d.accept(Qt.CopyAction)
                }
            }
            Rectangle {
                anchors.fill: parent
                anchors.margins: 4
                visible: drop.containsDrag
                radius: 10
                color: Theme.tint(Theme.blue, "1a")
                border.color: Theme.blue
                border.width: 2
                Column {
                    anchors.centerIn: parent
                    spacing: 8
                    IconLabel { anchors.horizontalCenter: parent.horizontalCenter; glyph: Theme.iconSave; size: 28; color: Theme.blue }
                    Text { text: qsTr("Drop to save to this PC in %1").arg(root.display); color: Theme.text; font.pixelSize: 12
                           width: Math.min(implicitWidth, drop.width - 40); wrapMode: Text.WrapAnywhere; horizontalAlignment: Text.AlignHCenter }
                }
            }
        }
    }
}
