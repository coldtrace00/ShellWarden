import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import ShellWarden

// Create / edit an own terminal theme: background colour or picture, text
// colour, colour of what you type, cursor, selection and the 16-colour palette.
Popup {
    id: root
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(760, (parent ? parent.width : 760) - 32)
    height: Math.min(780, (parent ? parent.height : 780) - 32)
    modal: true
    focus: true
    padding: 0
    closePolicy: Popup.CloseOnEscape
    Overlay.modal: Rectangle { color: "#a0070910" }
    background: Rectangle { radius: 14; color: Theme.surface; border.color: Theme.border }

    // editable state
    property string themeId: ""
    property string name: ""
    property string bg: "#12141c"
    property string fg: "#e2e4ed"
    property string input: ""
    property bool autoInput: false
    property string cursor: "#4fd1c5"
    property string selection: "#2f3f5c"
    property var ansi: []
    property string bgImage: ""      // local path or file:// URL
    property int bgDim: 60
    property string error: ""

    readonly property var ansiNames: [qsTr("Black"), qsTr("Red"), qsTr("Green"), qsTr("Yellow"), qsTr("Blue"), qsTr("Magenta"), qsTr("Cyan"), qsTr("White")]

    // colours go into the StyledText previews as <font color='…'>: only #rgb … #rrggbbaa
    function hx(v) { return /^#[0-9A-Fa-f]{3,8}$/.test(String(v)) ? String(v) : (/^#[0-9A-Fa-f]{3,8}$/.test(fg) ? fg : "#e2e4ed") }

    function loadFrom(t) {
        bg = t.bg; fg = t.fg; cursor = t.cursor; selection = t.selection
        ansi = t.ansi.slice()
        input = t.input || ""
        autoInput = !!t.autoInput
        bgImage = t.bgImage || ""
        bgDim = t.bgDim !== undefined ? t.bgDim : 60
    }
    function openNew() {
        themeId = ""
        name = ""
        const cur = App.terminalThemes.find(t => t.id === App.settings.terminalTheme) || App.terminalThemes.find(t => !t.custom)
        loadFrom(cur)
        bgImage = ""; bgDim = 60; input = ""; autoInput = false
        error = ""
        useNow.checked = true
        open()
    }
    function openEdit(t) {
        themeId = t.id
        name = t.name
        loadFrom(t)
        error = ""
        useNow.checked = App.settings.terminalTheme === t.id
        open()
    }
    function imageUrl(p) { return p === "" ? "" : p.startsWith(":/") ? "qrc" + p : (p.startsWith("file:") ? p : "file:///" + p.replace(/^\/+/, "")) }

    // colour being edited: {key, index}
    property var editing: null
    ColorPicker {
        id: colorDlg
        onAccepted: {
            const c = selectedColor.toString()
            const e = root.editing
            if (!e) return
            if (e.key === "ansi") { const a = root.ansi.slice(); a[e.index] = c; root.ansi = a }
            else root[e.key] = c
        }
    }
    function pick(key, index) {
        editing = { key: key, index: index === undefined ? -1 : index }
        colorDlg.selectedColor = key === "ansi" ? ansi[index] : (root[key] || "#ffffff")
        colorDlg.open()
    }
    FileDialog {
        id: imgDlg
        nameFilters: [qsTr("Images (*.png *.jpg *.jpeg *.webp *.bmp)")]
        onAccepted: root.bgImage = selectedFile.toString()
    }

    component ColorRow: RowLayout {
        id: cr
        property string label: ""
        property string hint: ""
        property string value: ""
        signal pick()
        Layout.fillWidth: true
        spacing: 10
        ColumnLayout {
            Layout.fillWidth: true
            Layout.preferredWidth: 300
            spacing: 1
            Text { textFormat: Text.PlainText; text: cr.label; color: Theme.text; font.pixelSize: 13 }
            Text { textFormat: Text.PlainText; visible: cr.hint !== ""; text: cr.hint; color: Theme.textFaint; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true }
        }
        Item { Layout.fillWidth: true; implicitHeight: 1 }
        AbstractButton {
            id: crBtn
            implicitWidth: 120; implicitHeight: 32
            hoverEnabled: true
            onClicked: cr.pick()
            background: Rectangle { radius: 8; color: Theme.surface2; border.color: crBtn.hovered ? Theme.accent : Theme.border }
            contentItem: Item {
                Rectangle { id: sw; x: 8; anchors.verticalCenter: parent.verticalCenter; width: 18; height: 18; radius: 5
                            color: cr.value || "transparent"; border.color: Theme.border }
                Text { textFormat: Text.PlainText; anchors.left: sw.right; anchors.leftMargin: 8; anchors.verticalCenter: parent.verticalCenter
                       text: cr.value || qsTr("not set"); color: Theme.textDim; font.pixelSize: 11; font.family: Theme.monoFont }
            }
        }
    }

    contentItem: ColumnLayout {
        spacing: 0

        // header
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 20
            Layout.bottomMargin: 12
            Text { textFormat: Text.PlainText; Layout.fillWidth: true; text: root.themeId ? qsTr("Edit theme") : qsTr("Create your own theme"); color: Theme.text; font.pixelSize: 18; font.weight: Font.DemiBold }
            AppButton { glyph: Theme.iconClose; variant: "ghost"; compact: true; onClicked: root.close() }
        }

        // live preview
        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin: 20; Layout.rightMargin: 20
            implicitHeight: 150
            radius: 10
            clip: true
            color: root.bg
            border.color: Theme.border
            Image {
                anchors.fill: parent
                visible: root.bgImage !== ""
                source: root.imageUrl(root.bgImage)
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
            }
            Rectangle { anchors.fill: parent; visible: root.bgImage !== ""; color: root.bg; opacity: root.bgDim / 100 }
            Column {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 2
                Text { textFormat: Text.StyledText; font.family: Theme.monoFont; font.pixelSize: 13; color: root.fg
                       text: "Last login: Thu Nov  5 09:12:44 from <font color='" + root.hx(root.ansi[14]) + "'>203.0.113.7</font>" }
                Row {
                    Text { textFormat: Text.PlainText; text: "admin@server"; font.family: Theme.monoFont; font.pixelSize: 13; font.bold: true; color: root.ansi[10] || root.fg }
                    Text { textFormat: Text.PlainText; text: ":"; font.family: Theme.monoFont; font.pixelSize: 13; color: root.fg }
                    Text { textFormat: Text.PlainText; text: "~"; font.family: Theme.monoFont; font.pixelSize: 13; font.bold: true; color: root.ansi[12] || root.fg }
                    Text { textFormat: Text.PlainText; text: "$ "; font.family: Theme.monoFont; font.pixelSize: 13; color: root.fg }
                    Text { textFormat: Text.PlainText; text: "ls -la /var/log"; font.family: Theme.monoFont; font.pixelSize: 13; color: root.input || root.fg }
                }
                Text { textFormat: Text.StyledText; font.family: Theme.monoFont; font.pixelSize: 13; color: root.fg
                       text: "<b><font color='" + root.hx(root.ansi[12]) + "'>nginx/</font></b>  syslog  <font color='" + root.hx(root.ansi[2]) + "'>backup.sh</font>  <font color='" + root.hx(root.ansi[1]) + "'>error.log</font>" }
                Text { textFormat: Text.StyledText; font.family: Theme.monoFont; font.pixelSize: 13; color: root.fg
                       text: "<b><font color='" + root.hx(root.ansi[9]) + "'>Failed</font></b> password · <b><font color='" + root.hx(root.ansi[11]) + "'>WARNING</font></b> disk 91% · <font color='" + root.hx(root.ansi[10]) + "'>OK</font>" }
                Row {
                    spacing: 0
                    Text { textFormat: Text.StyledText; font.family: Theme.monoFont; font.pixelSize: 13; color: root.fg
                           text: "<b><font color='" + root.hx(root.ansi[10]) + "'>admin@server</font></b>:~$ " }
                    Rectangle { width: 8; height: 16; color: root.cursor }
                    Rectangle { width: 70; height: 16; color: root.selection
                                Text { textFormat: Text.PlainText; anchors.centerIn: parent; text: qsTr("selected"); color: root.fg; font.family: Theme.monoFont; font.pixelSize: 12 } }
                }
            }
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.topMargin: 12
            contentWidth: availableWidth
            clip: true

            ColumnLayout {
                width: parent.width
                spacing: 14

                RowLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 20; Layout.rightMargin: 20
                    spacing: 10
                    AppField {
                        Layout.fillWidth: true
                        label: qsTr("Name")
                        placeholder: qsTr("My theme")
                        text: root.name
                        onTextChanged: root.name = text
                    }
                    ColumnLayout {
                        spacing: 5
                        Text { textFormat: Text.PlainText; text: qsTr("Start from"); color: Theme.textDim; font.pixelSize: 12; font.weight: Font.Medium }
                        AppCombo {
                            Layout.preferredWidth: 220
                            model: App.terminalThemes.map(t => t.name)
                            currentIndex: -1
                            displayText: currentIndex < 0 ? qsTr("Copy colours from…") : currentText
                            onActivated: (i) => {
                                const keepImg = root.bgImage, keepDim = root.bgDim, keepInput = root.input, keepAuto = root.autoInput
                                root.loadFrom(App.terminalThemes[i])
                                root.bgImage = keepImg; root.bgDim = keepDim; root.input = keepInput; root.autoInput = keepAuto
                            }
                        }
                    }
                }

                Rectangle { Layout.fillWidth: true; Layout.leftMargin: 20; Layout.rightMargin: 20; height: 1; color: Theme.border }

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 20; Layout.rightMargin: 20
                    spacing: 12
                    ColorRow { value: root.bg; onPick: root.pick("bg"); label: qsTr("Background"); hint: qsTr("Colour of the terminal itself.") }
                    ColorRow { value: root.fg; onPick: root.pick("fg"); label: qsTr("Text (what the server prints)"); hint: qsTr("Plain output: file lists, logs, messages.") }
                    ColorRow { value: root.cursor; onPick: root.pick("cursor"); label: qsTr("Cursor") }
                    ColorRow { value: root.selection; onPick: root.pick("selection"); label: qsTr("Selection") }
                }

                Rectangle { Layout.fillWidth: true; Layout.leftMargin: 20; Layout.rightMargin: 20; height: 1; color: Theme.border }

                // what you type
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 20; Layout.rightMargin: 20
                    spacing: 8
                    RowLayout {
                        Layout.fillWidth: true
                        ColorRow { value: root.input; onPick: root.pick("input"); label: qsTr("What you type"); hint: qsTr("The command after the prompt, e.g. ls -la") }
                        AppButton { visible: root.input !== ""; glyph: Theme.iconClose; variant: "ghost"; compact: true; onClicked: root.input = ""
                                    ToolTip.visible: hovered; ToolTip.text: qsTr("Don't colour input") }
                    }
                    Text {
                        textFormat: Text.PlainText
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        color: Theme.textFaint
                        font.pixelSize: 11
                        text: qsTr("A terminal can't tell your typing from the server's output — the server echoes both. So this colour is set through the shell prompt on the server (bash, like the coloured prompt in Debian). It works for the current connection; to keep it for good, add the line below to ~/.bashrc on the server.")
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        visible: root.input !== ""
                        spacing: 10
                        Text { textFormat: Text.PlainText; Layout.fillWidth: true; text: qsTr("Apply automatically after connecting"); color: Theme.text; font.pixelSize: 12 }
                        AppSwitch { checked: root.autoInput; onToggled: root.autoInput = checked }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        visible: root.input !== ""
                        spacing: 8
                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: 30
                            radius: 6
                            color: Theme.bg
                            border.color: Theme.border
                            Text {
                                textFormat: Text.PlainText
                                anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 8
                                verticalAlignment: Text.AlignVCenter
                                text: App.inputColorCommand(root.input).trim()
                                color: Theme.textDim; font.family: Theme.monoFont; font.pixelSize: 10
                                elide: Text.ElideRight
                            }
                        }
                        AppButton { text: qsTr("Copy for ~/.bashrc"); glyph: Theme.iconCopy; compact: true
                                    onClicked: App.copyText(App.inputColorCommand(root.input).trim()) }
                    }
                }

                Rectangle { Layout.fillWidth: true; Layout.leftMargin: 20; Layout.rightMargin: 20; height: 1; color: Theme.border }

                // background picture
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 20; Layout.rightMargin: 20
                    spacing: 8
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1
                            Text { textFormat: Text.PlainText; text: qsTr("Background picture"); color: Theme.text; font.pixelSize: 13 }
                            Text {
                                textFormat: Text.PlainText
                                Layout.fillWidth: true
                                wrapMode: Text.WordWrap
                                color: Theme.textFaint; font.pixelSize: 11
                                text: root.bgImage === "" ? qsTr("Any photo or picture (PNG, JPG, WebP). A copy is kept next to the vault.")
                                                          : decodeURIComponent(root.bgImage.split("/").pop())
                            }
                        }
                        AppButton { text: root.bgImage === "" ? qsTr("Choose picture…") : qsTr("Change…"); glyph: Theme.iconFolder; compact: true; onClicked: imgDlg.open() }
                        AppButton { visible: root.bgImage !== ""; text: qsTr("Remove"); variant: "ghost"; compact: true; onClicked: root.bgImage = "" }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        visible: root.bgImage !== ""
                        spacing: 10
                        Text { textFormat: Text.PlainText; text: qsTr("Darken picture"); color: Theme.textDim; font.pixelSize: 12 }
                        Slider {
                            id: dimSlider
                            Layout.fillWidth: true
                            from: 0; to: 95; stepSize: 1
                            value: root.bgDim
                            onMoved: root.bgDim = Math.round(value)
                            background: Rectangle {
                                x: dimSlider.leftPadding; y: dimSlider.topPadding + dimSlider.availableHeight / 2 - 2
                                width: dimSlider.availableWidth; height: 4; radius: 2; color: Theme.surface2
                                Rectangle { width: dimSlider.visualPosition * parent.width; height: 4; radius: 2; color: Theme.accent }
                            }
                            handle: Rectangle {
                                x: dimSlider.leftPadding + dimSlider.visualPosition * (dimSlider.availableWidth - width)
                                y: dimSlider.topPadding + dimSlider.availableHeight / 2 - 8
                                width: 16; height: 16; radius: 8; color: Theme.accent; border.color: Theme.bg; border.width: 2
                            }
                        }
                        Text { textFormat: Text.PlainText; text: root.bgDim + "%"; color: Theme.text; font.pixelSize: 12; font.family: Theme.monoFont; Layout.preferredWidth: 36 }
                    }
                    Text {
                        textFormat: Text.PlainText
                        Layout.fillWidth: true
                        visible: root.bgImage !== ""
                        wrapMode: Text.WordWrap
                        color: Theme.textFaint; font.pixelSize: 11
                        text: qsTr("The background colour is laid over the picture so the text stays readable. 60–80% is a good start for bright photos.")
                    }
                }

                Rectangle { Layout.fillWidth: true; Layout.leftMargin: 20; Layout.rightMargin: 20; height: 1; color: Theme.border }

                // 16-colour palette
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.leftMargin: 20; Layout.rightMargin: 20
                    spacing: 6
                    Text { textFormat: Text.PlainText; text: qsTr("Palette (the 16 colours programs use)"); color: Theme.text; font.pixelSize: 13 }
                    Text { textFormat: Text.PlainText; Layout.fillWidth: true; wrapMode: Text.WordWrap; color: Theme.textFaint; font.pixelSize: 11
                           text: qsTr("Top row: normal colours, bottom row: bright. ls colours folders blue, errors are red, and so on. Click a colour to change it.") }
                    Grid {
                        columns: 8
                        spacing: 6
                        Repeater {
                            model: 16
                            delegate: AbstractButton {
                                width: 64; height: 44
                                hoverEnabled: true
                                onClicked: root.pick("ansi", index)
                                ToolTip.visible: hovered
                                ToolTip.text: (index >= 8 ? qsTr("Bright %1") : "%1").arg(root.ansiNames[index % 8]) + " · " + (root.ansi[index] || "")
                                background: Rectangle { radius: 8; color: root.ansi[index] || "#000"; border.color: parent.hovered ? Theme.text : Theme.border; border.width: parent.hovered ? 2 : 1 }
                                contentItem: Item {}
                            }
                        }
                    }
                }
                Item { height: 6 }
            }
        }

        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }
        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 16; Layout.rightMargin: 16; Layout.topMargin: 8
            AppCheckBox { id: useNow; text: qsTr("Use as the default theme") }
            Text { textFormat: Text.PlainText; Layout.fillWidth: true; text: root.error; color: Theme.danger; font.pixelSize: 12; wrapMode: Text.WordWrap }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 16
            Layout.topMargin: 4
            spacing: 10
            AppButton {
                visible: root.themeId !== ""
                text: qsTr("Delete"); glyph: Theme.iconDelete; variant: "danger"
                onClicked: { App.deleteCustomTheme(root.themeId); root.close() }
            }
            Item { Layout.fillWidth: true }
            AppButton { text: qsTr("Cancel"); variant: "ghost"; onClicked: root.close() }
            AppButton {
                text: qsTr("Save"); variant: "primary"
                onClicked: {
                    let img = root.bgImage
                    if (img.startsWith("file:")) img = decodeURIComponent(img.replace(/^file:\/\/\//, "").replace(/^file:\/\//, "/"))
                    // keep a leading slash on Linux paths
                    if (Qt.platform.os !== "windows" && img !== "" && !img.startsWith("/")) img = "/" + img
                    const id = App.saveCustomTheme({ id: root.themeId, name: root.name.trim(), bg: root.bg, fg: root.fg,
                                                     cursor: root.cursor, selection: root.selection, ansi: root.ansi,
                                                     input: root.input, autoInput: root.autoInput,
                                                     bgImage: img, bgDim: root.bgDim })
                    if (!id.startsWith("custom-")) { root.error = id; return }
                    if (useNow.checked) App.setSetting("terminalTheme", id)
                    root.close()
                }
            }
        }
    }
}
