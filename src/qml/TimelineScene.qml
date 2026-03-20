import QtQuick 2.15

Item {
    id: timelineRoot

    focus: true

    Keys.onTabPressed: function(event) {
        videoViewportBridge.requestSelectNextNode()
        event.accepted = true
    }

    function rectValue(mapValue, key) {
        return mapValue && mapValue[key] !== undefined ? mapValue[key] : 0
    }

    function boolValue(mapValue, key) {
        return mapValue && mapValue[key] !== undefined ? Boolean(mapValue[key]) : false
    }

    function globalPoint(localX, localY) {
        var mapped = timelineRoot.mapToGlobal(Qt.point(localX, localY))
        return mapped ? mapped : Qt.point(0, 0)
    }

    onWidthChanged: timelineController.setViewportSize(width, height)
    onHeightChanged: timelineController.setViewportSize(width, height)
    Component.onCompleted: timelineController.setViewportSize(width, height)

    Rectangle {
        anchors.fill: parent
        color: "#050608"
    }

    Rectangle {
        x: rectValue(timelineController.timelineRect, "x")
        y: rectValue(timelineController.timelineRect, "y")
        width: rectValue(timelineController.timelineRect, "width")
        height: rectValue(timelineController.timelineRect, "height")
        color: "#07080a"
        border.width: 1
        border.color: "#20242a"
    }

    Item {
        x: rectValue(timelineController.filmstripRect, "x")
        y: rectValue(timelineController.filmstripRect, "y")
        width: rectValue(timelineController.filmstripRect, "width")
        height: rectValue(timelineController.filmstripRect, "height")
        visible: height > 0
        clip: true

        Rectangle {
            anchors.fill: parent
            radius: 4
            color: "#10151b"
            border.width: 1
            border.color: "#242a31"
        }

        Repeater {
            model: timelineController.thumbnailTiles

            Item {
                property var tileData: modelData
                x: tileData.x - parent.x
                y: 0
                width: Math.max(1, tileData.width)
                height: parent.height

                Rectangle {
                    anchors.fill: parent
                    color: "#141a21"
                    border.width: 1
                    border.color: "#0c1015"
                }

                Image {
                    anchors.fill: parent
                    anchors.margins: 1
                    source: tileData.source
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    cache: true
                    smooth: false
                }
            }
        }
    }

    Rectangle {
        x: rectValue(timelineController.loopBarRect, "x")
        y: rectValue(timelineController.loopBarRect, "y")
        width: rectValue(timelineController.loopBarRect, "width")
        height: rectValue(timelineController.loopBarRect, "height")
        radius: 4
        color: "#12161b"
        border.width: 1
        border.color: "#242930"
    }

    Rectangle {
        x: rectValue(timelineController.trackAreaRect, "x")
        y: rectValue(timelineController.trackAreaRect, "y")
        width: rectValue(timelineController.trackAreaRect, "width")
        height: rectValue(timelineController.trackAreaRect, "height")
        radius: 4
        color: "#080a0d"
    }

    Repeater {
        model: timelineController.gridLines

        Rectangle {
            property var lineData: modelData
            x: lineData.x
            y: rectValue(timelineController.loopBarRect, "y")
            width: 1
            height: rectValue(timelineController.trackAreaRect, "y")
                + rectValue(timelineController.trackAreaRect, "height")
                - rectValue(timelineController.loopBarRect, "y")
            color: lineData.major ? "#282c32" : "#1a1d22"
        }
    }

    Repeater {
        model: timelineController.loopRangeGeometries

        Item {
            property var loopData: modelData

            Rectangle {
                visible: boolValue(loopData, "selectionVisible")
                x: rectValue(loopData.selectionRect, "x")
                y: rectValue(loopData.selectionRect, "y")
                width: rectValue(loopData.selectionRect, "width")
                height: rectValue(loopData.selectionRect, "height")
                radius: 3
                color: loopData.selected ? "#5a88ca5c" : "#426eaa48"
                border.width: loopData.selected ? 2 : 1
                border.color: loopData.selected ? "#b4d4ffec" : "#75a5e4dc"
            }

            Rectangle {
                visible: boolValue(loopData, "startHandleVisible")
                x: rectValue(loopData.startHandleRect, "x")
                y: rectValue(loopData.startHandleRect, "y")
                width: rectValue(loopData.startHandleRect, "width")
                height: rectValue(loopData.startHandleRect, "height")
                radius: 2
                color: "#e9eff6"
            }

            Rectangle {
                visible: boolValue(loopData, "endHandleVisible")
                x: rectValue(loopData.endHandleRect, "x")
                y: rectValue(loopData.endHandleRect, "y")
                width: rectValue(loopData.endHandleRect, "width")
                height: rectValue(loopData.endHandleRect, "height")
                radius: 2
                color: "#e9eff6"
            }
        }
    }

    Rectangle {
        visible: timelineController.hasLoopIndicator
        x: timelineController.loopIndicatorX
        y: rectValue(timelineController.loopBarRect, "y") + 1
        width: 1
        height: rectValue(timelineController.loopBarRect, "height") - 2
        color: "#dce4ecb0"
        opacity: 0.9
    }

    Rectangle {
        visible: timelineController.hasPendingLoopDraftHandle
        x: timelineController.pendingLoopDraftHandleX - 3.5
        y: rectValue(timelineController.loopBarRect, "y") - 1
        width: 7
        height: rectValue(timelineController.loopBarRect, "height") + 2
        radius: 2
        color: "#8ec7ff"
        border.width: 1
        border.color: "#d6ecff"
        opacity: 0.95
    }

    Repeater {
        model: timelineController.trackGeometries

        Item {
            property var trackData: modelData

            Rectangle {
                x: trackData.lineStartX
                y: trackData.y - (trackData.selected ? 1 : 0.5)
                width: Math.max(1, trackData.lineEndX - trackData.lineStartX)
                height: trackData.selected ? 2 : 1
                color: trackData.color
                opacity: trackData.selected ? 1.0 : 0.85
            }

            Rectangle {
                x: trackData.lineStartX
                y: trackData.y - trackData.capHeight * 0.5
                width: trackData.selected ? 2 : 1
                height: trackData.capHeight
                color: trackData.color
                opacity: trackData.selected ? 1.0 : 0.85
            }

            Rectangle {
                x: trackData.lineEndX
                y: trackData.y - trackData.capHeight * 0.5
                width: trackData.selected ? 2 : 1
                height: trackData.capHeight
                color: trackData.color
                opacity: trackData.selected ? 1.0 : 0.85
            }
        }
    }

    Rectangle {
        visible: timelineController.hasHoverLine
        x: timelineController.hoverX
        y: rectValue(timelineController.loopBarRect, "y")
        width: 1
        height: rectValue(timelineController.trackAreaRect, "y")
            + rectValue(timelineController.trackAreaRect, "height")
            - rectValue(timelineController.loopBarRect, "y")
        color: "#d6dce484"
    }

    Rectangle {
        x: timelineController.markerX - 1
        y: rectValue(timelineController.loopBarRect, "y")
        width: 2
        height: rectValue(timelineController.trackAreaRect, "y")
            + rectValue(timelineController.trackAreaRect, "height")
            - rectValue(timelineController.loopBarRect, "y")
        color: "#e1e5ea"
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        hoverEnabled: !timelineController.playbackActive
        preventStealing: true
        cursorShape: timelineController.cursorShape

        onPressed: function(mouse) {
            timelineRoot.forceActiveFocus()
            var gp = timelineRoot.globalPoint(mouse.x, mouse.y)
            timelineController.handleMousePress(mouse.button, mouse.x, mouse.y, mouse.modifiers, gp.x, gp.y)
            mouse.accepted = true
        }

        onDoubleClicked: function(mouse) {
            timelineRoot.forceActiveFocus()
            timelineController.handleMouseDoubleClick(mouse.button, mouse.x, mouse.y)
            mouse.accepted = true
        }

        onPositionChanged: function(mouse) {
            var gp = timelineRoot.globalPoint(mouse.x, mouse.y)
            if (mouse.buttons & (Qt.LeftButton | Qt.RightButton)) {
                timelineController.handleMouseMove(mouse.x, mouse.y, gp.x, gp.y)
            } else {
                timelineController.handleHoverMove(mouse.x, mouse.y, gp.x, gp.y)
            }
        }

        onReleased: function(mouse) {
            timelineController.handleMouseRelease(mouse.button)
            mouse.accepted = true
        }

        onWheel: function(wheel) {
            var gp = timelineRoot.globalPoint(wheel.x, wheel.y)
            timelineController.handleWheel(wheel.x, wheel.y, wheel.angleDelta.y, wheel.modifiers, gp.x, gp.y)
            wheel.accepted = true
        }

        onCanceled: {
            timelineController.handleMouseRelease(Qt.LeftButton)
        }

        onExited: timelineController.handleHoverLeave()
    }
}
