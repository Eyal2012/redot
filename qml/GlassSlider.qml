import QtQuick
import QtQuick.Controls.Basic
Slider {
        property bool controlMotion: true
    GlassTokens { id: tokens }
    id: slider
        background: Rectangle { x: slider.leftPadding; y: slider.topPadding+slider.availableHeight/2-height/2; width: slider.availableWidth; height: 4; radius: 2; color: tokens.track; Rectangle { width: slider.visualPosition*parent.width; height: parent.height; radius: 2; color: tokens.accent } }
        handle: Rectangle { x: slider.leftPadding+slider.visualPosition*(slider.availableWidth-width); y: slider.topPadding+slider.availableHeight/2-height/2; width: 15; height: 15; radius: 8; color: slider.pressed?tokens.text:tokens.handle; border.width: slider.activeFocus?2:1; border.color: slider.activeFocus?tokens.text:tokens.outline }
    }
