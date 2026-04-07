import QtQuick 2.15

Item {
    id: thumbnailRoot
    objectName: "thumbnailStripScene"
    focus: true

    function rectValue(mapValue, key) {
        return mapValue && mapValue[key] !== undefined ? mapValue[key] : 0
    }

    onWidthChanged: thumbnailStripController.setViewportSize(width, height)
    onHeightChanged: thumbnailStripController.setViewportSize(width, height)
    Component.onCompleted: thumbnailStripController.setViewportSize(width, height)

    Rectangle {
        anchors.fill: parent
        color: "#050608"
    }

    Item {
        x: rectValue(thumbnailStripController.stripRect, "x")
        y: rectValue(thumbnailStripController.stripRect, "y")
        width: rectValue(thumbnailStripController.stripRect, "width")
        height: rectValue(thumbnailStripController.stripRect, "height")
        clip: true

        Rectangle {
            anchors.fill: parent
            radius: 4
            color: "#10151b"
            border.width: 1
            border.color: "#242a31"
        }

        Repeater {
            model: thumbnailStripController.thumbnailTiles

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

        Rectangle {
            visible: thumbnailStripController.hasSelectedNodeRange
            x: thumbnailStripController.selectedNodeRangeX - parent.x
            y: 0
            width: Math.max(1, thumbnailStripController.selectedNodeRangeWidth)
            height: parent.height
            color: "#000000"
            opacity: 0.5
        }

        Rectangle {
            x: thumbnailStripController.markerX - parent.x - 1
            y: 0
            width: 2
            height: parent.height
            color: "#e1e5ea"
        }

        Rectangle {
            visible: thumbnailStripController.hasHoverLine
            x: thumbnailStripController.hoverX - parent.x
            y: 0
            width: 1
            height: parent.height
            color: "#d6dce484"
        }
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        hoverEnabled: !thumbnailStripController.playbackActive
        preventStealing: true
        cursorShape: thumbnailStripController.playbackActive ? Qt.ArrowCursor : Qt.PointingHandCursor

        onPressed: function(mouse) {
            thumbnailRoot.forceActiveFocus()
            thumbnailStripController.handleMousePress(mouse.button, mouse.x, mouse.y)
            mouse.accepted = true
        }

        onPositionChanged: function(mouse) {
            if (mouse.buttons & Qt.LeftButton) {
                thumbnailStripController.handleMouseMove(mouse.x, mouse.y)
            } else {
                thumbnailStripController.handleHoverMove(mouse.x, mouse.y)
            }
        }

        onReleased: function(mouse) {
            thumbnailStripController.handleMouseRelease(mouse.button)
            mouse.accepted = true
        }

        onCanceled: thumbnailStripController.handleMouseRelease(Qt.LeftButton)
        onExited: thumbnailStripController.handleHoverLeave()
    }
}
