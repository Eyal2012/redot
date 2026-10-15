import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import QtMultimedia

Rectangle {
    id: root
    GlassTokens { id: tokens }
    color: mediaController.desktopGlass ? tokens.desktopTint : tokens.base
    // DWM supplies the sole outer contour, including maximize/restore.
    radius: 0
    property bool playback: mediaController.playing
    Item {
        id: mediaContent
        anchors.fill: parent
        // The compositor supplies this window's live desktop backdrop. Only
        // individual cards/video draw media; no full-window thumbnail image.
        GridView {
            id: gallery
            anchors.fill: parent
            anchors.leftMargin: 22; anchors.rightMargin: 22
            topMargin: 202; bottomMargin: 30
            visible: !root.playback
            model: mediaController
            cellWidth: width/Math.max(2,Math.floor(width/310))
            cellHeight: cellWidth*9/16+78
            clip: true; reuseItems: true; focus: true
            Keys.onReturnPressed: if(currentIndex>=0)mediaController.open(currentIndex)
            Keys.onEnterPressed: if(currentIndex>=0)mediaController.open(currentIndex)
            cacheBuffer: height/2
            ScrollBar.vertical: ScrollBar { width: 8 }
            delegate: Item {
                required property int index
                required property string clipTitle
                required property string thumbnail
                required property string metadata
                required property string dateText
                required property string clipError
                required property int audioCount
                width: gallery.cellWidth; height: gallery.cellHeight
                Rectangle {
                    id: card
                    anchors.fill: parent; anchors.margins: 10; radius: 14
                    color: hover.hovered?tokens.pillHover:tokens.pillIdle
                    border.width: gallery.currentIndex===index&&gallery.activeFocus?1:0
                    border.color: tokens.focus
                    Behavior on color { enabled: mediaController.motion; ColorAnimation { duration: 95 } }
                    Image { id: image; objectName: "clipThumbnail"; x: 1; y: 1; width: parent.width-2; height: width*9/16; source: thumbnail; fillMode: Image.PreserveAspectCrop; asynchronous: true; cache: false; smooth: true }
                    Rectangle { x: parent.width-66; y: 10; width: 54; height: 25; radius: 12; color: tokens.surfaceSolid; visible: audioCount>0; Text { anchors.centerIn: parent; text: audioCount+" audio"; color: tokens.text; font.pixelSize: 11 } }
                    Text { x: 12; y: image.height+10; width: parent.width-24; text: clipTitle; elide: Text.ElideRight; font.family: Design.fontFamily; font.pixelSize: 14; color: tokens.text }
                    Text { x: 12; y: image.height+34; width: parent.width-24; text: clipError.length?"Unreadable clip":metadata; elide: Text.ElideRight; font.pixelSize: 12; color: tokens.secondary }
                    HoverHandler { id: hover }
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: { gallery.currentIndex=index;gallery.forceActiveFocus();mediaController.open(index) } }
                    ToolTip.visible: hover.hovered; ToolTip.delay: 700; ToolTip.text: clipTitle+"\n"+metadata+"\n"+dateText+(clipError.length?"\n"+clipError:"")
                }
            }
        }
        Text { anchors.centerIn: parent; visible: !root.playback&&mediaController.count===0; text: "Your clips appear here\nRecord or save a replay, then refresh."; color: tokens.secondary; font.pixelSize: 20; horizontalAlignment: Text.AlignHCenter; lineHeight: 1.5 }
        Rectangle {
            id: videoArea
            x: 0; y: 0; width: root.width; height: root.height
            color: tokens.videoBlack; visible: root.playback
            VideoOutput {
                id: sharpVideo; objectName: "sharpVideo"
                anchors.fill: parent; fillMode: VideoOutput.PreserveAspectFit
                Component.onCompleted: mediaController.attachSink(videoSink)
            }
        }
    }
    GlassPanel {
        id: navigation; objectName: "navigationGlass"
        x: 32; y: 66; width: root.width-64; height: 64
        glass: true; radius: 26
        RowLayout {
            anchors.fill: parent; anchors.leftMargin: 20; anchors.rightMargin: 14; spacing: 12
            GlassButton { controlMotion: mediaController.motion; text: "‹ Library"; visible: root.playback; onClicked: mediaController.back() }
            Text { text: root.playback?"Now playing":"View Media"; font.family: Design.fontFamily; font.pixelSize: 19; font.weight: Font.DemiBold; color: tokens.text }
            Text { visible: !root.playback; text: mediaController.count+" clips"; color: tokens.secondary; font.pixelSize: 12 }
            Item { Layout.fillWidth: true }
            TextField {
                id: search
                visible: !root.playback
                Layout.preferredWidth: Math.min(300,root.width*0.28)
                placeholderText: "Search clips"; color: tokens.text; placeholderTextColor: tokens.secondary
                background: Rectangle { color: tokens.pillIdle; border.color: tokens.outline; radius: tokens.controlRadius }
                onTextEdited: queryDelay.restart()
            }
            GlassButton { controlMotion: mediaController.motion; text: "Refresh"; visible: !root.playback; onClicked: mediaController.refresh() }
            GlassButton { controlMotion: mediaController.motion; text: "Open Folder"; onClicked: mediaController.folder() }
        }
    }
    Item {
        id: titleGlass; objectName: "mediaTitleBar"
        x: 6; y: 5; width: root.width-12; height: 36
        Image { x: 12; anchors.verticalCenter: parent.verticalCenter; width: 22; height: 22; source: "qrc:/ReDot/idle-64.png"; smooth: true }
        Text { x: 40; anchors.verticalCenter: parent.verticalCenter; text: "ReDot · View Media"; color: tokens.text; font.family: Design.fontFamily; font.pixelSize: 13 }
        Row {
            anchors.right: parent.right; anchors.rightMargin: 2; anchors.verticalCenter: parent.verticalCenter; spacing: 4
            Repeater {
                model: ["Minimize","Maximize / Restore","Close"]
                delegate: GlassButton { controlMotion: mediaController.motion;
                    required property string modelData
                    required property int index
                    objectName: "mediaWindow"+index; width: 43; height: 32
                    property bool nativeHover: false
                    property bool nativePressed: false
                    Accessible.name: index===1?(mediaController.maximized?"Restore":"Maximize"):modelData
                    text: index===0?"—":index===1?(mediaController.maximized?"❐":"□"):"×"
                    onClicked: mediaController.windowAction(index)
                    contentItem: Text { text: parent.text; color: tokens.text; font.pixelSize: 17; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                    background: Rectangle { radius: 9; color: (parent.down||parent.nativePressed)?tokens.pillPressed:(parent.hovered||parent.nativeHover)?(index===2?tokens.closeHover:tokens.pillHover):tokens.pillIdle; border.width: parent.activeFocus?2:1; border.color: tokens.outline }
                    ToolTip.visible: hovered; ToolTip.text: modelData
                }
            }
        }
    }
    Timer { id: queryDelay; interval: 180; onTriggered: mediaController.query(search.text,filterChoice.currentText,sortChoice.currentText) }
    GlassPanel {
        id: filters; objectName: "filtersGlass"
        x: 32; y: 145; width: Math.min(root.width-64,620); height: 46
        visible: !root.playback
        glass: true; radius: 23
        RowLayout {
            anchors.fill: parent; anchors.leftMargin: 14; anchors.rightMargin: 14; spacing: 14
            Text { text: "Show"; color: tokens.secondary; font.pixelSize: 12 }
            GlassChoice { controlMotion: mediaController.motion; id: filterChoice; objectName: "filterChoice"; model: ["All","Replay","Recording","Has audio","Unreadable"]; Layout.preferredWidth: 156; onActivated: queryDelay.restart() }
            Text { text: "Sort"; color: tokens.secondary; font.pixelSize: 12 }
            GlassChoice { controlMotion: mediaController.motion; id: sortChoice; model: ["Newest","Oldest","Name","Duration","Size (largest first)","Size (smallest first)"]; Layout.preferredWidth: 180; onActivated: queryDelay.restart() }
            Item { Layout.fillWidth: true }
        }
    }
    GlassPanel {
        id: mediaError
        x: 32; y: root.height-88; width: root.width-64; height: 60
        visible: !root.playback && mediaController.status.length>0
        glass: true; radius: 22
        Text { anchors.fill: parent; anchors.margins: 14; text: mediaController.status; color: tokens.error; wrapMode: Text.Wrap; font.pixelSize: 13 }
    }
    GlassPanel {
        id: playbackBar; objectName: "playbackGlass"
        x: 32; y: Math.max(105,Math.min(root.height-142,sharpVideo.contentRect.y+sharpVideo.contentRect.height-138)); width: root.width-64; height: 115
        visible: root.playback
        glass: false; radius: 28
        ColumnLayout {
            anchors.fill: parent; anchors.margins: 14; spacing: 6
            RowLayout {
                Layout.fillWidth: true
                Text { text: mediaController.title; elide: Text.ElideMiddle; Layout.fillWidth: true; color: tokens.text; font.pixelSize: 13 }
                Text { text: mediaController.status; color: tokens.secondary; font.pixelSize: 12 }
            }
            RowLayout {
                Layout.fillWidth: true; spacing: 10
                GlassButton { controlMotion: mediaController.motion; text: mediaController.paused?"Play":"Pause"; onClicked: mediaController.toggle() }
                GlassSlider { controlMotion: mediaController.motion; id: seek; Layout.fillWidth: true; from: 0; to: Math.max(0.1,mediaController.duration); value: mediaController.position; onMoved: mediaController.seek(value) }
                GlassButton { controlMotion: mediaController.motion; text: "Fullscreen"; onClicked: mediaController.fullscreen() }
            }
            RowLayout {
                Layout.fillWidth: true; spacing: 12
                GlassButton { controlMotion: mediaController.motion; text: mediaController.muted?"Unmute":"Mute"; checked: mediaController.muted; onClicked: mediaController.mute() }
                GlassSlider { controlMotion: mediaController.motion; from: 0; to: 1; value: mediaController.playbackVolume; Layout.preferredWidth: 120; onMoved: mediaController.volume(value) }
                Text { text: "Audio track"; color: tokens.secondary; font.pixelSize: 12 }
                GlassChoice { controlMotion: mediaController.motion; model: mediaController.tracks; currentIndex: mediaController.activeTrack; Layout.preferredWidth: 190; enabled: mediaController.tracks.length>0; onActivated: mediaController.track(currentIndex) }
                Item { Layout.fillWidth: true }
                GlassButton { controlMotion: mediaController.motion; text: "External player"; onClicked: mediaController.external() }
            }
        }
    }
    Shortcut { sequence: "Escape"; onActivated: mediaController.escape() }
    Row {
        objectName:"desktopBackdropFeedback"
        anchors.right:parent.right; anchors.rightMargin:32; anchors.bottom:parent.bottom; anchors.bottomMargin:16
        visible:!root.playback&&mediaController.windowsTransparencyDisabled
        GlassButton { controlMotion: mediaController.motion; text:"Open Windows transparency settings"; onClicked:mediaController.openWindowsTransparencySettings(); ToolTip.visible:hovered; ToolTip.text:mediaController.backdropStatus }
    }
    Shortcut { sequence: "Space"; enabled: root.playback; onActivated: mediaController.toggle() }
}
