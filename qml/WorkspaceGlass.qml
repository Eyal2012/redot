import QtQuick
Item {
    id: root
    GlassTokens { id: tokens }
    objectName: "workspaceMaterialScene"
    property var regions: []
    property bool glass: true
    property int probeTick: 0 // verification drives this; shipping is static
    // DWM owns the real background scene. No internal wallpaper or capture.
    Rectangle { anchors.fill: parent; color:root.glass?tokens.desktopTint:tokens.base }
    Rectangle { visible:root.probeTick!==0; x:0; y:0; width:1; height:1; color:root.probeTick%2?"#501a142a":"#501b152b" }
    Repeater {
        model: root.regions
        delegate: Item {
            required property var modelData
            x:modelData.cx; y:modelData.cy; width:modelData.cw; height:modelData.ch
            clip:true
            GlassPanel {
                x:modelData.x-modelData.cx+1; y:modelData.y-modelData.cy+1
                width:modelData.w-2; height:modelData.h-2
                // Activation only changes the window's background source.
                // The control/panel material never switches to another fill.
                // An opaque root gives a fully solid accessibility fallback.
                radius:modelData.radius; glass:true
            }
        }
    }
}
