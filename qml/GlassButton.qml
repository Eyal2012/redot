import QtQuick
import QtQuick.Controls.Basic
Button {
        property bool controlMotion: true
    GlassTokens { id: tokens }
    id: pill
        implicitHeight: 34
        implicitWidth: Math.max(62, contentItem.implicitWidth+28)
        font.family: Design.fontFamily; font.pixelSize: Design.fontSize
        contentItem: Text { text: pill.text; color: pill.enabled ? tokens.text : tokens.disabled; font: pill.font; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
        background: Rectangle {
            radius: tokens.controlRadius; border.width: pill.activeFocus?2:1; border.color: pill.activeFocus?tokens.focus:pill.checked ? tokens.focus : tokens.pillBorder
            color: pill.checked ? tokens.pillChecked : pill.down?tokens.pillPressed:pill.hovered ? tokens.pillHover : tokens.pillIdle
            Behavior on color { enabled: controlMotion; ColorAnimation { duration: tokens.interactionMs } }
        }
    }
