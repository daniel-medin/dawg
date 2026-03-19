import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Window 2.15

Rectangle {
    id: root

    color: "#07090c"

    AppTheme {
        id: theme
    }

    property string activeMenuKind: ""
    property int activeRowIndex: -1
    property var activeMenuItems: []

    function closeMenu() {
        activeMenuKind = ""
        activeRowIndex = -1
        activeMenuItems = []
    }

    function openMenu(kind, rowIndex, items, anchorItem) {
        activeMenuKind = kind
        activeRowIndex = rowIndex
        activeMenuItems = items
        var targetParent = menuPopup.parent ? menuPopup.parent : root
        var local = anchorItem.mapToItem(targetParent, 0, anchorItem.height + 4)
        var popupWidth = Math.max(220, menuPopup.implicitWidth)
        var popupHeight = menuPopup.implicitHeight
        var maxX = Math.max(0, targetParent.width - popupWidth - 8)
        var maxY = Math.max(0, targetParent.height - popupHeight - 8)
        var desiredX = local.x

        if (desiredX + popupWidth > targetParent.width - 8) {
            desiredX = local.x + anchorItem.width - popupWidth
        }

        menuPopup.popupX = Math.max(8, Math.min(maxX, desiredX))
        menuPopup.popupY = Math.max(8, Math.min(maxY, local.y))
        menuPopup.open()
    }

    function triggerMenuAction(actionKey) {
        if (activeMenuKind === "header") {
            if (actionKey === "importAudio") {
                audioPoolController.importAudio()
            } else if (actionKey === "showLength") {
                audioPoolController.showLength = !audioPoolController.showLength
            } else if (actionKey === "showSize") {
                audioPoolController.showSize = !audioPoolController.showSize
            } else if (actionKey === "closePanel") {
                audioPoolController.closePanel()
            }
        } else if (activeMenuKind === "video") {
            if (actionKey === "toggleMute") {
                audioPoolController.toggleVideoAudioMuted()
            } else if (actionKey === "toggleFastPlayback") {
                audioPoolController.toggleFastPlayback()
            }
        } else if (activeMenuKind === "row") {
            if (actionKey === "addToFrame") {
                audioPoolController.itemDoubleActivated(activeRowIndex)
            } else if (actionKey === "deleteAudio") {
                audioPoolController.deleteAudio(activeRowIndex)
            } else if (actionKey === "deleteAudioAndNodes") {
                audioPoolController.deleteAudioAndNodes(activeRowIndex)
            }
        }

        closeMenu()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 8
            spacing: 8

            Label {
                text: "Audio Pool"
                color: theme.titleText
                font.pixelSize: 14
                font.weight: Font.DemiBold
            }

            Item {
                Layout.fillWidth: true
            }

            ToolButton {
                id: headerMenuButton

                text: "\u2630"
                hoverEnabled: true
                onClicked: {
                    root.openMenu("header", -1, [
                        { key: "importAudio", text: "Import Audio... (Ctrl+Shift+I)" },
                        { key: "showLength", text: "Show Length", checkable: true, checked: audioPoolController.showLength },
                        { key: "showSize", text: "Show Size", checkable: true, checked: audioPoolController.showSize },
                        { key: "closePanel", text: "Close (P)" }
                    ], headerMenuButton)
                }

                contentItem: Label {
                    text: headerMenuButton.text
                    color: theme.titleText
                    font.pixelSize: 13
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }

                background: Rectangle {
                    radius: 6
                    color: headerMenuButton.down
                        ? theme.pressedFill
                        : (headerMenuButton.hovered ? theme.hoverFill : "transparent")
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.leftMargin: 8
            Layout.rightMargin: 8
            Layout.bottomMargin: 8
            visible: audioPoolController.hasVideoAudio
            implicitHeight: 32
            radius: 6
            color: "#10151c"
            border.width: 1
            border.color: "#202834"

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 4
                spacing: 6

                Label {
                    text: "\u266B"
                    color: "#c7d0da"
                    font.pixelSize: 13
                }

                Label {
                    Layout.fillWidth: true
                    text: audioPoolController.videoAudioLabel
                    color: "#c7d0da"
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }

                ToolButton {
                    id: videoMenuButton

                    text: "\u2630"
                    hoverEnabled: true
                    ToolTip.visible: hovered
                    ToolTip.text: audioPoolController.videoAudioTooltip
                    onClicked: {
                        root.openMenu("video", -1, [
                            { key: "toggleMute", text: audioPoolController.videoAudioMuted ? "Unmute" : "Mute" },
                            { key: "toggleFastPlayback", text: "Fast Playback", checkable: true, checked: audioPoolController.fastPlaybackEnabled }
                        ], videoMenuButton)
                    }

                    contentItem: Label {
                        text: videoMenuButton.text
                        color: theme.titleText
                        font.pixelSize: 12
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    background: Rectangle {
                        radius: 6
                        color: videoMenuButton.down
                            ? theme.pressedFill
                            : (videoMenuButton.hovered ? theme.hoverFill : "transparent")
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "transparent"

            Label {
                anchors.centerIn: parent
                visible: audioPoolController.count === 0
                text: "No imported sounds yet."
                color: theme.subtitleText
                font.pixelSize: 13
            }

            ListView {
                id: listView

                anchors.fill: parent
                anchors.leftMargin: 4
                anchors.rightMargin: 4
                clip: true
                model: audioPoolController
                spacing: 1
                reuseItems: true

                delegate: Rectangle {
                    id: rowRoot

                    required property int index
                    required property string assetPath
                    required property string displayName
                    required property bool connected
                    required property bool isPlaying
                    required property string durationText
                    required property string sizeText
                    required property color statusColor
                    required property string connectionSummary

                    width: listView.width
                    height: 30
                    radius: 4
                    color: rowMouseArea.containsMouse ? Qt.rgba(1, 1, 1, 0.03) : "transparent"
                    Drag.active: dragHandler.active
                    Drag.dragType: Drag.Automatic
                    Drag.supportedActions: Qt.CopyAction
                    Drag.mimeData: ({
                        "application/x-dawg-audio-path": rowRoot.assetPath,
                        "text/plain": rowRoot.assetPath
                    })

                    ToolTip.visible: rowMouseArea.containsMouse
                    ToolTip.text: connectionSummary

                    MouseArea {
                        id: rowMouseArea

                        anchors.fill: parent
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        hoverEnabled: true
                        propagateComposedEvents: true
                        z: 0

                        onPressed: function(mouse) {
                            if (mouse.button === Qt.RightButton) {
                                clickTimer.stop()
                                root.openMenu("row", rowRoot.index, [
                                    { key: "addToFrame", text: "Add to Frame" },
                                    { key: "deleteAudio", text: "Delete Audio" },
                                    { key: "deleteAudioAndNodes", text: "Delete Audio + Nodes" }
                                ], rowMenuButton)
                                mouse.accepted = true
                                return
                            }

                            if ((mouse.modifiers & Qt.ControlModifier) !== 0) {
                                clickTimer.stop()
                                audioPoolController.startPreview(rowRoot.index)
                                mouse.accepted = true
                            }
                        }

                        onReleased: function(mouse) {
                            if ((mouse.modifiers & Qt.ControlModifier) !== 0 || mouse.button === Qt.LeftButton) {
                                audioPoolController.stopPreview()
                            }
                        }

                        onCanceled: audioPoolController.stopPreview()

                        onClicked: function(mouse) {
                            if (mouse.button === Qt.RightButton || (mouse.modifiers & Qt.ControlModifier) !== 0) {
                                return
                            }
                            clickTimer.restart()
                        }

                        onDoubleClicked: function(mouse) {
                            if ((mouse.modifiers & Qt.ControlModifier) !== 0) {
                                return
                            }
                            clickTimer.stop()
                            audioPoolController.itemDoubleActivated(rowRoot.index)
                        }

                        Timer {
                            id: clickTimer
                            interval: 180
                            repeat: false
                            onTriggered: audioPoolController.itemActivated(rowRoot.index)
                        }
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 4
                        spacing: 6
                        z: 1

                        Rectangle {
                            Layout.alignment: Qt.AlignVCenter
                            width: 8
                            height: 8
                            radius: 4
                            color: rowRoot.isPlaying ? "#47d86f" : rowRoot.statusColor
                            border.width: rowRoot.isPlaying ? 1 : 0
                            border.color: rowRoot.isPlaying ? "#aef2bf" : "transparent"
                        }

                        Label {
                            Layout.fillWidth: true
                            text: rowRoot.displayName
                            color: rowRoot.connected ? "#d8e0ea" : "#9ea9b7"
                            font.pixelSize: 12
                            elide: Text.ElideRight
                        }

                        Label {
                            visible: audioPoolController.showLength
                            text: rowRoot.durationText
                            color: "#91a0b1"
                            font.pixelSize: 11
                            horizontalAlignment: Text.AlignRight
                            Layout.preferredWidth: 56
                        }

                        Label {
                            visible: audioPoolController.showSize
                            text: rowRoot.sizeText
                            color: "#91a0b1"
                            font.pixelSize: 11
                            horizontalAlignment: Text.AlignRight
                            Layout.preferredWidth: 64
                        }

                        ToolButton {
                            id: rowMenuButton

                            text: "\u2630"
                            hoverEnabled: true
                            onClicked: {
                                clickTimer.stop()
                                root.openMenu("row", rowRoot.index, [
                                    { key: "addToFrame", text: "Add to Frame" },
                                    { key: "deleteAudio", text: "Delete Audio" },
                                    { key: "deleteAudioAndNodes", text: "Delete Audio + Nodes" }
                                ], rowMenuButton)
                            }

                            contentItem: Label {
                                text: rowMenuButton.text
                                color: theme.titleText
                                font.pixelSize: 12
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }

                            background: Rectangle {
                                radius: 6
                                color: rowMenuButton.down
                                    ? theme.pressedFill
                                    : (rowMenuButton.hovered ? theme.hoverFill : "transparent")
                            }
                        }
                    }

                    DragHandler {
                        id: dragHandler
                        target: null
                    }
                }

                ScrollBar.vertical: ScrollBar {
                    policy: ScrollBar.AsNeeded
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
        modal: false
        focus: true
        padding: 6
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        implicitWidth: Math.max(220, menuColumn.implicitWidth + leftPadding + rightPadding)
        implicitHeight: menuColumn.implicitHeight + topPadding + bottomPadding
        onClosed: root.closeMenu()

        background: Rectangle {
            radius: 10
            color: theme.menuBackground
            border.width: 0
        }

        contentItem: Column {
            id: menuColumn
            spacing: 0

            Repeater {
                model: root.activeMenuItems

                Item {
                    id: menuItemRoot

                    required property var modelData

                    implicitWidth: 220
                    implicitHeight: 32

                    Rectangle {
                        anchors.fill: parent
                        radius: 6
                        color: menuMouseArea.containsMouse
                            ? theme.menuItemHover
                            : "transparent"
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        spacing: 8

                        Label {
                            text: modelData.checkable ? (modelData.checked ? "\u2713" : "") : ""
                            color: theme.menuItemText
                            font.pixelSize: 12
                            Layout.preferredWidth: 14
                        }

                        Label {
                            Layout.fillWidth: true
                            text: modelData.text || ""
                            color: modelData.enabled === false ? theme.menuItemDisabled : theme.menuItemText
                            font.pixelSize: 12
                            elide: Text.ElideRight
                        }
                    }

                    MouseArea {
                        id: menuMouseArea

                        anchors.fill: parent
                        enabled: modelData.enabled !== false
                        hoverEnabled: enabled
                        onClicked: root.triggerMenuAction(modelData.key)
                    }
                }
            }
        }
    }
}
