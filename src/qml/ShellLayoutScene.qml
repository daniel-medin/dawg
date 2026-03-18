import QtQuick 2.15

Item {
    id: root

    AppTheme {
        id: theme
    }

    function rectValue(mapValue, key) {
        return mapValue && mapValue[key] !== undefined ? mapValue[key] : 0
    }

    function rectVisible(mapValue) {
        return mapValue && mapValue.visible === true
    }

    onWidthChanged: shellLayoutController.setViewportSize(width, height)
    onHeightChanged: shellLayoutController.setViewportSize(width, height)
    Component.onCompleted: shellLayoutController.setViewportSize(width, height)

    Rectangle {
        anchors.fill: parent
        color: theme.shellBackground
    }

    Rectangle {
        x: rectValue(shellLayoutController.canvasRect, "x")
        y: rectValue(shellLayoutController.canvasRect, "y")
        width: rectValue(shellLayoutController.canvasRect, "width")
        height: rectValue(shellLayoutController.canvasRect, "height")
        visible: rectVisible(shellLayoutController.canvasRect)
        color: "#090c10"
        border.width: 1
        border.color: "#151b23"
    }

    Rectangle {
        x: rectValue(shellLayoutController.timelineRect, "x")
        y: rectValue(shellLayoutController.timelineRect, "y")
        width: rectValue(shellLayoutController.timelineRect, "width")
        height: rectValue(shellLayoutController.timelineRect, "height")
        visible: rectVisible(shellLayoutController.timelineRect)
        color: "#050608"
        border.width: 1
        border.color: "#151b23"
    }

    Rectangle {
        x: rectValue(shellLayoutController.clipEditorRect, "x")
        y: rectValue(shellLayoutController.clipEditorRect, "y")
        width: rectValue(shellLayoutController.clipEditorRect, "width")
        height: rectValue(shellLayoutController.clipEditorRect, "height")
        visible: rectVisible(shellLayoutController.clipEditorRect)
        color: "#07090c"
        border.width: 1
        border.color: "#151b23"
    }

    Rectangle {
        x: rectValue(shellLayoutController.mixRect, "x")
        y: rectValue(shellLayoutController.mixRect, "y")
        width: rectValue(shellLayoutController.mixRect, "width")
        height: rectValue(shellLayoutController.mixRect, "height")
        visible: rectVisible(shellLayoutController.mixRect)
        color: "#080b10"
        border.width: 1
        border.color: "#151b23"
    }

    Rectangle {
        x: rectValue(shellLayoutController.audioPoolRect, "x")
        y: rectValue(shellLayoutController.audioPoolRect, "y")
        width: rectValue(shellLayoutController.audioPoolRect, "width")
        height: rectValue(shellLayoutController.audioPoolRect, "height")
        visible: rectVisible(shellLayoutController.audioPoolRect)
        color: "#07090c"
        border.width: 1
        border.color: "#151b23"
    }

    Repeater {
        model: [
            { key: "timeline", rect: shellLayoutController.timelineHandleRect, cursor: Qt.SizeVerCursor },
            { key: "clipEditor", rect: shellLayoutController.clipEditorHandleRect, cursor: Qt.SizeVerCursor },
            { key: "mix", rect: shellLayoutController.mixHandleRect, cursor: Qt.SizeVerCursor },
            { key: "audioPool", rect: shellLayoutController.audioPoolHandleRect, cursor: Qt.SizeHorCursor }
        ]

        Item {
            required property var modelData

            x: rectValue(modelData.rect, "x")
            y: rectValue(modelData.rect, "y")
            width: rectValue(modelData.rect, "width")
            height: rectValue(modelData.rect, "height")
            visible: rectVisible(modelData.rect)

            Rectangle {
                anchors.fill: parent
                color: dragArea.pressed ? "#34404f" : (dragArea.containsMouse ? "#27313d" : "#161c24")
            }

            MouseArea {
                id: dragArea

                anchors.fill: parent
                hoverEnabled: true
                cursorShape: modelData.cursor
                acceptedButtons: Qt.LeftButton

                onPressed: function(mouse) {
                    shellLayoutController.beginResize(modelData.key, mouse.x + parent.x, mouse.y + parent.y)
                    mouse.accepted = true
                }

                onPositionChanged: function(mouse) {
                    if (pressed) {
                        shellLayoutController.updateResize(mouse.x + parent.x, mouse.y + parent.y)
                    }
                }

                onReleased: shellLayoutController.endResize()
                onCanceled: shellLayoutController.endResize()
            }
        }
    }
}
