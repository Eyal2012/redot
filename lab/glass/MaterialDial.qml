import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
ColumnLayout {
    id: dial
    property string title
    property alias value: slider.value
    property alias from: slider.from
    property alias to: slider.to
    property string suffix: "%"
    property real factor: 100
    spacing: 4
    opacity: enabled ? 1 : .45
    Layout.fillWidth: true
    RowLayout {
        Layout.fillWidth: true
        Text { text: dial.title; color: "#e2deed"; font.pixelSize: 12; Layout.fillWidth: true }
        Text { text: (dial.factor===100 ? Math.round(dial.value*dial.factor*10)/10 : Math.round(dial.value*dial.factor))+dial.suffix; color: "#aba6bc"; font.pixelSize: 11 }
    }
    Slider {
        id: slider
        objectName: dial.objectName + "Slider"
        Accessible.name: dial.title
        Layout.fillWidth: true
        from: 0; to: 1
        wheelEnabled: false
        implicitHeight: 24
        background: Rectangle {
            x: slider.leftPadding; y: slider.topPadding + slider.availableHeight/2-height/2
            width: slider.availableWidth; height: 3; radius: 1.5; color: "#454250"
            Rectangle { width: slider.visualPosition*parent.width; height: 3; radius: 1.5; color: "#b7accf" }
        }
        handle: Rectangle {
            x: slider.leftPadding+slider.visualPosition*(slider.availableWidth-width)
            y: slider.topPadding+slider.availableHeight/2-height/2
            implicitWidth: 13; implicitHeight: 13; radius: 6.5
            color: slider.pressed ? "#ffffff" : "#e3dcf3"
            border.width: slider.activeFocus ? 2 : 1; border.color: slider.activeFocus ? "#b699ee" : "#f4f0ff"
        }
    }
}
