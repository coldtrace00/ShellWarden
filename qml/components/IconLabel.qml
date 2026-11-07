import QtQuick
import QtQuick.Shapes
import ShellWarden

// Vector icon drawn with QtQuick.Shapes, so it looks the same on Windows 10,
// Windows 11 and Linux (no dependency on Segoe Fluent / MDL2 fonts).
// `glyph` is an icon name (Theme.iconLock == "lock"); see Icons.qml.
Item {
    id: root
    property string glyph: ""
    property int size: 14
    property color color: Theme.text

    implicitWidth: size
    implicitHeight: size

    readonly property string pathData: Icons.path(glyph)

    Shape {
        id: shape
        // Icons are designed on a 24x24 grid.
        width: 24
        height: 24
        scale: root.size / 24
        transformOrigin: Item.TopLeft
        visible: root.pathData !== ""
        // Qt >= 6.6: analytic anti-aliasing. Older Qt: multisampled layer.
        layer.enabled: !curve
        layer.samples: 4
        layer.smooth: true
        property bool curve: false
        Component.onCompleted: {
            if (Shape.CurveRenderer !== undefined) {
                preferredRendererType = Shape.CurveRenderer
                curve = true
            }
        }

        ShapePath {
            strokeColor: root.color
            // ~1.5 device pixels regardless of icon size
            strokeWidth: Math.max(1.5, Math.min(2.6, 1.55 * 24 / Math.max(8, root.size)))
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: root.pathData }
        }
    }
}
