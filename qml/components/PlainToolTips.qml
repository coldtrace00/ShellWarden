import QtQuick
import QtQuick.Controls

// Every "ToolTip.text: …" in the program is shown by one shared ToolTip, and
// the style draws its text as AutoText: text that looks like HTML would be
// rendered, and "<img src=…>" in it loaded. Tooltips show server text (proxy
// errors, $SSH_CONNECTION…), so the shared one is switched to plain text once,
// before anything is shown. Put one instance in the main window.
Item {
    visible: false
    function apply() {
        const tip = ToolTip.toolTip
        if (tip && tip.contentItem && tip.contentItem.textFormat !== undefined)
            tip.contentItem.textFormat = Text.PlainText
        return tip && tip.contentItem ? tip.contentItem.textFormat === Text.PlainText : false
    }
    Component.onCompleted: apply()
}
