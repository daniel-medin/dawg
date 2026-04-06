import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Window 2.15
import Dawg 1.0

Rectangle {
    id: root

    color: "#0d1117"
    property color menuPopupTextColor: "#eef2f6"
    property string activeMenuKind: ""
    property var activeMenuItems: []
    property var activeMenuAnchor: null

    AppTheme {
        id: theme
    }

    function closeMenu() {
        activeMenuKind = ""
        activeMenuItems = []
        activeMenuAnchor = null
    }

    function openMenu(kind, items, anchorItem) {
        if (menuPopup.opened && activeMenuKind === kind && activeMenuAnchor === anchorItem) {
            menuPopup.close()
            closeMenu()
            return
        }

        activeMenuKind = kind
        activeMenuItems = items
        activeMenuAnchor = anchorItem
        var targetParent = menuPopup.parent ? menuPopup.parent : root
        var local = anchorItem.mapToItem(targetParent, 0, anchorItem.height + 4)
        var popupWidth = Math.max(220, menuPopup.implicitWidth)
        var popupHeight = menuPopup.implicitHeight
        var maxX = Math.max(0, targetParent.width - popupWidth - 8)
        var maxY = Math.max(0, targetParent.height - popupHeight - 8)
        menuPopup.popupX = Math.max(8, Math.min(maxX, local.x))
        menuPopup.popupY = Math.max(8, Math.min(maxY, local.y))
        menuPopup.open()
    }

    function triggerMenuAction(actionKey) {
        var menuKind = activeMenuKind
        menuPopup.close()
        closeMenu()

        if (menuKind === "file") {
            mainWindowBridge.requestNodeEditorFileAction(actionKey)
        } else if (menuKind === "audio") {
            mainWindowBridge.requestNodeEditorAudioAction(actionKey)
        }
    }

    function menuItemEnabled(item) {
        if (!item)
            return false
        switch (item.key) {
        case "open":
            return nodeEditorController.canOpenNode
        case "save":
            return nodeEditorController.canSaveNode
        case "saveAs":
            return nodeEditorController.canSaveNodeAs
        case "export":
            return nodeEditorController.canExportNode
        case "import":
            return nodeEditorController.hasSelection
        default:
            return item.enabled !== false
        }
    }

    function pointInsideItem(item, targetParent, x, y) {
        if (!item || !targetParent)
            return false
        var topLeft = item.mapToItem(targetParent, 0, 0)
        return x >= topLeft.x
            && x <= topLeft.x + item.width
            && y >= topLeft.y
            && y <= topLeft.y + item.height
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 40
            color: "#10151c"
            border.width: 1
            border.color: "#1e2732"

            Row {
                anchors.left: parent.left
                anchors.leftMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                spacing: 4

                ToolButton {
                    id: fileMenuButton
                    text: "File"
                    hoverEnabled: true

                    onClicked: {
                        root.openMenu("file", [
                            { key: "open", text: "Open Node..." },
                            { key: "save", text: "Save Node" },
                            { key: "saveAs", text: "Save Node As..." },
                            { key: "export", text: "Export Node..." }
                        ], fileMenuButton)
                    }

                    contentItem: Label {
                        text: fileMenuButton.text
                        color: theme.titleText
                        font.pixelSize: 13
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    background: Rectangle {
                        radius: 6
                        color: fileMenuButton.down
                            ? theme.pressedFill
                            : (fileMenuButton.hovered ? theme.hoverFill : "transparent")
                    }
                }

                ToolButton {
                    id: audioMenuButton
                    text: "Audio"
                    hoverEnabled: true

                    onClicked: {
                        root.openMenu("audio", [
                            { key: "import", text: "Import Audio..." }
                        ], audioMenuButton)
                    }

                    contentItem: Label {
                        text: audioMenuButton.text
                        color: theme.titleText
                        font.pixelSize: 13
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    background: Rectangle {
                        radius: 6
                        color: audioMenuButton.down
                            ? theme.pressedFill
                            : (audioMenuButton.hovered ? theme.hoverFill : "transparent")
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                width: Math.max(120, parent.width - 220)
                text: nodeEditorController.selectedNodeName
                color: theme.titleText
                font.pixelSize: 14
                font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 12
            spacing: 8

            Label {
                Layout.fillWidth: true
                text: nodeEditorController.nodeContainerText
                color: "#7f8b99"
                font.pixelSize: 11
                elide: Text.ElideRight
            }

            Label {
                Layout.fillWidth: true
                text: nodeEditorController.audioSummaryText
                color: "#a8b3c0"
                font.pixelSize: 12
                elide: Text.ElideRight
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 112
                radius: 8
                color: "#121820"
                border.width: 1
                border.color: "#202936"

                ClipWaveformQuickItem {
                    id: nodeWaveform
                    objectName: "nodeEditorWaveform"
                    anchors.fill: parent
                    anchors.margins: 1
                    clipRangeHandlesVisible: false
                    visible: nodeEditorController.hasAttachedAudio
                }

                Column {
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 6
                    visible: !nodeEditorController.hasAttachedAudio

                    Text {
                        text: "Node Audio"
                        color: "#eef2f6"
                        font.pixelSize: 15
                        font.weight: Font.DemiBold
                    }

                    Text {
                        text: nodeEditorController.emptyBodyText
                        color: "#94a3b3"
                        font.pixelSize: 13
                        wrapMode: Text.WordWrap
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 120
                radius: 8
                color: "#111720"
                border.width: 1
                border.color: "#202936"

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 8

                    Text {
                        Layout.fillWidth: true
                        text: "Internal Tracks"
                        color: "#eef2f6"
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                    }

                    ScrollView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true

                        Column {
                            width: parent.width
                            spacing: 6

                            Repeater {
                                model: nodeEditorController.nodeTracks

                                delegate: Rectangle {
                                    required property var modelData
                                    width: parent.width
                                    height: 44
                                    radius: 6
                                    color: modelData.primary ? "#1a2430" : "#151c25"
                                    border.width: 1
                                    border.color: modelData.primary ? "#31455d" : "#263140"

                                    Column {
                                        anchors.fill: parent
                                        anchors.margins: 8
                                        spacing: 2

                                        Text {
                                            text: modelData.title
                                            color: "#eef2f6"
                                            font.pixelSize: 13
                                            font.weight: modelData.primary ? Font.DemiBold : Font.Medium
                                            elide: Text.ElideRight
                                        }

                                        Text {
                                            text: modelData.subtitle
                                            color: "#91a0b0"
                                            font.pixelSize: 11
                                            elide: Text.ElideRight
                                        }
                                    }
                                }
                            }

                            Text {
                                visible: nodeEditorController.nodeTrackCount === 0
                                width: parent.width
                                text: "No internal tracks yet."
                                color: "#94a3b3"
                                font.pixelSize: 12
                            }
                        }
                    }
                }
            }
        }
    }

    Popup {
        id: menuPopup

        property real popupX: 0
        property real popupY: 0

        parent: root.Window.window ? root.Window.window.contentItem : root
        x: popupX
        y: popupY
        z: 1001
        modal: false
        focus: true
        padding: 6
        closePolicy: Popup.CloseOnEscape
        implicitWidth: Math.max(220, menuColumn.implicitWidth + leftPadding + rightPadding)
        implicitHeight: menuColumn.implicitHeight + topPadding + bottomPadding

        onClosed: root.closeMenu()

        background: Rectangle {
            color: theme.menuBackground
            border.color: theme.menuBorder
            border.width: 1
            radius: 6
        }

        contentItem: Column {
            id: menuColumn
            spacing: 2

            Repeater {
                model: root.activeMenuItems

                delegate: Item {
                    required property var modelData
                    implicitWidth: 220
                    implicitHeight: 32

                    Rectangle {
                        anchors.fill: parent
                        radius: 6
                        color: hoverArea.containsMouse && root.menuItemEnabled(modelData)
                            ? theme.menuItemHover
                            : "transparent"
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        spacing: 8

                        Label {
                            Layout.fillWidth: true
                            text: modelData.text || ""
                            color: root.menuItemEnabled(modelData) ? root.menuPopupTextColor : theme.menuItemDisabled
                            font.pixelSize: 13
                            elide: Text.ElideRight
                        }
                    }

                    MouseArea {
                        id: hoverArea
                        anchors.fill: parent
                        enabled: root.menuItemEnabled(modelData)
                        hoverEnabled: enabled
                        onClicked: root.triggerMenuAction(modelData.key)
                    }
                }
            }
        }
    }

    MouseArea {
        visible: menuPopup.opened
        parent: root.Window.window ? root.Window.window.contentItem : root
        anchors.fill: parent
        z: 1000
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        hoverEnabled: false

        onPressed: function(mouse) {
            if (root.pointInsideItem(root.activeMenuAnchor, parent, mouse.x, mouse.y)) {
                mouse.accepted = false
                return
            }

            root.closeMenu()
            menuPopup.close()
            mouse.accepted = false
        }
    }
}
