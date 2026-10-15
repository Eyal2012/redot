import QtQuick
// The only production material. No texture input or background acquisition.
Item {
    id: panel
    property bool glass: true
    property real radius: Design.surfaceRadius
    default property alias content: foreground.data
    ShaderEffect {
        anchors.fill: parent
        property vector2d surfaceSize: Qt.vector2d(width,height)
        property real cornerRadius: panel.radius
        property real solid: panel.glass?0:1
        property color tintColor: Design.base
        property color solidColor: Design.surfaceSolid
        property color edgeColor: Design.outline
        property real fillStrength: Design.fill
        property real edgeStrength: Design.edge
        fragmentShader: "qrc:/PandaClip/desktop.frag.qsb"
    }
    Item { id: foreground; anchors.fill: parent }
}
