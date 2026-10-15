import QtQuick
Item {
    id: material
    property real cornerRadius: 18
    property real tint: .025
    property real surfaceOpacity: .06
    property real highlight: .14
    property real grain: 0
    property real hover: 0
    // A bounded six-pixel paint outset; it has no input handler or native HWND.
    ShaderEffect {
        x: -6; y: -6; width: material.width+12; height: material.height+12
        property vector2d surfaceSize: Qt.vector2d(material.width,material.height)
        property real cornerRadius: material.cornerRadius
        property real tint: material.tint
        property real surfaceOpacity: material.surfaceOpacity
        property real highlight: material.highlight
        property real grain: material.grain
        property real hover: material.hover
        property real outset: 6
        fragmentShader: "qrc:/GlassLab/surface.frag.qsb"
    }
}
