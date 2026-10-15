import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import GlassLab.Experimental 1.0

Item {
    id: root
    objectName: "glassLabRoot"
    width: 1040; height: 720
    property real tint: Lab.preset.tint
    property real surfaceOpacity: Lab.preset.surfaceOpacity
    property real highlight: Lab.preset.highlight
    property real radius: Lab.preset.radius
    property real refraction: Lab.preset.refraction
    property real localBlur: Lab.preset.localBlur
    property real grain: Lab.preset.grain
    property real backdropShade: Lab.preset.backdropShade
    property string materialPreset: Lab.preset.materialPreset
    property bool demoRunning: false
    property bool gridVisible: true
    property int navigation: 0
    property bool solidPreview: Lab.preset.solidPreview
    property int backgroundMode: Lab.preset.backgroundMode===undefined ? 0 : Lab.preset.backgroundMode
    property bool reduceTransparency: Lab.preset.reduceTransparency===true
    property real desktopBlur: Lab.preset.desktopBlur===undefined ? 4 : Lab.preset.desktopBlur
    property real desktopRefraction: Lab.preset.desktopRefraction===undefined ? 3 : Lab.preset.desktopRefraction
    property real desktopFill: Lab.preset.desktopFill===undefined ? .18 : Lab.preset.desktopFill
    property bool customRequested: backgroundMode===0 && !solidPreview && !reduceTransparency && !Lab.highContrast
    property bool customLive: customRequested && Exclusion.safe && desktopSource.live
    property real samplePhase: 0
    // Respect Windows motion policy on launch. Animate sample is a deliberate,
    // local demonstration action; it never changes the Windows preference.
    property bool sampleMoving: false
    Component.onCompleted: sampleMoving=Lab.motionAllowed
    function applyMaterialPreset(name) {
        materialPreset=name
        tint=name==="Soft Frost" ? .055 : .025
        surfaceOpacity=name==="Soft Frost" ? .13 : name==="Solid" ? .20 : .06
        highlight=name==="Soft Frost" ? .18 : .14
        radius=18; refraction=3
        localBlur=name==="Soft Frost" ? 1.4 : .15
        grain=name==="Soft Frost" ? .035 : 0
        backdropShade=name==="Soft Frost" ? .065 : .025
        solidPreview=name==="Solid"
        desktopBlur=name==="Soft Frost" ? 10 : 1
        desktopFill=name==="Soft Frost" ? .24 : .18
        desktopRefraction=3
    }
    function reset() {
        applyMaterialPreset("Clear")
    }
    function presetValues() {return {tint:tint,surfaceOpacity:surfaceOpacity,highlight:highlight,radius:radius,refraction:refraction,localBlur:localBlur,grain:grain,backdropShade:backdropShade,materialPreset:materialPreset,solidPreview:solidPreview,backgroundMode:backgroundMode,reduceTransparency:reduceTransparency,desktopBlur:desktopBlur,desktopRefraction:desktopRefraction,desktopFill:desktopFill}}
    function refreshNativeMode(){Lab.solid(backgroundMode!==1||solidPreview||reduceTransparency)}
    onSolidPreviewChanged: refreshNativeMode()
    onBackgroundModeChanged: refreshNativeMode()
    onReduceTransparencyChanged: refreshNativeMode()

    Rectangle {
        anchors.fill: parent; radius: 12
        color: Lab.acrylic ? Qt.rgba(.055,.065,.08,root.backdropShade) : "#15191f"
    }
    DesktopTexture { id: desktopSource;objectName: "desktopSource";width: 1;height: 1;capturing: root.customRequested && Exclusion.safe }
    Loader { anchors.fill: parent;active: root.customRequested;visible: root.customLive;sourceComponent: Component {DesktopGlass {source: desktopSource;pixelScale: Lab.pixelRatio;blurRadius: root.desktopBlur;edgeStrength: root.desktopRefraction;tint: root.tint;fillStrength: root.desktopFill;highlight: root.highlight}} }
    MouseArea {
        x: 10; y: 8; width: parent.width-160; height: 40
        onPressed: Lab.move()
        onDoubleClicked: Lab.maximize()
    }
    Row {
        x: 24; y: 20; spacing: 10
        Text { text: "◉"; color: "#e0daec"; font.pixelSize: 16 }
        Text { text: "ReDot"; color: "#efedf6"; font.pixelSize: 14; font.weight: Font.DemiBold }
        Text { text: "/  Glass Lab"; color: "#aaa6b7"; font.pixelSize: 14 }
    }
    Row {
        anchors.right: parent.right; anchors.rightMargin: 16; y: 8; spacing: 4
        Pill { objectName: "minimize"; text: "−"; implicitWidth: 38; implicitHeight: 32; cornerRadius: 10; surfaceOpacity: 0; onClicked: Lab.minimize(); Accessible.name: "Minimize" }
        Pill { objectName: "maximize"; text: "□"; implicitWidth: 38; implicitHeight: 32; cornerRadius: 10; surfaceOpacity: 0; onClicked: Lab.maximize(); Accessible.name: "Maximize or restore" }
        Pill { objectName: "close"; text: "×"; implicitWidth: 38; implicitHeight: 32; cornerRadius: 10; surfaceOpacity: 0; onClicked: Lab.close(); Accessible.name: "Close Glass Lab" }
    }

    RowLayout {
        anchors { left: parent.left; right: parent.right; top: parent.top; bottom: footer.top; leftMargin: 28; rightMargin: 28; topMargin: 76; bottomMargin: 20 }
        spacing: 28
        ColumnLayout {
            Layout.fillWidth: true; Layout.fillHeight: true
            spacing: root.height<700 ? 12 : 16
            Item {
                Layout.alignment: Qt.AlignHCenter
                Layout.preferredWidth: 308; Layout.preferredHeight: 54
                Surface { anchors.fill: parent; tint: root.tint; surfaceOpacity: root.surfaceOpacity; highlight: root.highlight; grain: root.grain; cornerRadius: root.radius+7 }
                Row {
                    anchors.centerIn: parent; spacing: 4
                    Repeater {
                        model: ["◉  Capture","◌  Audio","↻  Replay"]
                        Pill {
                            objectName: "nav"+index
                            text: modelData; tint: root.tint; highlight: root.highlight; cornerRadius: root.radius
                            grain: root.grain
                            surfaceOpacity: root.navigation===index ? root.surfaceOpacity+.025 : 0
                            onClicked: root.navigation=index
                        }
                    }
                }
            }
            ColumnLayout {
                Layout.fillWidth: true; Layout.topMargin: 12; spacing: 10
                Text { text: "A little more light."; color: "#f5f2fa"; font.family: "Segoe UI Variable"; font.pixelSize: 32; font.weight: Font.DemiBold; font.letterSpacing: -.7 }
                Text { text: "Quiet edges. Floating controls. Your workspace behind them."; color: "#b9b4c6"; font.pixelSize: 13; Layout.fillWidth: true; wrapMode: Text.WordWrap }
            }
            RowLayout {
                Layout.topMargin: 8; Layout.bottomMargin: 4; spacing: 10
                Pill {
                    id: record; objectName: "record"
                    text: root.demoRunning ? "●   Stop demo" : "●   Start Recording"
                    implicitWidth: 218; implicitHeight: 56; accent: true
                    tint: root.tint; highlight: root.highlight; surfaceOpacity: root.surfaceOpacity; cornerRadius: root.radius
                    grain: root.grain
                    onClicked: root.demoRunning=!root.demoRunning
                    contentItem: Row {
                        spacing: 13
                        leftPadding: 16; topPadding: 7
                        Rectangle { width: 15; height: 15; radius: root.demoRunning ? 4 : 7.5; color: "#fa536f"; anchors.verticalCenter: parent.verticalCenter }
                        Text { text: root.demoRunning ? "Stop demo" : "Start Recording"; color: "#f5f2fa"; font.pixelSize: 14; font.weight: Font.DemiBold; anchors.verticalCenter: parent.verticalCenter }
                    }
                }
                Pill {
                    id: gear; objectName: "gear"; text: "⚙"; Accessible.name: "Demo settings"
                    implicitHeight: 56; implicitWidth: 56
                    tint: root.tint; highlight: root.highlight; surfaceOpacity: root.surfaceOpacity; cornerRadius: root.radius
                    grain: root.grain
                    onClicked: demoPopup.visible ? demoPopup.close() : demoPopup.open()
                }
            }
            Text { text: root.demoRunning ? "Demo active · no video or audio is being recorded" : "Visual prototype · recording pipeline is not connected"; color: "#aaa5b7"; font.pixelSize: 11 }
            Item { Layout.fillHeight: true; Layout.minimumHeight: 5 }
            RowLayout {
                Layout.fillWidth: true
                Text { text: "OPTICAL SAMPLE"; color: "#d6d0e3"; font.pixelSize: 10; font.letterSpacing: 1.4; Layout.fillWidth: true }
                Pill { objectName: "motion"; text: root.sampleMoving ? "Pause sample" : "Animate sample"; implicitHeight: 26; surfaceOpacity: 0; enabled: !Lab.highContrast; onClicked: root.sampleMoving=!root.sampleMoving }
            }
            Item {
                id: bench; objectName: "bench"
                Layout.fillWidth: true; Layout.preferredHeight: root.height<700 ? 140 : 170
                Item {
                    id: benchScene
                    anchors.fill: parent
                    clip: true
                    Rectangle { anchors.fill: parent; radius: 20; color: "#17232c"; border.color: "#354751" }
                    Repeater {
                        model: root.gridVisible ? 18 : 0
                        Rectangle { x: index*bench.width/18; y: 1; width: 1; height: bench.height-2; color: "#496472"; opacity: .45 }
                    }
                    Repeater {
                        model: 4
                        Rectangle {
                            x: 1; y: 36+index*31+Math.sin(root.samplePhase+index*.7)*10; width: bench.width-2; height: 4
                            color: ["#78bdc4","#b3b2dc","#d6a589","#85bba3"][index]
                            opacity: .78
                        }
                    }
                    Rectangle { x: (bench.width-30)*(.5+.45*Math.sin(root.samplePhase)); y: 24; width: 9; height: 112; radius: 4; color: "#e1d7b7"; opacity: .9 }
                    Text { x: 20; y: 16; text: "01  /  LIGHT STUDY"; font.pixelSize: 10; font.letterSpacing: 2; color: "#b5ced5" }
                    Text { x: 20; anchors.bottom: parent.bottom; anchors.bottomMargin: 12; text: "Lines remain in the application. They are not your desktop."; font.pixelSize: 10; color: "#b5ced5" }
                }
                ShaderEffectSource { id: benchTexture; sourceItem: benchScene; hideSource: false; live: true; textureSize: Qt.size(Math.min(720,bench.width),170); visible: false }
                ShaderEffect {
                    id: lens; objectName: "lens"
                    x: (bench.width-width)/2; y: 49; width: Math.min(270,bench.width-40); height: 68
                    property variant scene: benchTexture
                    property vector2d surfaceSize: Qt.vector2d(width,height)
                    property vector2d sceneSize: Qt.vector2d(bench.width,bench.height)
                    property vector2d origin: Qt.vector2d(x,y)
                    property real cornerRadius: root.radius
                    property real refraction: Lab.highContrast ? 0 : root.refraction
                    property real localBlur: Lab.highContrast ? 0 : root.localBlur
                    property real tint: root.tint
                    property real surfaceOpacity: root.surfaceOpacity
                    property real highlight: root.highlight
                    property real grain: Lab.highContrast ? 0 : root.grain
                    fragmentShader: "qrc:/GlassLab/lens.frag.qsb"
                    Text { anchors.centerIn: parent; text: "A small lens"; font.pixelSize: 14; font.weight: Font.DemiBold; color: "#ffffff" }
                }
            }
            Text { text: "Refraction samples only this bounded Qt scene. Desktop Acrylic is blur, not a lens."; color: "#9993ab"; font.pixelSize: 11; Layout.fillWidth: true; wrapMode: Text.WordWrap }
        }
        Item {
            Layout.preferredWidth: 284; Layout.fillHeight: true
            Surface { anchors.fill: parent; tint: root.tint; surfaceOpacity: root.surfaceOpacity+.015; highlight: root.highlight; grain: root.grain; cornerRadius: root.radius }
            ScrollView {
                id: materialScroll
                objectName: "materialScroll"
                anchors.fill: parent; anchors.margins: 20
                clip: true; contentWidth: Math.max(0,availableWidth-14)
                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                ScrollBar.vertical: ScrollBar {
                    id: materialBar; objectName: "materialBar"
                    policy: ScrollBar.AsNeeded; active: true
                    implicitWidth: 6
                    contentItem: Rectangle { implicitWidth: 4; implicitHeight: 32; radius: 2; color: materialBar.pressed ? "#b9becc" : "#70798a" }
                }
                WheelHandler {
                    target: null
                    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                    onWheel: function(event) {
                        var delta=event.pixelDelta.y!==0 ? event.pixelDelta.y : event.angleDelta.y/120*36
                        if(delta===0)return
                        var viewport=materialScroll.contentItem
                        viewport.contentY=Math.max(0,Math.min(Math.max(0,viewport.contentHeight-viewport.height),viewport.contentY-delta))
                        event.accepted=true
                    }
                }
                function revealFocusedControl(control) {
                    if(!control || !contentItem)return
                    for(var ancestor=control.parent;ancestor;ancestor=ancestor.parent) {
                        if(ancestor===contentItem) {
                            var top=control.mapToItem(contentItem,0,0).y
                            var delta=top<0 ? top-6 : top+control.height>contentItem.height ? top+control.height-contentItem.height+6 : 0
                            if(delta!==0)contentItem.contentY=Math.max(0,Math.min(contentItem.contentHeight-contentItem.height,contentItem.contentY+delta))
                            return
                        }
                    }
                }
                Connections { target: root.Window.window; function onActiveFocusItemChanged() {materialScroll.revealFocusedControl(root.Window.window.activeFocusItem)} }
                ColumnLayout {
                    width: materialScroll.contentWidth; spacing: 8
                    Text { text: "Material"; color: "#f0edf7"; font.pixelSize: 18; font.weight: Font.DemiBold }
                    Text { text: "Tune the surface. Keep the content clear."; color: "#aaa4b8"; font.pixelSize: 11; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                    ComboBox {
                        id: modeCombo
                        objectName: "backgroundMode";Layout.fillWidth: true
                        model: ["Custom Desktop","Native Acrylic","Solid"]
                        currentIndex: root.backgroundMode;onActivated: root.backgroundMode=currentIndex
                        wheelEnabled: false
                        background: Rectangle {radius: 12;color: "#20262f";border.width: 1;border.color: "#5b566b"}
                        contentItem: Text {text: modeCombo.displayText;color: "#eee9f6";font.pixelSize: 12;leftPadding: 12;rightPadding: 25;verticalAlignment: Text.AlignVCenter}
                        indicator: Text {x: modeCombo.width-23;y: 8;text: "⌄";color: "#c7bddb"}
                        delegate: ItemDelegate {width: modeCombo.width;text: modelData;palette.windowText: "#eee9f6";background: Rectangle {color: parent.highlighted ? "#343044" : "#20262f"}}
                        popup.background: Rectangle {radius: 12;color: "#20262f";border.width: 1;border.color: "#5b566b"}
                    }
                    MaterialDial {objectName: "desktopBlur";title: "Custom blur radius";to: 24;factor: 1;suffix: " physical px";value: root.desktopBlur;enabled: root.customRequested;onValueChanged: root.desktopBlur=value}
                    MaterialDial {objectName: "desktopRefraction";title: "Desktop edge refraction";to: 16;factor: 1;suffix: " physical px";value: root.desktopRefraction;enabled: root.customRequested;onValueChanged: root.desktopRefraction=value}
                    MaterialDial {objectName: "desktopFill";title: "Desktop surface fill";to: .6;value: root.desktopFill;enabled: root.customRequested;onValueChanged: root.desktopFill=value}
                    CheckBox {id: reduceBox;text: "Reduce transparency (app)";checked: root.reduceTransparency;onToggled: root.reduceTransparency=checked;contentItem: Text {text: reduceBox.text;leftPadding: 26;color: "#ddd7e9";font.pixelSize: 12;verticalAlignment: Text.AlignVCenter}indicator: Rectangle {y: 6;width: 16;height: 16;radius: 5;color: reduceBox.checked?"#aba0c6":"transparent";border.color: "#837b98"}}
                    Text {text: "Custom Desktop uses live WGC monitor pixels. Native Acrylic has system-controlled blur. Below: separate in-app sample.";color: "#b1a9be";font.pixelSize: 10;Layout.fillWidth: true;wrapMode: Text.WordWrap}
                    RowLayout {
                        Layout.fillWidth: true; spacing: 4
                        Repeater {
                            model: ["Clear","Soft Frost","Solid"]
                            Pill {
                                objectName: "preset"+index; text: modelData; textSize: 12; Layout.fillWidth: true; implicitWidth: 70; implicitHeight: 32; padding: 7; cornerRadius: 11; grain: root.grain; tint: root.tint; highlight: root.highlight; surfaceOpacity: root.materialPreset===modelData ? .13 : .025; onClicked: root.applyMaterialPreset(modelData); Accessible.name: modelData+" material preset"
                                Rectangle { width: parent.width*.4; height: 1; x: parent.width*.3; y: parent.height-3; color: "#c6badd"; opacity: .75; visible: root.materialPreset===modelData }
                            }
                        }
                    }
                    MaterialDial { objectName: "tint"; title: "Lavender accent"; value: root.tint; onValueChanged: root.tint=value }
                    MaterialDial { objectName: "opacity"; title: "Surface opacity"; to: .65; value: root.surfaceOpacity; onValueChanged: root.surfaceOpacity=value }
                    MaterialDial { objectName: "highlight"; title: "Edge light"; value: root.highlight; onValueChanged: root.highlight=value }
                    MaterialDial { objectName: "radius"; title: "Corner radius"; from: 8; to: 32; factor: 1; suffix: " px"; value: root.radius; onValueChanged: root.radius=value }
                    MaterialDial { objectName: "grain"; title: "Fine grain"; value: root.grain; onValueChanged: root.grain=value }
                    MaterialDial { objectName: "shade"; title: "Desktop shade overlay"; to: .35; value: root.backdropShade; enabled: Lab.acrylic; onValueChanged: root.backdropShade=value }
                    Text { text: "Desktop Acrylic blur is controlled by Windows. Shade applies only when Acrylic is configured."; color: "#a7a6b2"; font.pixelSize: 10; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                    Rectangle { Layout.fillWidth: true; height: 1; color: "#ffffff"; opacity: .10 }
                    Text { text: "IN-APP LENS ONLY"; font.pixelSize: 10; font.letterSpacing: 1; color: "#b0a8c1" }
                    MaterialDial { objectName: "refraction"; title: "Refraction"; to: 8; factor: 1; suffix: " px"; value: root.refraction; onValueChanged: root.refraction=value }
                    MaterialDial { objectName: "blur"; title: "Sample diffusion"; to: 4; factor: 10; suffix: " / 10 px"; value: root.localBlur; onValueChanged: root.localBlur=value }
                    CheckBox {
                        objectName: "solid"; text: "Preview solid fallback"; checked: root.solidPreview
                        onToggled: {
                            if(root.materialPreset==="Solid" && !checked)root.applyMaterialPreset("Clear")
                            else root.solidPreview=checked
                        }
                        contentItem: Text { text: parent.text; leftPadding: 26; verticalAlignment: Text.AlignVCenter; color: "#ddd7e9"; font.pixelSize: 12 }
                        indicator: Rectangle { y: 6; width: 16; height: 16; radius: 5; color: parent.checked ? "#aba0c6" : "transparent"; border.color: "#837b98"; Text { anchors.centerIn: parent; text: parent.parent.checked ? "✓" : ""; color: "#17141f"; font.pixelSize: 12 } }
                    }
                    RowLayout {
                        Layout.fillWidth: true; spacing: 8
                        Pill { objectName: "reset"; text: "Reset"; onClicked: root.reset(); Layout.fillWidth: true; surfaceOpacity: .08 }
                        Pill { objectName: "save"; text: Lab.saving ? "Saving…" : "Save preset"; enabled: !Lab.saving; onClicked: Lab.save(root.presetValues()); Layout.fillWidth: true; accent: true; surfaceOpacity: .18 }
                    }
                    Text { text: Lab.saveStatus; color: "#b1a9c2"; font.pixelSize: 10; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                }
            }
        }
    }
    Item {
        id: footer
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom; leftMargin: 28; rightMargin: 28; bottomMargin: 18 }
        height: 56
        RowLayout {
            anchors.fill: parent; spacing: 12
            Rectangle { width: 5; height: 5; radius: 2.5; color: Lab.acrylic ? "#a2c7b0" : "#b5a8cf" }
            Text {
                text: root.backgroundMode===0 ? (root.reduceTransparency||root.solidPreview||Lab.highContrast ? "Solid: application accessibility / fallback preference" : !Exclusion.safe ? Exclusion.status : desktopSource.status+" · source age "+(desktopSource.frameAge<0?"n/a":Math.round(desktopSource.frameAge)+" ms"))+"\n"+Exclusion.status+"\nCustom Desktop ≠ Acrylic. Monitor frames transient/local; source validation required on each PC." : Lab.policy+"\n"+Lab.policyDetails
                color: "#b1aabb"; font.pixelSize: 10; Layout.fillWidth: true; wrapMode: Text.WordWrap
            }
            Pill { text: "Windows settings ↗"; implicitHeight: 32; cornerRadius: 12; surfaceOpacity: .08; onClicked: Lab.openWindowsSettings() }
        }
    }
    NumberAnimation on samplePhase { from: 0; to: Math.PI*2; duration: 4500; loops: Animation.Infinite; running: root.sampleMoving && !Lab.highContrast }
    Popup {
        id: demoPopup; objectName: "demoPopup"
        // Layout positions are not observable through mapToItem(). Recompute
        // at open, after layout has placed the actual button.
        onAboutToShow: {
            var anchor=gear.mapToItem(root,0,gear.height)
            x=Math.max(20,Math.min(root.width-width-20,anchor.x-180))
            y=Math.min(root.height-height-64,anchor.y+10)
        }
        width: 284; padding: 20
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        focus: true; modal: true; dim: false
        onClosed: gear.forceActiveFocus()
        background: Item {
            Rectangle { anchors.fill: parent; radius: root.radius; color: Lab.acrylic ? Qt.rgba(.045,.055,.075,.70) : "#182029" }
            Surface { anchors.fill: parent; tint: root.tint; cornerRadius: root.radius; highlight: root.highlight; grain: root.grain; surfaceOpacity: root.surfaceOpacity }
        }
        contentItem: ColumnLayout {
            spacing: 16
            RowLayout {
                Text { text: "Demo settings"; color: "#f3f0fa"; font.pixelSize: 15; font.weight: Font.DemiBold; Layout.fillWidth: true }
                Pill { objectName: "popupClose"; text: "×"; implicitWidth: 28; implicitHeight: 28; surfaceOpacity: 0; onClicked: demoPopup.close(); Accessible.name: "Close demo settings" }
            }
            Text { text: "A small anchored sheet, using the same material."; color: "#b7afc7"; font.pixelSize: 12; Layout.fillWidth: true; wrapMode: Text.WordWrap }
            Switch { objectName: "guides"; text: "Show optical guide lines"; checked: root.gridVisible; onToggled: root.gridVisible=checked; palette.windowText: "#e9e2f4"; palette.highlight: "#a697c2" }
            Pill { text: "Done"; Layout.fillWidth: true; accent: true; onClicked: demoPopup.close() }
        }
    }
}
