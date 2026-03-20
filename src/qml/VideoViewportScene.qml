import QtQuick
import QtQuick.Controls
import Dawg 1.0

Rectangle {
    id: root
    color: "#0c1016"
    clip: true
    focus: true

    property point pressPoint: Qt.point(0, 0)
    property rect selectionRect: Qt.rect(0, 0, 0, 0)
    property bool pendingSeed: false
    property bool marqueeSelecting: false
    property string draggedTrackId: ""
    property bool draggedTrackWasSelected: false
    property string hoveredTrackId: ""

    Keys.onTabPressed: function(event) {
        videoViewportBridge.requestSelectNextVisibleNode()
        event.accepted = true
    }

    readonly property var frameRectData: videoViewportController.frameRect(width, height)
    readonly property rect frameRect: Qt.rect(
        frameRectData.x || 0,
        frameRectData.y || 0,
        frameRectData.width || 0,
        frameRectData.height || 0)
    readonly property real scaleX: frameRect.width / Math.max(1, videoViewportController.sourceWidth)
    readonly property real scaleY: frameRect.height / Math.max(1, videoViewportController.sourceHeight)

    function normalizedRect(x1, y1, x2, y2) {
        const left = Math.min(x1, x2)
        const top = Math.min(y1, y2)
        return Qt.rect(left, top, Math.abs(x2 - x1), Math.abs(y2 - y1))
    }

    function droppedAssetPath(drop) {
        if (drop.formats && drop.formats.indexOf("application/x-dawg-audio-path") >= 0
                && drop.getDataAsString) {
            return drop.getDataAsString("application/x-dawg-audio-path")
        }
        if (drop.hasUrls && drop.urls.length > 0) {
            return drop.urls[0].toLocalFile()
        }
        if (drop.text) {
            return drop.text.trim()
        }
        return ""
    }

    Rectangle {
        anchors.fill: parent
        color: "#0c1016"
    }

    Rectangle {
        x: root.frameRect.x
        y: root.frameRect.y
        width: root.frameRect.width
        height: root.frameRect.height
        color: "#181d24"
        border.width: 1
        border.color: "#3c4654"
        visible: root.frameRect.width > 0 && root.frameRect.height > 0
    }

    Image {
        id: frameImage
        x: root.frameRect.x
        y: root.frameRect.y
        width: root.frameRect.width
        height: root.frameRect.height
        visible: videoViewportController.hasFrame && !videoViewportController.nativePresentationActive
        source: videoViewportController.hasFrame && !videoViewportController.nativePresentationActive
            ? videoViewportController.frameSource
            : ""
        asynchronous: false
        cache: false
        smooth: true
        fillMode: Image.PreserveAspectFit
    }

    VideoViewportQuickItem {
        x: root.frameRect.x
        y: root.frameRect.y
        width: root.frameRect.width
        height: root.frameRect.height
        visible: videoViewportController.nativePresentationActive
        controller: videoViewportController
    }

    Item {
        anchors.fill: parent
        visible: videoViewportController.hasFrame

        VideoOverlayQuickItem {
            anchors.fill: parent
            controller: videoViewportController
        }

        Rectangle {
            visible: root.marqueeSelecting
            x: root.selectionRect.x
            y: root.selectionRect.y
            width: root.selectionRect.width
            height: root.selectionRect.height
            color: "#0f3f8f"
            opacity: 0.34
            border.width: 1
            border.color: "#8fc2ff"
        }
    }

    Item {
        visible: !videoViewportController.hasFrame
        anchors.fill: parent

        Rectangle {
            width: Math.max(420, Math.min(parent.width - 40, 680))
            height: Math.min(parent.height - 40, 280)
            anchors.centerIn: parent
            radius: 22
            color: "#eb101218"
            border.width: 1
            border.color: "#8c46566c"

            Column {
                anchors.fill: parent
                anchors.margins: 24
                spacing: 12

                Image {
                    source: "qrc:/branding/logo-transparent.png"
                    fillMode: Image.PreserveAspectFit
                    width: Math.min(parent.width, 480)
                    height: 110
                    anchors.horizontalCenter: parent.horizontalCenter
                }

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "Video-first sound staging"
                    color: "#e8eef4"
                    font.pointSize: 16
                    font.bold: true
                }

                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    text: "Import a video to start placing nodes, attach sound, and stage movement directly on the film."
                    color: "#b6bfc9"
                    font.pointSize: 11
                }

                Button {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "Import Video..."
                    onClicked: videoViewportBridge.requestImportVideo()
                }
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        cursorShape: root.draggedTrackId !== ""
            ? Qt.ClosedHandCursor
            : (root.hoveredTrackId !== "" ? Qt.PointingHandCursor : Qt.ArrowCursor)

        onPressed: function(mouse) {
            if (!videoViewportController.hasFrame) {
                return
            }

            root.forceActiveFocus()

            const trackId = videoViewportController.trackIdAt(mouse.x, mouse.y, root.width, root.height)
            if (mouse.button === Qt.RightButton) {
                if (trackId !== "") {
                    videoViewportBridge.requestTrackContextMenu(trackId, mouse.x, mouse.y)
                }
                return
            }

            if (mouse.button !== Qt.LeftButton) {
                return
            }

            root.draggedTrackWasSelected = false
            if (trackId !== "") {
                root.draggedTrackWasSelected = videoViewportController.overlayIsSelected(trackId)
                if ((mouse.modifiers & Qt.ControlModifier)
                        && videoViewportController.overlayHasAttachedAudio(trackId)) {
                    videoViewportBridge.requestTrackSelected(trackId)
                    videoViewportBridge.requestTrackGainPopup(trackId, mouse.x, mouse.y)
                    return
                }

                root.draggedTrackId = trackId
                if (!root.draggedTrackWasSelected) {
                    videoViewportBridge.requestTrackSelected(trackId)
                }
                return
            }

            root.pressPoint = Qt.point(mouse.x, mouse.y)
            root.selectionRect = Qt.rect(mouse.x, mouse.y, 0, 0)
            root.pendingSeed = true
            root.marqueeSelecting = false
        }

        onPositionChanged: function(mouse) {
            if (!videoViewportController.hasFrame) {
                root.hoveredTrackId = ""
                return
            }

            root.hoveredTrackId = videoViewportController.trackIdAt(mouse.x, mouse.y, root.width, root.height)
            if (root.draggedTrackId !== "") {
                const point = videoViewportController.widgetToImagePoint(mouse.x, mouse.y, root.width, root.height)
                videoViewportBridge.requestSelectedTrackMoved(point.x, point.y)
                return
            }

            if (!root.pendingSeed) {
                return
            }

            const dx = mouse.x - root.pressPoint.x
            const dy = mouse.y - root.pressPoint.y
            if (!root.marqueeSelecting && Math.hypot(dx, dy) >= 6) {
                root.marqueeSelecting = true
            }

            if (root.marqueeSelecting) {
                root.selectionRect = root.normalizedRect(
                    root.pressPoint.x,
                    root.pressPoint.y,
                    mouse.x,
                    mouse.y)
            }
        }

        onReleased: function(mouse) {
            root.hoveredTrackId = videoViewportController.trackIdAt(mouse.x, mouse.y, root.width, root.height)
            if (mouse.button !== Qt.LeftButton) {
                return
            }

            if (root.draggedTrackId !== "") {
                root.draggedTrackId = ""
                root.draggedTrackWasSelected = false
                return
            }

            if (root.marqueeSelecting) {
                const selectedIds = videoViewportController.tracksInRect(
                    root.selectionRect.x,
                    root.selectionRect.y,
                    root.selectionRect.width,
                    root.selectionRect.height,
                    root.width,
                    root.height)
                videoViewportBridge.requestTracksSelected(selectedIds)
                root.marqueeSelecting = false
                root.pendingSeed = false
                root.draggedTrackWasSelected = false
                root.selectionRect = Qt.rect(0, 0, 0, 0)
                return
            }

            if (root.pendingSeed) {
                const point = videoViewportController.widgetToImagePoint(mouse.x, mouse.y, root.width, root.height)
                videoViewportBridge.requestSeedPoint(point.x, point.y)
            }

            root.pendingSeed = false
            root.draggedTrackWasSelected = false
            root.selectionRect = Qt.rect(0, 0, 0, 0)
        }

        onDoubleClicked: function(mouse) {
            if (!videoViewportController.hasFrame || mouse.button !== Qt.LeftButton) {
                return
            }

            const trackId = videoViewportController.trackIdAt(mouse.x, mouse.y, root.width, root.height)
            if (trackId === "") {
                return
            }

            videoViewportBridge.requestTrackSelected(trackId)
            videoViewportBridge.requestTrackActivated(trackId)
        }

        onWheel: function(wheel) {
            if (!videoViewportController.hasFrame
                    || !(wheel.modifiers & Qt.ControlModifier)
                    || wheel.angleDelta.y === 0) {
                return
            }

            const trackId = videoViewportController.trackIdAt(wheel.x, wheel.y, root.width, root.height)
            if (trackId === ""
                    || !videoViewportController.overlayHasAttachedAudio(trackId)
                    || !videoViewportController.overlayIsSelected(trackId)) {
                return
            }

            videoViewportBridge.requestTrackGainAdjust(trackId, wheel.angleDelta.y, wheel.x, wheel.y)
            wheel.accepted = true
        }

        onExited: root.hoveredTrackId = ""
    }

    DropArea {
        anchors.fill: parent

        onEntered: function(drag) {
            if (videoViewportController.hasFrame
                    && root.droppedAssetPath(drag).length > 0) {
                drag.accepted = true
            }
        }

        onDropped: function(drop) {
            if (!videoViewportController.hasFrame) {
                return
            }

            const assetPath = root.droppedAssetPath(drop)
            if (assetPath.length === 0) {
                return
            }

            const point = videoViewportController.widgetToImagePoint(
                drop.x,
                drop.y,
                root.width,
                root.height)
            videoViewportBridge.requestAudioDropped(assetPath, point.x, point.y)
            drop.accepted = true
        }
    }
}
