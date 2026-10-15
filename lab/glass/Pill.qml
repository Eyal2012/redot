import QtQuick
import QtQuick.Controls
Button {
    id: control
    property real tint: .025
    property real surfaceOpacity: .06
    property real highlight: .14
    property real grain: 0
    property real cornerRadius: 20
    property bool accent: false
    property int textSize: 13
    implicitHeight: 42
    implicitWidth: Math.max(44,label.implicitWidth+32)
    hoverEnabled: true
    Accessible.name: text
    padding: 12
    background: Surface {
        tint: control.tint
        surfaceOpacity: control.surfaceOpacity + (control.accent ? .02 : 0)
        highlight: control.highlight + (control.activeFocus ? .22 : 0)
        grain: control.grain
        cornerRadius: control.cornerRadius
        hover: control.down ? 1.7 : control.hovered ? 1 : 0
    }
    contentItem: Text {
        id: label
        text: control.text
        color: control.enabled ? "#f5f3fb" : "#827e8e"
        font.family: "Segoe UI Variable"
        font.pixelSize: control.textSize
        font.weight: control.accent ? Font.DemiBold : Font.Medium
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
}
