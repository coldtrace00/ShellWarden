import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ShellWarden

// Edit a text file on the server right here (through SFTP): open → change →
// Ctrl+S writes it back. Files up to 5 MB; the file keeps its permissions.
Popup {
    id: root
    property TerminalSession session: null
    property string path: ""
    property string original: ""
    property bool loading: false
    property bool saving: false
    property string error: ""
    property string status: ""
    property int readReq: -1
    property int writeReq: -1
    readonly property bool modified: !loading && editor.text !== original
    readonly property string fileName: path.split("/").pop()

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(1100, (parent ? parent.width : 1100) - 24)
    height: Math.min(800, (parent ? parent.height : 800) - 24)
    modal: true
    focus: true
    padding: 0
    closePolicy: Popup.NoAutoClose
    Overlay.modal: Rectangle { color: "#a0070910" }
    background: Rectangle { radius: 12; color: Theme.surface; border.color: Theme.border }

    function openFile(s, p) {
        session = s; path = p
        original = ""; editor.text = ""; error = ""; status = ""
        loading = true
        open()
        readReq = session.sftpReadFile(p)
    }
    function save() {
        if (!session || loading || saving) return
        saving = true; error = ""; status = ""
        writeReq = session.sftpWriteFile(path, editor.text)
    }
    function tryClose() { if (modified) confirmClose.open(); else close() }
    // keep the cursor on screen when moving with the arrow keys / PgUp / PgDn
    function ensureVisible(r) {
        const f = scroll.contentItem
        if (!f || f.height <= 0) return
        const pad = r.height
        if (r.y - pad < f.contentY)
            f.contentY = Math.max(0, r.y - pad)
        else if (r.y + r.height + pad > f.contentY + f.height)
            f.contentY = Math.min(Math.max(0, f.contentHeight - f.height), r.y + r.height + pad - f.height)
        const x = gutter.width + r.x
        if (x - 40 < f.contentX + gutter.width)
            f.contentX = Math.max(0, x - 40 - gutter.width)
        else if (x + 40 > f.contentX + f.width)
            f.contentX = Math.min(Math.max(0, f.contentWidth - f.width), x + 40 - f.width)
    }

    Connections {
        target: root.session
        function onSftpFileRead(reqId, p, text, binary, err) {
            if (reqId !== root.readReq) return
            root.loading = false
            if (err !== "") { root.error = err; return }
            if (binary) { root.error = qsTr("This looks like a binary file — it cannot be edited as text. Download it instead."); return }
            root.original = text
            editor.text = text
            editor.cursorPosition = 0
            editor.forceActiveFocus()
        }
        function onSftpDone(reqId, err) {
            if (reqId !== root.writeReq) return
            root.saving = false
            if (err !== "") { root.error = qsTr("Not saved: %1").arg(err); return }
            root.original = editor.text
            root.status = qsTr("Saved %1").arg(new Date().toLocaleTimeString(Qt.locale(), "HH:mm:ss"))
        }
    }
    Shortcut { sequences: [StandardKey.Save]; enabled: root.opened; onActivated: root.save() }
    Shortcut { sequence: "Escape"; enabled: root.opened && !confirmClose.opened; onActivated: root.tryClose() }

    contentItem: ColumnLayout {
        spacing: 0
        // header
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 14
            spacing: 10
            IconLabel { glyph: Theme.iconFile; size: 16; color: Theme.accent }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                Text { text: root.fileName + (root.modified ? "  •" : ""); color: Theme.text; font.pixelSize: 15; font.weight: Font.DemiBold; elide: Text.ElideMiddle; Layout.fillWidth: true }
                Text { text: (root.session ? root.session.label + ":" : "") + root.path; color: Theme.textFaint; font.pixelSize: 11; font.family: Theme.monoFont; elide: Text.ElideMiddle; Layout.fillWidth: true }
            }
            Text { visible: root.status !== "" && !root.modified; text: root.status; color: Theme.ok; font.pixelSize: 12 }
            BusyIndicator { visible: root.loading || root.saving; running: visible; implicitWidth: 22; implicitHeight: 22 }
            AppButton { text: qsTr("Save") + "  ·  Ctrl+S"; variant: "primary"; glyph: Theme.iconSave; enabled: root.modified && !root.saving; onClicked: root.save() }
            AppButton { glyph: Theme.iconClose; variant: "ghost"; compact: true; onClicked: root.tryClose() }
        }
        Rectangle {
            visible: root.error !== ""
            Layout.fillWidth: true
            implicitHeight: errText.implicitHeight + 16
            color: Theme.dangerDim
            Text { id: errText; anchors.fill: parent; anchors.margins: 8; anchors.leftMargin: 16; text: root.error; color: Theme.text; font.pixelSize: 12; wrapMode: Text.WordWrap }
        }
        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }
        // text with line numbers
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.terminalBg
            ScrollView {
                id: scroll
                anchors.fill: parent
                clip: true
                Row {
                    // line numbers
                    Column {
                        id: gutter
                        width: Math.max(44, String(editor.lineCount).length * 9 + 20)
                        topPadding: editor.topPadding
                        Repeater {
                            model: editor.lineCount
                            delegate: Text {
                                width: gutter.width - 10
                                horizontalAlignment: Text.AlignRight
                                text: index + 1
                                color: Theme.textFaint
                                font: editor.font
                                height: editor.lineCount > 0 ? editor.contentHeight / editor.lineCount : 18
                                verticalAlignment: Text.AlignTop
                            }
                        }
                    }
                    TextArea {
                        id: editor
                        width: Math.max(scroll.width - gutter.width - 2, implicitWidth)
                        readOnly: root.loading || root.error !== "" && root.original === ""
                        color: Theme.text
                        selectionColor: Theme.accentDim
                        selectedTextColor: Theme.text
                        font.family: Theme.monoFont
                        font.pixelSize: 13
                        wrapMode: TextArea.NoWrap
                        selectByMouse: true
                        persistentSelection: true
                        tabStopDistance: 4 * fm.advanceWidth(" ")
                        background: null
                        leftPadding: 8
                        FontMetrics { id: fm; font: editor.font }
                        onCursorRectangleChanged: if (activeFocus) root.ensureVisible(cursorRectangle)
                        SyntaxHighlighter {
                            id: highlighter
                            document: editor.textDocument
                            fileName: root.path
                            firstLine: root.original.substring(0, Math.min(200, root.original.indexOf("\n") < 0 ? root.original.length : root.original.indexOf("\n")))
                        }
                    }
                }
            }
            Text {
                anchors.centerIn: parent
                visible: root.loading
                text: qsTr("Opening…")
                color: Theme.textFaint
            }
        }
        // status bar
        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 8
            Layout.leftMargin: 14
            spacing: 14
            Text { text: qsTr("%1 lines").arg(editor.lineCount); color: Theme.textFaint; font.pixelSize: 11 }
            Text { text: "UTF-8"; color: Theme.textFaint; font.pixelSize: 11 }
            Text { text: highlighter.language; color: Theme.textFaint; font.pixelSize: 11 }
            Text { text: qsTr("Ln %1, Col %2").arg(editor.lineCount > 0 ? Math.round(editor.cursorRectangle.y / (editor.contentHeight / editor.lineCount)) + 1 : 1)
                                              .arg(editor.cursorPosition - editor.text.lastIndexOf("\n", editor.cursorPosition - 1))
                   color: Theme.textFaint; font.pixelSize: 11 }
            Item { Layout.fillWidth: true }
            Text { text: qsTr("Ctrl+S save · Esc close · the file keeps its permissions"); color: Theme.textFaint; font.pixelSize: 11 }
        }
    }

    Modal {
        id: confirmClose
        title: qsTr("Save changes to %1?").arg(root.fileName)
        subtitle: qsTr("The file on the server has not been changed yet.")
        RowLayout {
            Layout.fillWidth: true
            AppButton { text: qsTr("Don't save"); variant: "danger"; onClicked: { confirmClose.close(); root.close() } }
            Item { Layout.fillWidth: true }
            AppButton { text: qsTr("Cancel"); variant: "ghost"; onClicked: confirmClose.close() }
            AppButton {
                text: qsTr("Save"); variant: "primary"
                onClicked: {
                    confirmClose.close()
                    root.save()
                    closeAfterSave.restart()
                }
            }
        }
    }
    // close once the save went through
    Timer { id: closeAfterSave; interval: 150; repeat: true; onTriggered: if (!root.saving) { stop(); if (root.error === "") root.close() } }
}
