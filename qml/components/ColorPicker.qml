import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ShellWarden

// Dark colour picker that matches the app (the system ColorDialog is white on
// Windows). Same use as ColorDialog: set selectedColor, open(), onAccepted.
Popup {
    id: root
    property color selectedColor: "#4fd1c5"
    property string title: qsTr("Choose a colour")
    signal accepted()

    parent: Overlay.overlay
    anchors.centerIn: parent
    width: Math.min(340, (parent ? parent.width : 340) - 32)
    modal: true
    focus: true
    padding: 18
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    Overlay.modal: Rectangle { color: "#a0070910" }
    background: Rectangle { radius: 14; color: Theme.surface; border.color: Theme.border }

    // working colour as HSV (kept separately so hue survives grey colours)
    property real h: 0
    property real s: 0
    property real v: 1
    readonly property color current: Qt.hsva(h, s, v, 1)

    function hex(c) {
        const t = (x) => ("0" + Math.round(x * 255).toString(16)).slice(-2)
        return "#" + t(c.r) + t(c.g) + t(c.b)
    }
    function load(c) {
        c = Qt.color(c)
        h = c.hsvHue < 0 ? h : c.hsvHue
        s = c.hsvSaturation
        v = c.hsvValue
        hexField.text = hex(c)
    }
    onAboutToShow: load(selectedColor)

    readonly property var presets: ["#ffffff", "#c9cddc", "#545a72", "#1c1f2b", "#ff5c72", "#fb923c", "#f5c26b", "#facc15",
                                    "#4fd18b", "#22c55e", "#4fd1c5", "#0ea5e9", "#5ea1ff", "#6366f1", "#c58cff", "#ec4899"]

    contentItem: ColumnLayout {
        spacing: 12
        Text { textFormat: Text.PlainText; text: root.title; color: Theme.text; font.pixelSize: 15; font.weight: Font.DemiBold }

        // saturation (x) / brightness (y) square
        Rectangle {
            id: sv
            Layout.fillWidth: true
            Layout.preferredHeight: 170
            radius: 8
            color: Qt.hsva(root.h, 1, 1, 1)
            clip: true
            Rectangle {
                anchors.fill: parent
                radius: 8
                gradient: Gradient { orientation: Gradient.Horizontal
                    GradientStop { position: 0; color: "#ffffffff" }
                    GradientStop { position: 1; color: "#00ffffff" } }
            }
            Rectangle {
                anchors.fill: parent
                radius: 8
                gradient: Gradient {
                    GradientStop { position: 0; color: "#00000000" }
                    GradientStop { position: 1; color: "#ff000000" } }
            }
            Rectangle {
                width: 14; height: 14; radius: 7
                x: root.s * sv.width - 7
                y: (1 - root.v) * sv.height - 7
                color: "transparent"
                border.width: 2
                border.color: root.v > 0.5 ? "#000000" : "#ffffff"
            }
            MouseArea {
                anchors.fill: parent
                function upd(m) {
                    root.s = Math.max(0, Math.min(1, m.x / width))
                    root.v = Math.max(0, Math.min(1, 1 - m.y / height))
                    hexField.text = root.hex(root.current)
                }
                onPressed: (m) => upd(m)
                onPositionChanged: (m) => upd(m)
            }
        }
        // hue bar
        Rectangle {
            id: hueBar
            Layout.fillWidth: true
            Layout.preferredHeight: 14
            radius: 7
            gradient: Gradient { orientation: Gradient.Horizontal
                GradientStop { position: 0.000; color: "#ff0000" }
                GradientStop { position: 0.167; color: "#ffff00" }
                GradientStop { position: 0.333; color: "#00ff00" }
                GradientStop { position: 0.500; color: "#00ffff" }
                GradientStop { position: 0.667; color: "#0000ff" }
                GradientStop { position: 0.833; color: "#ff00ff" }
                GradientStop { position: 1.000; color: "#ff0000" } }
            Rectangle {
                width: 6; height: 20; radius: 3
                anchors.verticalCenter: parent.verticalCenter
                x: root.h * hueBar.width - 3
                color: "white"
                border.color: "#80000000"
            }
            MouseArea {
                anchors.fill: parent
                anchors.margins: -4
                function upd(m) { root.h = Math.max(0, Math.min(0.999, (m.x - 4) / hueBar.width)); hexField.text = root.hex(root.current) }
                onPressed: (m) => upd(m)
                onPositionChanged: (m) => upd(m)
            }
        }
        // quick colours: even 8-column grid
        GridLayout {
            Layout.fillWidth: true
            columns: 8
            rowSpacing: 6
            columnSpacing: 6
            Repeater {
                model: root.presets
                delegate: Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 22
                    radius: 6
                    color: modelData
                    border.width: root.hex(root.current) === modelData ? 2 : 1
                    border.color: root.hex(root.current) === modelData ? Theme.text : Theme.border
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: root.load(modelData) }
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Rectangle { width: 36; height: 36; radius: 8; color: root.current; border.color: Theme.border }
            TextField {
                id: hexField
                Layout.fillWidth: true
                implicitHeight: 36
                color: Theme.text
                font.family: Theme.monoFont
                selectByMouse: true
                maximumLength: 7
                background: Rectangle { radius: 8; color: Theme.surface2; border.color: hexField.activeFocus ? Theme.accent : Theme.border }
                onTextEdited: if (/^#[0-9a-fA-F]{6}$/.test(text)) {
                    const c = Qt.color(text)
                    if (c.hsvHue >= 0) root.h = c.hsvHue
                    root.s = c.hsvSaturation; root.v = c.hsvValue
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            AppButton { text: qsTr("Cancel"); variant: "ghost"; onClicked: root.close() }
            AppButton {
                text: qsTr("Choose"); variant: "primary"
                onClicked: { root.selectedColor = root.hex(root.current); root.close(); root.accepted() }
            }
        }
    }
}
