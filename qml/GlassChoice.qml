import QtQuick
import QtQuick.Controls.Basic
ComboBox {
        property bool controlMotion: true
    GlassTokens { id: tokens }
    id: choice
        implicitHeight: 32
        font.family: Design.fontFamily; font.pixelSize: Design.fontSize
        contentItem: Text { text: choice.displayText; color: tokens.text; font: choice.font; elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter; leftPadding: 14; rightPadding: 26 }
        indicator: Text { x: choice.width-22; anchors.verticalCenter: parent.verticalCenter; text: "v"; color: tokens.secondary; font.pixelSize: 18 }
        background: Rectangle { radius: tokens.controlRadius; color: choice.hovered?tokens.pillHover:tokens.pillIdle; border.width: choice.activeFocus?2:1; border.color: choice.activeFocus?tokens.focus:tokens.outline }
        delegate: ItemDelegate { width: choice.width; text: modelData; highlighted: choice.highlightedIndex===index; contentItem: Text { text: modelData; color: tokens.text; font: choice.font; leftPadding: 10; verticalAlignment: Text.AlignVCenter } background: Rectangle { radius: 8; color: parent.highlighted?tokens.pillChecked:"transparent" } }
        popup: Popup {
            y: choice.height+7; width: choice.width; padding: 7
            implicitHeight: Math.min(contentItem.implicitHeight+14,280)
            contentItem: ListView { clip: true; implicitHeight: contentHeight; model: choice.popup.visible?choice.delegateModel:null; currentIndex: choice.highlightedIndex; ScrollIndicator.vertical: ScrollIndicator {} }
            background: GlassPanel { glass: false; radius: 16 }
            enter: Transition { NumberAnimation { property: "opacity"; from: 0; to: 1; duration: controlMotion?95:0 } }
        }
    }
