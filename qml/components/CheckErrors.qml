import QtQuick
import QtQuick.Layouts
import ShellWarden

// Why an IP check failed: one line per service that was tried, then a short hint.
//   ● ipinfo.io       name not found (DNS)
//   ● api.ipify.org   name not found (DNS)
//   No internet connection, or your network blocks these services.
ColumnLayout {
    id: root
    property var errors: []        // [{service, error}]
    property string hint: ""
    property color accent: Theme.warn
    readonly property int count: errors ? errors.length : 0
    visible: count > 0 || hint !== ""
    spacing: 3

    GridLayout {
        visible: root.count > 0
        columns: 3
        columnSpacing: 8
        rowSpacing: 2
        Repeater {
            model: root.count
            delegate: Rectangle {
                Layout.row: index; Layout.column: 0
                Layout.alignment: Qt.AlignVCenter
                width: 6; height: 6; radius: 3
                color: root.accent
            }
        }
        Repeater {
            model: root.count
            delegate: Text {
                textFormat: Text.PlainText
                Layout.row: index; Layout.column: 1
                text: root.errors[index].service
                color: Theme.text
                font.pixelSize: 11
                font.family: Theme.monoFont
            }
        }
        Repeater {
            model: root.count
            delegate: Text {
                textFormat: Text.PlainText
                Layout.row: index; Layout.column: 2
                Layout.fillWidth: true
                text: root.errors[index].error
                color: Theme.textDim
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }
        }
    }
    Text {
        textFormat: Text.PlainText
        visible: root.hint !== ""
        Layout.fillWidth: true
        text: root.hint
        color: Theme.textFaint
        font.pixelSize: 11
        wrapMode: Text.WordWrap
    }
}
