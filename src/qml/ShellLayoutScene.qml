import QtQuick 2.15

Item {
    id: root
    property bool handlesOnly: false
    readonly property var handleModel: [
        { key: "timeline", cursor: Qt.SizeVerCursor },
        { key: "nodeEditor", cursor: Qt.SizeVerCursor },
        { key: "mix", cursor: Qt.SizeVerCursor },
        { key: "audioPool", cursor: Qt.SizeHorCursor }
    ]

    AppTheme {
        id: theme
    }

    function rectValue(mapValue, key) {
        return mapValue && mapValue[key] !== undefined ? mapValue[key] : 0
    }

    function rectVisible(mapValue) {
        return mapValue && mapValue.visible === true
    }

    function handleRectFor(key) {
        if (key === "timeline") {
            return shellLayoutController.timelineHandleRect
        }
        if (key === "nodeEditor") {
            return shellLayoutController.nodeEditorHandleRect
        }
        if (key === "mix") {
            return shellLayoutController.mixHandleRect
        }
        if (key === "audioPool") {
            return shellLayoutController.audioPoolHandleRect
        }
        return null
    }

    onWidthChanged: shellLayoutController.setViewportSize(width, height)
    onHeightChanged: shellLayoutController.setViewportSize(width, height)
    Component.onCompleted: shellLayoutController.setViewportSize(width, height)

    Rectangle {
        anchors.fill: parent
        color: theme.shellBackground
        visible: !root.handlesOnly
    }

    Rectangle {
        x: rectValue(shellLayoutController.canvasRect, "x")
        y: rectValue(shellLayoutController.canvasRect, "y")
        width: rectValue(shellLayoutController.canvasRect, "width")
        height: rectValue(shellLayoutController.canvasRect, "height")
        visible: rectVisible(shellLayoutController.canvasRect)
                 && !root.handlesOnly
        color: "#090c10"
        border.width: 1
        border.color: "#151b23"
    }

    Rectangle {
        x: rectValue(shellLayoutController.thumbnailRect, "x")
        y: rectValue(shellLayoutController.thumbnailRect, "y")
        width: rectValue(shellLayoutController.thumbnailRect, "width")
        height: rectValue(shellLayoutController.thumbnailRect, "height")
        visible: rectVisible(shellLayoutController.thumbnailRect)
                 && !root.handlesOnly
        color: "#050608"
        border.width: 1
        border.color: "#151b23"
    }

    Rectangle {
        x: rectValue(shellLayoutController.timelineRect, "x")
        y: rectValue(shellLayoutController.timelineRect, "y")
        width: rectValue(shellLayoutController.timelineRect, "width")
        height: rectValue(shellLayoutController.timelineRect, "height")
        visible: rectVisible(shellLayoutController.timelineRect)
                 && !root.handlesOnly
        color: "#050608"
        border.width: 1
        border.color: "#151b23"
    }

    Rectangle {
        x: rectValue(shellLayoutController.mixRect, "x")
        y: rectValue(shellLayoutController.mixRect, "y")
        width: rectValue(shellLayoutController.mixRect, "width")
        height: rectValue(shellLayoutController.mixRect, "height")
        visible: rectVisible(shellLayoutController.mixRect)
                 && !root.handlesOnly
        color: "#080b10"
        border.width: 1
        border.color: "#151b23"
    }

    Rectangle {
        x: rectValue(shellLayoutController.nodeEditorRect, "x")
        y: rectValue(shellLayoutController.nodeEditorRect, "y")
        width: rectValue(shellLayoutController.nodeEditorRect, "width")
        height: rectValue(shellLayoutController.nodeEditorRect, "height")
        visible: rectVisible(shellLayoutController.nodeEditorRect)
                 && !root.handlesOnly
        color: "#0a0e14"
        border.width: 1
        border.color: "#151b23"
    }

    Rectangle {
        x: rectValue(shellLayoutController.audioPoolRect, "x")
        y: rectValue(shellLayoutController.audioPoolRect, "y")
        width: rectValue(shellLayoutController.audioPoolRect, "width")
        height: rectValue(shellLayoutController.audioPoolRect, "height")
        visible: rectVisible(shellLayoutController.audioPoolRect)
                 && !root.handlesOnly
        color: "#07090c"
        border.width: 1
        border.color: "#151b23"
    }

    Repeater {
        model: root.handleModel

        Item {
            required property var modelData

            readonly property var handleRect: root.handleRectFor(modelData.key)
            property bool hoverGlowVisible: false

            x: rectValue(handleRect, "x")
            y: rectValue(handleRect, "y")
            width: rectValue(handleRect, "width")
            height: rectValue(handleRect, "height")
            visible: rectVisible(handleRect)

            Rectangle {
                anchors.fill: parent
                color: "#161c24"
            }

            Rectangle {
                anchors.fill: parent
                color: dragArea.pressed ? theme.resizeHandlePressed : theme.resizeHandleHover
                opacity: dragArea.pressed ? 1.0 : (hoverGlowVisible ? 1.0 : 0.0)

                Behavior on opacity {
                    NumberAnimation {
                        duration: 150
                    }
                }
            }

            MouseArea {
                id: dragArea

                anchors.fill: parent
                hoverEnabled: true
                preventStealing: true
                cursorShape: modelData.cursor
                acceptedButtons: Qt.LeftButton

                function rootPoint(mouse) {
                    return dragArea.mapToItem(root, mouse.x, mouse.y)
                }

                onPressed: function(mouse) {
                    hoverTimer.stop()
                    hoverGlowVisible = false
                    var point = rootPoint(mouse)
                    shellLayoutController.beginResize(modelData.key, point.x, point.y)
                    mouse.accepted = true
                }

                onPositionChanged: function(mouse) {
                    if (pressed) {
                        var point = rootPoint(mouse)
                        shellLayoutController.updateResize(point.x, point.y)
                    }
                }

                onContainsMouseChanged: {
                    if (containsMouse) {
                        hoverTimer.restart()
                    } else {
                        hoverTimer.stop()
                        hoverGlowVisible = false
                    }
                }

                onReleased: shellLayoutController.endResize()
                onCanceled: shellLayoutController.endResize()
            }

            Timer {
                id: hoverTimer
                interval: 1000
                repeat: false
                onTriggered: {
                    if (dragArea.containsMouse && !dragArea.pressed) {
                        hoverGlowVisible = true
                    }
                }
            }
        }
    }
}
