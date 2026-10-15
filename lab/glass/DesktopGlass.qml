import QtQuick
Item {
    id: glass
    property var source
    property real blurRadius: 4
    property real edgeStrength: 3
    property real tint: .025
    property real fillStrength: .12
    property real highlight: .14
    property real cornerRadius: 12
    property real pixelScale: 1
    property vector2d sampleOrigin: source.sourceOrigin
    property vector2d physicalSize: Qt.vector2d(width*pixelScale,height*pixelScale)
    ShaderEffect {
        id: horizontal
        width: Math.max(1,glass.source.sourceSize.x);height: Math.max(1,glass.source.sourceSize.y)
        visible: false
        property variant scene: glass.source
        property vector2d textureSize: glass.source.sourceSize
        property vector2d axis: Qt.vector2d(1,0)
        property real blurRadius: glass.blurRadius
        fragmentShader: "qrc:/GlassLab/desktop_blur.frag.qsb"
    }
    ShaderEffectSource {id: horizontalTexture;sourceItem: horizontal;hideSource: true;live: true;visible: false;textureSize: Qt.size(horizontal.width,horizontal.height)}
    ShaderEffect {
        id: vertical
        width: horizontal.width;height: horizontal.height;visible: false
        property variant scene: horizontalTexture
        property vector2d textureSize: glass.source.sourceSize
        property vector2d axis: Qt.vector2d(0,1)
        property real blurRadius: glass.blurRadius
        fragmentShader: "qrc:/GlassLab/desktop_blur.frag.qsb"
    }
    ShaderEffectSource {id: blurredTexture;sourceItem: vertical;hideSource: true;live: true;visible: false;textureSize: Qt.size(vertical.width,vertical.height)}
    ShaderEffect {
        anchors.fill: parent
        property variant scene: blurredTexture
        property vector2d surfaceSize: glass.physicalSize
        property vector2d sourceSize: glass.source.sourceSize
        property vector2d origin: glass.sampleOrigin
        property real cornerRadius: glass.cornerRadius*glass.pixelScale
        property real edgeStrength: glass.edgeStrength
        property real tint: glass.tint
        property real fillStrength: glass.fillStrength
        property real highlight: glass.highlight
        fragmentShader: "qrc:/GlassLab/desktop_edge.frag.qsb"
    }
}
