import QtQuick 2.15

Item {
    id: root
    objectName: "shellRoot"
    focus: true

    AppTheme {
        id: theme
    }

    function rectValue(mapValue, key) {
        return mapValue && mapValue[key] !== undefined ? mapValue[key] : 0
    }

    function rectVisible(mapValue) {
        return mapValue && mapValue.visible === true
    }

    Rectangle {
        anchors.fill: parent
        color: theme.shellBackground
    }

    QuickTitleBar {
        id: titleBar
        objectName: "quickTitleBar"
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 44
    }

    Item {
        id: contentArea
        objectName: "shellContentArea"
        anchors.top: titleBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        clip: true

        ShellLayoutScene {
            id: shellLayoutBackground
            objectName: "shellLayoutBackground"
            anchors.fill: parent
            z: 0
        }

        VideoViewportScene {
            id: videoViewportScene
            objectName: "videoViewportScene"
            x: rectValue(shellLayoutController.canvasRect, "x")
            y: rectValue(shellLayoutController.canvasRect, "y")
            width: rectValue(shellLayoutController.canvasRect, "width")
            height: rectValue(shellLayoutController.canvasRect, "height")
            visible: rectVisible(shellLayoutController.canvasRect)
            z: 1
        }

        TimelineScene {
            id: timelineScene
            objectName: "timelineScene"
            x: rectValue(shellLayoutController.timelineRect, "x")
            y: rectValue(shellLayoutController.timelineRect, "y")
            width: rectValue(shellLayoutController.timelineRect, "width")
            height: rectValue(shellLayoutController.timelineRect, "height")
            visible: rectVisible(shellLayoutController.timelineRect)
            z: 1
        }

        ClipEditorScene {
            id: clipEditorScene
            objectName: "clipEditorScene"
            x: rectValue(shellLayoutController.clipEditorRect, "x")
            y: rectValue(shellLayoutController.clipEditorRect, "y")
            width: rectValue(shellLayoutController.clipEditorRect, "width")
            height: rectValue(shellLayoutController.clipEditorRect, "height")
            visible: rectVisible(shellLayoutController.clipEditorRect)
            z: 1
        }

        MixScene {
            id: mixScene
            objectName: "mixScene"
            x: rectValue(shellLayoutController.mixRect, "x")
            y: rectValue(shellLayoutController.mixRect, "y")
            width: rectValue(shellLayoutController.mixRect, "width")
            height: rectValue(shellLayoutController.mixRect, "height")
            visible: rectVisible(shellLayoutController.mixRect)
            z: 1
        }

        AudioPoolScene {
            id: audioPoolScene
            objectName: "audioPoolScene"
            x: rectValue(shellLayoutController.audioPoolRect, "x")
            y: rectValue(shellLayoutController.audioPoolRect, "y")
            width: rectValue(shellLayoutController.audioPoolRect, "width")
            height: rectValue(shellLayoutController.audioPoolRect, "height")
            visible: rectVisible(shellLayoutController.audioPoolRect)
            z: 1
        }

        ShellLayoutScene {
            id: shellLayoutHandles
            objectName: "shellLayoutScene"
            anchors.fill: parent
            handlesOnly: true
            z: 10
        }

        ShellOverlay {
            id: shellOverlayScene
            objectName: "shellOverlayScene"
            anchors.fill: parent
            z: 20
        }

        MouseArea {
            anchors.fill: parent
            visible: titleBar.menusOpen
            z: 39
            acceptedButtons: Qt.LeftButton | Qt.RightButton

            onPressed: function(mouse) {
                titleBar.closeAllMenus()
                mouse.accepted = true
            }
        }

        Item {
            id: contextMenuHost
            objectName: "contextMenuHost"
            anchors.fill: parent
            visible: contextMenuController.visible
            z: 40

            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                onPressed: contextMenuController.dismiss()
            }

            ContextMenuOverlay {
                id: contextMenuOverlay
                objectName: "contextMenuOverlay"
                x: contextMenuController.menuX
                y: contextMenuController.menuY
            }
        }

        Item {
            id: dialogHost
            objectName: "dialogHost"
            anchors.fill: parent
            visible: dialogController.visible
            z: 50

            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton | Qt.RightButton
            }

            DialogOverlay {
                id: dialogOverlay
                objectName: "dialogOverlay"
                anchors.centerIn: parent
            }
        }

        Item {
            id: filePickerHost
            objectName: "filePickerHost"
            anchors.fill: parent
            visible: filePickerController.visible
            z: 60

            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton | Qt.RightButton
            }

            FilePickerOverlay {
                id: filePickerOverlay
                objectName: "filePickerOverlay"
                anchors.centerIn: parent
            }
        }
    }
}
