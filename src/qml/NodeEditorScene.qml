import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import Dawg 1.0

Rectangle {
    id: root

    color: "#0d1117"
    property color menuPopupTextColor: "#eef2f6"
    property color menuPopupShortcutColor: "#95a4b5"
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
        menuPopup.popupX = Math.max(8, Math.min(targetParent.width - menuPopup.implicitWidth - 8, local.x))
        menuPopup.popupY = Math.max(8, Math.min(targetParent.height - menuPopup.implicitHeight - 8, local.y))
        menuPopup.open()
    }

    function triggerMenuAction(actionKey) {
        if (activeMenuKind === "file") {
            nodeEditorController.triggerFileAction(actionKey)
        } else if (activeMenuKind === "audio") {
            nodeEditorController.triggerAudioAction(actionKey)
        }
        closeMenu()
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
                    enabled: nodeEditorController.canOpenNode

                    onClicked: {
                        root.openMenu("file", [
                            { key: "open", text: "Open Node...", enabled: nodeEditorController.canOpenNode },
                            { key: "save", text: "Save Node", enabled: nodeEditorController.hasSelection },
                            { key: "export", text: "Export Node...", enabled: nodeEditorController.hasSelection }
                        ], fileMenuButton)
                    }

                    contentItem: Label {
                        text: fileMenuButton.text
                        color: fileMenuButton.enabled ? theme.titleText : theme.menuItemDisabled
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
                    enabled: nodeEditorController.hasSelection

                    onClicked: {
                        root.openMenu("audio", [
                            { key: "import", text: "Import Audio...", enabled: nodeEditorController.hasSelection }
                        ], audioMenuButton)
                    }

                    contentItem: Label {
                        text: audioMenuButton.text
                        color: audioMenuButton.enabled ? theme.titleText : theme.menuItemDisabled
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
        }
    }

    Popup {
        id: menuPopup

        property real popupX: 0
        property real popupY: 0

        parent: root
        x: popupX
        y: popupY
        z: 1001
        modal: false
        focus: true
        padding: 6
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside | Popup.CloseOnPressOutsideParent
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
                    implicitWidth: itemRow.implicitWidth + 20
                    implicitHeight: 32

                    Rectangle {
                        anchors.fill: parent
                        radius: 6
                        color: hoverArea.containsMouse && modelData.enabled !== false
                            ? theme.menuItemHover
                            : "transparent"
                    }

                    RowLayout {
                        id: itemRow
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        spacing: 10

                        Text {
                            text: modelData.checkable ? (modelData.checked ? "\u2713" : "") : ""
                            color: modelData.enabled === false ? theme.menuItemDisabled : root.menuPopupTextColor
                            font.pixelSize: 13
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            Layout.preferredWidth: 14
                        }

                        Text {
                            text: modelData.text
                            color: modelData.enabled === false ? theme.menuItemDisabled : root.menuPopupTextColor
                            font.pixelSize: 13
                            verticalAlignment: Text.AlignVCenter
                            Layout.fillWidth: true
                        }
                    }

                    MouseArea {
                        id: hoverArea
                        anchors.fill: parent
                        enabled: modelData.enabled !== false
                        hoverEnabled: enabled
                        onClicked: {
                            menuPopup.close()
                            root.triggerMenuAction(modelData.key)
                        }
                    }
                }
            }
        }
    }
}
