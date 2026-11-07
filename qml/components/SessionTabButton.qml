import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import ShellWarden

// Session tab: host avatar (with status dot), label + endpoint, close button.
// Drag horizontally to reorder; middle click closes.
AbstractButton {
    id: root
    property var session: null
    property bool active: false
    property bool split: false        // shown in the second pane
    signal closeRequested()
    signal dragStep(int direction)
    signal contextRequested()

    implicitHeight: 40
    implicitWidth: Math.min(240, Math.max(150, row.implicitWidth + 22))
    hoverEnabled: true

    readonly property color hostColor: session && session.color ? session.color : Theme.avatarColor(session ? session.label : "")
    readonly property color dotColor: {
        if (!session) return Theme.textFaint
        switch (session.state) {
        case 4: return Theme.ok
        case 5: return session.closedWithError ? Theme.danger : Theme.textFaint
        default: return Theme.warn
        }
    }

    TapHandler {
        acceptedButtons: Qt.MiddleButton
        onTapped: root.closeRequested()
    }
    TapHandler {
        acceptedButtons: Qt.RightButton
        onTapped: root.contextRequested()
    }
    DragHandler {
        id: drag
        target: null
        xAxis.enabled: true
        yAxis.enabled: false
        property real last: 0
        onActiveChanged: last = 0
        onTranslationChanged: {
            const dx = translation.x - last
            if (Math.abs(dx) > root.width * 0.6) {
                root.dragStep(dx > 0 ? 1 : -1)
                last = translation.x
            }
        }
    }

    background: Rectangle {
        radius: 9
        color: root.active ? Theme.surface2 : (root.hovered ? Theme.surface : "transparent")
        border.width: root.active || root.split ? 1 : 0
        border.color: root.split ? Theme.blue : Theme.border
        Behavior on color { ColorAnimation { duration: 100 } }
        // host colour accent on the active tab
        Rectangle {
            visible: root.active
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.leftMargin: 10
            anchors.rightMargin: 10
            height: 2
            radius: 1
            color: root.hostColor
        }
    }

    contentItem: Item {
        RowLayout {
            id: row
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 6
            spacing: 8

            // avatar + status
            Rectangle {
                width: 24; height: 24; radius: 7
                color: Theme.tint(root.hostColor, "33")
                Text {
                    anchors.centerIn: parent
                    text: root.session && root.session.kind === "serial" ? "⏚" : Theme.initials(root.session ? root.session.label : "")
                    color: root.hostColor
                    font.pixelSize: 10
                    font.weight: Font.Bold
                }
                Rectangle {
                    width: 9; height: 9; radius: 5
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: -2
                    color: root.dotColor
                    border.width: 2
                    border.color: Theme.bg
                    SequentialAnimation on opacity {
                        running: root.session && (root.session.state < 4)
                        loops: Animation.Infinite
                        NumberAnimation { to: 0.3; duration: 500 }
                        NumberAnimation { to: 1; duration: 500 }
                        onRunningChanged: if (!running) parent.opacity = 1
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 0
                RowLayout {
                    spacing: 5
                    Layout.fillWidth: true
                    Text {
                        Layout.fillWidth: true
                        text: root.session ? root.session.label : ""
                        color: root.active ? Theme.text : Theme.textDim
                        font.pixelSize: 12
                        font.weight: root.active ? Font.DemiBold : Font.Normal
                        elide: Text.ElideRight
                    }
                    IconLabel {
                        visible: root.session && root.session.viaProxy
                        glyph: Theme.iconShield; size: 11; color: Theme.accent
                    }
                    IconLabel {
                        visible: root.session && root.session.viaJump
                        glyph: Theme.iconHop; size: 11; color: Theme.blue
                    }
                    IconLabel {
                        visible: root.session && root.session.forwards.length > 0
                        glyph: Theme.iconTunnel; size: 11
                        color: root.session && root.session.activity ? Theme.ok : Theme.textDim
                    }
                }
                Text {
                    Layout.fillWidth: true
                    text: root.session ? root.session.endpoint : ""
                    color: Theme.textFaint
                    font.pixelSize: 10
                    font.family: Theme.monoFont
                    elide: Text.ElideMiddle
                }
            }

            AbstractButton {
                id: closeBtn
                width: 20; height: 20
                hoverEnabled: true
                focusPolicy: Qt.NoFocus
                opacity: root.hovered || root.active ? 1 : 0
                onClicked: root.closeRequested()
                background: Rectangle { radius: 5; color: closeBtn.hovered ? Theme.surfaceHover : "transparent" }
                contentItem: Item { IconLabel { anchors.centerIn: parent; glyph: Theme.iconClose; size: 11; color: closeBtn.hovered ? Theme.text : Theme.textDim } }
            }
        }
    }

    ToolTip.visible: hovered && session && !drag.active
    ToolTip.delay: 700
    ToolTip.text: session ? session.label + "\n" + session.endpoint + (session.routeText ? "\n" + session.routeText : "") : ""
}
