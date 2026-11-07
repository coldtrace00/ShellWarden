import QtQuick
import QtQuick.Shapes
import ShellWarden

// ShellWarden mark: a faceted shield with a terminal prompt inside.
Item {
    id: root
    property int size: 32
    implicitWidth: size
    implicitHeight: size

    Shape {
        width: 64; height: 64
        scale: root.size / 64
        transformOrigin: Item.TopLeft
        layer.enabled: !curve
        layer.samples: 4
        property bool curve: false
        Component.onCompleted: {
            if (Shape.CurveRenderer !== undefined) {
                preferredRendererType = Shape.CurveRenderer
                curve = true
            }
        }
        // shield rim
        ShapePath {
            strokeWidth: 0
            strokeColor: "transparent"
            fillGradient: LinearGradient {
                x1: 10; y1: 4; x2: 54; y2: 60
                GradientStop { position: 0; color: Theme.accent }
                GradientStop { position: 1; color: Theme.blue }
            }
            PathSvg { path: "M32 3 L55 9.5 V30 C55 44.5 45.5 55 32 61 C18.5 55 9 44.5 9 30 V9.5 Z" }
        }
        // inner plate
        ShapePath {
            strokeWidth: 0
            strokeColor: "transparent"
            fillColor: "#0d1018"
            PathSvg { path: "M32 7.6 L50.6 12.8 V30 C50.6 41.8 43 50.6 32 55.8 C21 50.6 13.4 41.8 13.4 30 V12.8 Z" }
        }
        // lit left half (facet)
        ShapePath {
            strokeWidth: 0
            strokeColor: "transparent"
            fillColor: "#0bffffff"
            PathSvg { path: "M32 7.6 V55.8 C21 50.6 13.4 41.8 13.4 30 V12.8 Z" }
        }
        // prompt ">"
        ShapePath {
            strokeColor: Theme.accent
            strokeWidth: 3.4
            fillColor: "transparent"
            capStyle: ShapePath.FlatCap
            joinStyle: ShapePath.MiterJoin
            PathSvg { path: "M21 23.5 L29.5 31 L21 38.5" }
        }
        // cursor block
        ShapePath {
            strokeWidth: 0
            strokeColor: "transparent"
            fillColor: "#e6e8f0"
            PathSvg { path: "M33 35.6 H43.5 V39 H33 Z" }
        }
    }
}
