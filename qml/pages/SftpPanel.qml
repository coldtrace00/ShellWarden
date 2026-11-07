import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import ShellWarden

// File browser over SFTP: navigate, send files to the server (menu or drag & drop
// from Explorer), save one or more files to this PC, rename, delete, new folder,
// transfer queue. Click selects, Ctrl+click adds, Shift+click selects a range.
Item {
    id: root
    property TerminalSession session: null
    property string path: ""
    property var entries: []
    property string error: ""
    property bool loading: false
    property int selected: -1
    property var history: []
    property int listReq: -1
    property url lastLocalFolder: App.homeFolder()
    property var picked: ({})              // index -> true (several files)
    property int anchorIndex: -1
    readonly property var pickedFiles: {
        const out = []
        for (const k in picked) if (picked[k] && entries[k] && !entries[k].isDir) out.push(entries[k])
        return out
    }
    signal closeRequested()

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
        selected = i
    }
    // save the chosen server files: one folder dialog for all of them
    function saveToPc(remotePaths) {
        if (remotePaths.length === 0) return
        downloadDlg.remotes = remotePaths
        downloadDlg.open()
    }

    function permString(m, isDir) {
        const r = "rwxrwxrwx"
        let out = isDir ? "d" : "-"
        for (let i = 0; i < 9; ++i) out += (m & (1 << (8 - i))) ? r[i] : "-"
        return out
    }
    function octal(m) { return ("000" + (m & 0o7777).toString(8)).slice(-4) }

    // Tab completion in the path field: reads the folder listing (read-only,
    // the same request as opening the folder) and completes the name.
    property int compReq: -1
    property string compDir: ""
    property string compPrefix: ""
    property var compMatches: []
    function completeFrom(entries) {
        const m = entries.filter(e => e.name.startsWith(compPrefix)).map(e => e.name + (e.isDir ? "/" : ""))
        compMatches = m.length > 1 ? m : []
        if (m.length === 0) return
        let common = m[0]
        for (const x of m) { let i = 0; while (i < common.length && i < x.length && common[i] === x[i]) ++i; common = common.substring(0, i) }
        pathField.text = joinPath(compDir, common).replace(/\/\/+/g, "/")
        pathField.cursorPosition = pathField.text.length
    }
    function startCompletion() {
        const t = pathField.text
        const slash = t.lastIndexOf("/")
        compDir = slash <= 0 ? "/" : t.substring(0, slash)
        compPrefix = t.substring(slash + 1)
        if (compDir === root.path) { completeFrom(root.entries); return }
        compReq = session.sftpList(compDir)
    }

    function joinPath(dir, name) { return dir === "/" ? "/" + name : dir + "/" + name }
    function parentOf(p) { if (p === "/" || p === "") return "/"; const i = p.lastIndexOf("/"); return i <= 0 ? "/" : p.substring(0, i) }
    function refresh() { load(root.path) }
    property string requested: ""
    property string pendingSelect: ""
    function load(p) {
        loading = true
        error = ""
        requested = p
        pendingSelect = ""
        listReq = session.sftpList(p)
    }
    function open(p) {
        if (root.path !== "") history.push(root.path)
        load(p)
    }

    Component.onCompleted: if (session) load("")

    Connections {
        target: root.session
        function onSftpListed(reqId, path, entries, error) {
            if (reqId === root.compReq) { root.compReq = -1; if (error === "") root.completeFrom(entries); return }
            if (reqId !== root.listReq) return
            root.loading = false
            if (error !== "") {
                // A file path (e.g. typed or completed): open its folder and select the file.
                const req = root.requested
                if (root.pendingSelect === "" && req !== "" && req !== "/" && !req.endsWith("/")) {
                    root.pendingSelect = req.substring(req.lastIndexOf("/") + 1)
                    root.listReq = session.sftpList(root.parentOf(req))
                    root.loading = true
                    return
                }
                root.pendingSelect = ""
                root.error = error
                return
            }
            root.path = path
            root.entries = entries
            root.selected = -1
            root.picked = ({})
            root.anchorIndex = -1
            if (root.pendingSelect !== "") {
                for (let i = 0; i < entries.length; ++i)
                    if (entries[i].name === root.pendingSelect) { root.selected = i; list.positionViewAtIndex(i, ListView.Contain); break }
                root.pendingSelect = ""
            }
        }
        function onSftpDone(reqId, error) {
            if (error !== "") root.error = error
            root.refresh()
        }
        function onTransfersChanged() {
            // refresh the listing when an upload into this folder finishes
            for (const t of root.session.transfers)
                if (t.upload && t.state === 1 && !uploadsSeen[t.id]) { uploadsSeen[t.id] = true; root.refresh() }
        }
    }
    property var uploadsSeen: ({})

    FileDialog {
        id: uploadDlg
        title: qsTr("Send to the server — choose files")
        fileMode: FileDialog.OpenFiles
        currentFolder: root.lastLocalFolder
        onAccepted: {
            for (const f of selectedFiles) root.session.upload(f, root.path)
        }
    }
    FolderDialog {
        id: downloadDlg
        property var remotes: []
        title: remotes.length > 1 ? qsTr("Save %n files to this PC — choose a folder", "", remotes.length)
                                  : qsTr("Save to this PC — choose a folder")
        currentFolder: root.lastLocalFolder
        onAccepted: {
            root.lastLocalFolder = selectedFolder
            for (const r of remotes) root.session.download(r, selectedFolder)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 10
            spacing: 6
            IconLabel { glyph: Theme.iconFolder; size: 16; color: Theme.accent }
            Text { text: qsTr("Files"); color: Theme.text; font.pixelSize: 14; font.weight: Font.DemiBold; Layout.fillWidth: true }
            AppButton {
                id: transferBtn
                glyph: Theme.iconUpload; compact: true; text: qsTr("Transfer ▾")
                onClicked: transferMenu.popup(transferBtn, 0, transferBtn.height + 4)
                ToolTip.visible: hovered && !transferMenu.visible
                ToolTip.text: qsTr("Send files to the server or save files from the server to this PC")
            }
            AppMenu {
                id: transferMenu
                AppMenuItem {
                    glyph: Theme.iconUpload
                    text: qsTr("To the server: choose files on this PC…")
                    onTriggered: uploadDlg.open()
                }
                AppMenuItem {
                    glyph: Theme.iconSave
                    enabled: root.pickedFiles.length > 0
                    text: root.pickedFiles.length === 1 ? qsTr("To this PC: save \"%1\"…").arg(root.pickedFiles[0].name)
                          : root.pickedFiles.length > 1 ? qsTr("To this PC: save %n selected files…", "", root.pickedFiles.length)
                          : qsTr("To this PC: select files on the server first")
                    onTriggered: root.saveToPc(root.pickedFiles.map(e => root.joinPath(root.path, e.name)))
                }
            }
            AppButton { glyph: Theme.iconAdd; compact: true; variant: "ghost"; onClicked: { nameDlg.mode = "mkdir"; nameDlg.openWith("") }
                        ToolTip.visible: hovered; ToolTip.text: qsTr("New folder") }
            AppButton { glyph: Theme.iconClose; compact: true; variant: "ghost"; onClicked: root.closeRequested() }
        }

        // path bar
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            spacing: 4
            AppButton { glyph: Theme.iconBack; compact: true; variant: "ghost"; enabled: root.history.length > 0
                        onClicked: root.load(root.history.pop()) }
            AppButton { glyph: Theme.iconArrowUp; compact: true; variant: "ghost"; enabled: root.path !== "/"
                        onClicked: root.open(root.parentOf(root.path)) }
            TextField {
                id: pathField
                Layout.fillWidth: true
                implicitHeight: 30
                text: root.path
                color: Theme.text
                font.family: Theme.monoFont
                font.pixelSize: 11
                selectByMouse: true
                onAccepted: { root.compMatches = []; root.open(text) }
                onTextEdited: root.compMatches = []
                Keys.onTabPressed: (e) => { root.startCompletion(); e.accepted = true }
                Keys.onEscapePressed: { root.compMatches = []; text = root.path }
                background: Rectangle { radius: 6; color: Theme.surface2; border.color: pathField.activeFocus ? Theme.accent : Theme.border }
                ToolTip.visible: hovered && root.compMatches.length === 0
                ToolTip.delay: 800
                ToolTip.text: qsTr("Type a path; Tab completes folder and file names, Enter opens.")
            }
            AppButton { glyph: Theme.iconRefresh; compact: true; variant: "ghost"; onClicked: root.refresh() }
        }

        // completion candidates (more than one match)
        Flow {
            visible: root.compMatches.length > 0
            Layout.fillWidth: true
            Layout.leftMargin: 10; Layout.rightMargin: 10; Layout.topMargin: 4
            spacing: 4
            Repeater {
                model: root.compMatches.slice(0, 40)
                delegate: AbstractButton {
                    id: cand
                    implicitHeight: 24
                    implicitWidth: candText.implicitWidth + 14
                    hoverEnabled: true
                    onClicked: {
                        pathField.text = root.joinPath(root.compDir, modelData)
                        root.compMatches = []
                        pathField.forceActiveFocus()
                        if (modelData.endsWith("/")) root.startCompletion()
                    }
                    background: Rectangle { radius: 5; color: cand.hovered ? Theme.surfaceHover : Theme.surface2; border.color: Theme.border }
                    contentItem: Text { id: candText; text: modelData; color: modelData.endsWith("/") ? Theme.accent : Theme.text
                                        font.family: Theme.monoFont; font.pixelSize: 11; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                }
            }
        }

        Text {
            visible: root.error !== ""
            Layout.fillWidth: true
            Layout.margins: 10
            wrapMode: Text.WordWrap
            text: root.error
            color: Theme.danger
            font.pixelSize: 12
        }

        // listing
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: 6

            ListView {
                id: list
                anchors.fill: parent
                clip: true
                model: root.entries
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}
                delegate: Rectangle {
                    id: row
                    width: list.width
                    height: 32
                    color: root.picked[index] ? Theme.surface2 : (hov.hovered ? Theme.surfaceHover : "transparent")
                    HoverHandler { id: hov }
                    TapHandler {
                        onTapped: (ev) => root.pick(index, (ev.modifiers & Qt.ControlModifier) !== 0, (ev.modifiers & Qt.ShiftModifier) !== 0)
                        onDoubleTapped: {
                            if (modelData.isDir) root.open(root.joinPath(root.path, modelData.name))
                            else root.saveToPc([root.joinPath(root.path, modelData.name)])
                        }
                    }
                    TapHandler {
                        acceptedButtons: Qt.RightButton
                        onTapped: { if (!root.picked[index]) root.pick(index, false, false); root.selected = index; fileMenu.popup() }
                    }
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        spacing: 10
                        IconLabel { glyph: modelData.isDir ? Theme.iconFolder : Theme.iconFile; size: 15
                                    color: modelData.isDir ? Theme.accent : Theme.textDim }
                        Text {
                            Layout.fillWidth: true
                            text: modelData.name + (modelData.isLink ? " ↗" : "")
                            color: Theme.text
                            font.pixelSize: 12
                            elide: Text.ElideMiddle
                        }
                        Text {
                            text: modelData.isDir ? "" : App.formatBytes(modelData.size)
                            color: Theme.textDim
                            font.pixelSize: 11
                        }
                        Text {
                            visible: list.width > 400
                            text: root.permString(modelData.perms || 0, modelData.isDir)
                            color: Theme.textFaint
                            font.pixelSize: 10
                            font.family: Theme.monoFont
                        }
                        Text {
                            visible: list.width > 330
                            text: modelData.mtime
                            color: Theme.textFaint
                            font.pixelSize: 10
                            font.family: Theme.monoFont
                        }
                    }
                }
            }

            BusyIndicator { anchors.centerIn: parent; running: root.loading; visible: root.loading }
            Text {
                anchors.centerIn: parent
                visible: !root.loading && root.entries.length === 0 && root.error === ""
                text: qsTr("Empty folder")
                color: Theme.textFaint
            }

            // Drag & drop from Explorer → upload into the current folder
            DropArea {
                id: drop
                anchors.fill: parent
                keys: ["text/uri-list"]
                onDropped: (d) => {
                    for (const u of d.urls) root.session.upload(u, root.path)
                    d.acceptProposedAction()
                }
            }
            Rectangle {
                anchors.fill: parent
                anchors.margins: 6
                visible: drop.containsDrag
                radius: 10
                color: Theme.tint(Theme.accent, "1a")
                border.color: Theme.accent
                border.width: 2
                Column {
                    anchors.centerIn: parent
                    spacing: 8
                    IconLabel { anchors.horizontalCenter: parent.horizontalCenter; glyph: Theme.iconUpload; size: 30; color: Theme.accent }
                    Text { text: qsTr("Drop to send to the server, into %1").arg(root.path); color: Theme.text; font.pixelSize: 13 }
                }
            }
        }

        // details of the selected entry
        Rectangle {
            readonly property var e: root.selected >= 0 && root.selected < root.entries.length ? root.entries[root.selected] : null
            id: details
            visible: e !== null
            Layout.fillWidth: true
            implicitHeight: detCol.implicitHeight + 16
            color: Theme.surface2
            ColumnLayout {
                id: detCol
                anchors.left: parent.left; anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 12; anchors.rightMargin: 12
                spacing: 2
                Text {
                    Layout.fillWidth: true
                    text: details.e ? root.joinPath(root.path, details.e.name) : ""
                    color: Theme.text; font.pixelSize: 11; font.family: Theme.monoFont; elide: Text.ElideMiddle
                }
                Text {
                    Layout.fillWidth: true
                    wrapMode: Text.WordWrap
                    color: Theme.textDim; font.pixelSize: 11
                    text: !details.e ? "" :
                          (details.e.isDir ? qsTr("Folder") : qsTr("%1 (%2 bytes)").arg(App.formatBytes(details.e.size)).arg(details.e.size))
                          + "  ·  " + root.permString(details.e.perms || 0, details.e.isDir) + " (" + root.octal(details.e.perms || 0) + ")"
                          + (details.e.owner ? "  ·  " + qsTr("owner %1").arg(details.e.owner) : "")
                          + "  ·  " + details.e.mtime
                          + (details.e.isLink ? "  ·  " + qsTr("symbolic link") : "")
                }
            }
        }

        // transfers
        Rectangle {
            Layout.fillWidth: true
            visible: root.session && root.session.transfers.length > 0
            implicitHeight: Math.min(trCol.implicitHeight + 16, 220)
            color: Theme.bg
            Rectangle { width: parent.width; height: 1; color: Theme.border }
            Flickable {
                anchors.fill: parent
                anchors.margins: 8
                contentHeight: trCol.implicitHeight
                clip: true
                ColumnLayout {
                    id: trCol
                    width: parent.width
                    spacing: 8
                    RowLayout {
                        Layout.fillWidth: true
                        Text { text: qsTr("Transfers"); color: Theme.textDim; font.pixelSize: 11; font.weight: Font.Medium; Layout.fillWidth: true }
                        AppButton { text: qsTr("Clear finished"); compact: true; variant: "ghost"; onClicked: root.session.clearFinishedTransfers() }
                    }
                    Repeater {
                        model: root.session ? root.session.transfers : []
                        delegate: ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 3
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 6
                                IconLabel { glyph: modelData.upload ? Theme.iconUpload : Theme.iconSave; size: 13
                                            color: modelData.state === 2 ? Theme.danger : (modelData.state === 1 ? Theme.ok : Theme.accent) }
                                Text { Layout.fillWidth: true; text: modelData.name; color: Theme.text; font.pixelSize: 11; elide: Text.ElideMiddle }
                                Text {
                                    text: modelData.state === 0 ? modelData.sizeText + " · " + modelData.speedText
                                          : modelData.state === 1 ? qsTr("done") : modelData.state === 3 ? qsTr("cancelled") : qsTr("failed")
                                    color: modelData.state === 2 ? Theme.danger : Theme.textDim
                                    font.pixelSize: 10
                                }
                                AbstractButton {
                                    visible: modelData.state === 0
                                    width: 18; height: 18
                                    onClicked: root.session.cancelTransfer(modelData.id)
                                    contentItem: Item { IconLabel { anchors.centerIn: parent; glyph: Theme.iconClose; size: 10; color: Theme.textDim } }
                                }
                            }
                            Rectangle {
                                Layout.fillWidth: true
                                height: 4
                                radius: 2
                                color: Theme.surface2
                                Rectangle {
                                    width: parent.width * Math.max(0, Math.min(1, modelData.state === 1 ? 1 : modelData.progress))
                                    height: parent.height
                                    radius: 2
                                    color: modelData.state === 2 ? Theme.danger : (modelData.state === 1 ? Theme.ok : Theme.accent)
                                    Behavior on width { NumberAnimation { duration: 200 } }
                                }
                            }
                            Text { visible: modelData.error !== ""; text: modelData.error; color: Theme.danger; font.pixelSize: 10; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                        }
                    }
                }
            }
        }
    }

    AppMenu {
        id: fileMenu
        readonly property var entry: root.selected >= 0 ? root.entries[root.selected] : null
        AppMenuItem {
            visible: fileMenu.entry && !fileMenu.entry.isDir
            height: visible ? implicitHeight : 0
            glyph: Theme.iconEdit
            text: qsTr("Edit here")
            onTriggered: fileEditor.openFile(root.session, root.joinPath(root.path, fileMenu.entry.name))
        }
        AppMenuItem {
            text: fileMenu.entry && fileMenu.entry.isDir ? qsTr("Open")
                  : root.pickedFiles.length > 1 ? qsTr("Save %n selected files to this PC…", "", root.pickedFiles.length)
                  : qsTr("Save to this PC…")
            onTriggered: {
                const p = root.joinPath(root.path, fileMenu.entry.name)
                if (fileMenu.entry.isDir) root.open(p)
                else root.saveToPc(root.pickedFiles.length > 1 ? root.pickedFiles.map(e => root.joinPath(root.path, e.name)) : [p])
            }
        }
        AppMenuItem { text: qsTr("Copy path"); onTriggered: App.copyText(root.joinPath(root.path, fileMenu.entry.name)) }
        AppMenuItem { text: qsTr("Rename…"); onTriggered: { nameDlg.mode = "rename"; nameDlg.openWith(fileMenu.entry.name) } }
        MenuSeparator {}
        AppMenuItem { text: qsTr("Delete"); onTriggered: delDlg.open() }
    }

    Modal {
        id: nameDlg
        property string mode: "mkdir"
        title: mode === "mkdir" ? qsTr("New folder") : qsTr("Rename")
        function openWith(t) { nameField.text = t; open(); nameField.field.forceActiveFocus(); nameField.field.selectAll() }
        AppField { id: nameField; Layout.fillWidth: true; label: qsTr("Name"); onAccepted: nameOk.clicked() }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            AppButton { text: qsTr("Cancel"); variant: "ghost"; onClicked: nameDlg.close() }
            AppButton {
                id: nameOk
                text: qsTr("OK"); variant: "primary"
                enabled: nameField.text.trim().length > 0 && nameField.text.indexOf("/") < 0
                onClicked: {
                    if (nameDlg.mode === "mkdir") root.session.sftpMkdir(root.joinPath(root.path, nameField.text.trim()))
                    else root.session.sftpRename(root.joinPath(root.path, fileMenu.entry.name), root.joinPath(root.path, nameField.text.trim()))
                    nameDlg.close()
                }
            }
        }
    }

    Modal {
        id: delDlg
        title: fileMenu.entry ? qsTr("Delete \"%1\" on the server?").arg(fileMenu.entry.name) : ""
        subtitle: fileMenu.entry && fileMenu.entry.isDir ? qsTr("Only empty folders can be deleted.") : qsTr("This cannot be undone.")
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            AppButton { text: qsTr("Cancel"); variant: "ghost"; onClicked: delDlg.close() }
            AppButton {
                text: qsTr("Delete"); variant: "danger"; glyph: Theme.iconDelete
                onClicked: { root.session.sftpRemove(root.joinPath(root.path, fileMenu.entry.name), fileMenu.entry.isDir); delDlg.close() }
            }
        }
    }
    FileEditor { id: fileEditor }
}
