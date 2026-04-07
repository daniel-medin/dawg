import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Window 2.15
import Dawg 1.0

Rectangle {
    id: root

    color: "#0d1117"
    property int laneHeaderWidth: 148
    property color menuPopupTextColor: "#eef2f6"
    property string activeMenuKind: ""
    property var activeMenuItems: []
    property var activeMenuAnchor: null
    property bool editMarkerBlinkOn: true

    function timelineContentWidth(viewportWidth) {
        return Math.max(1, viewportWidth)
    }

    AppTheme {
        id: theme
    }

    Timer {
        interval: 500
        repeat: true
        running: nodeEditorController.showTimeline
            && nodeEditorController.nodeTrackCount > 0
        onTriggered: root.editMarkerBlinkOn = !root.editMarkerBlinkOn
        onRunningChanged: {
            if (running)
                root.editMarkerBlinkOn = true
        }
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
        } else if (menuKind === "audio" || menuKind === "track") {
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
        case "createTrack":
        case "newLane":
            return nodeEditorController.hasSelection
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
                    id: trackMenuButton
                    text: "Track"
                    hoverEnabled: true

                    onClicked: {
                        root.openMenu("track", [
                            { key: "createTrack", text: "Create Track" }
                        ], trackMenuButton)
                    }

                    contentItem: Label {
                        text: trackMenuButton.text
                        color: theme.titleText
                        font.pixelSize: 13
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    background: Rectangle {
                        radius: 6
                        color: trackMenuButton.down
                            ? theme.pressedFill
                            : (trackMenuButton.hovered ? theme.hoverFill : "transparent")
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
                width: Math.max(120, parent.width - 300)
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
            spacing: 0

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 120
                color: "#0b1016"

                ColumnLayout {
                    id: lanesPanelColumn
                    anchors.fill: parent
                    spacing: 0

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 28
                        color: "#101720"

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: root.laneHeaderWidth + 10
                            anchors.rightMargin: 10
                            spacing: 12

                            Text {
                                Layout.alignment: Qt.AlignVCenter
                                text: nodeEditorController.timelineStartText
                                color: "#7f8b99"
                                font.pixelSize: 11
                            }

                            Item {
                                Layout.fillWidth: true
                            }

                            Text {
                                Layout.alignment: Qt.AlignVCenter
                                text: nodeEditorController.playheadText
                                color: "#9ec7f0"
                                font.pixelSize: 11
                                font.weight: Font.DemiBold
                            }

                            Item {
                                Layout.fillWidth: true
                            }

                            Text {
                                Layout.alignment: Qt.AlignVCenter
                                text: nodeEditorController.timelineEndText
                                color: "#7f8b99"
                                font.pixelSize: 11
                            }
                        }

                        MouseArea {
                            x: root.laneHeaderWidth
                            y: 0
                            width: Math.max(1, parent.width - root.laneHeaderWidth)
                            height: parent.height
                            enabled: nodeEditorController.showTimeline
                            onClicked: function(mouse) {
                                root.forceActiveFocus()
                                nodeEditorController.setPlayheadFromRatio(mouse.x / Math.max(1, width))
                            }
                        }
                    }

                    ScrollView {
                        id: lanesScroll
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        contentWidth: root.timelineContentWidth(width)
                        contentHeight: lanesContent.height
                        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                        ScrollBar.vertical.policy: ScrollBar.AsNeeded

                        Item {
                            id: lanesContent
                            width: lanesScroll.contentWidth
                            height: lanesColumn.implicitHeight

                            Column {
                                id: lanesColumn
                                width: parent.width
                                spacing: 1

                                Repeater {
                                    model: nodeEditorController.nodeTracks

                                    delegate: Rectangle {
                                        id: laneDelegate
                                        required property var modelData
                                        width: parent.width
                                        height: 72
                                        color: modelData.primary ? "#17202a" : "#121922"

                                        Row {
                                            anchors.fill: parent
                                            spacing: 0

                                            Rectangle {
                                                width: root.laneHeaderWidth
                                                height: parent.height
                                                color: modelData.primary ? "#1b2632" : "#151e28"
                                                border.width: modelData.laneId === nodeEditorController.selectedLaneHeaderId ? 1 : 0
                                                border.color: "#9ec7f0"

                                                Text {
                                                    anchors.left: parent.left
                                                    anchors.right: parent.right
                                                    anchors.top: parent.top
                                                    anchors.leftMargin: 10
                                                    anchors.rightMargin: 42
                                                    anchors.topMargin: 12
                                                    text: modelData.title
                                                    color: "#eef2f6"
                                                    font.pixelSize: 13
                                                    font.weight: modelData.primary ? Font.DemiBold : Font.Medium
                                                    elide: Text.ElideRight
                                                }

                                                Text {
                                                    anchors.left: parent.left
                                                    anchors.right: parent.right
                                                    anchors.top: parent.top
                                                    anchors.leftMargin: 10
                                                    anchors.rightMargin: 42
                                                    anchors.topMargin: 34
                                                    text: modelData.subtitle
                                                    color: "#91a0b0"
                                                    font.pixelSize: 11
                                                    elide: Text.ElideRight
                                                }

                                                Column {
                                                    anchors.right: parent.right
                                                    anchors.rightMargin: 8
                                                    anchors.verticalCenter: parent.verticalCenter
                                                    spacing: 4
                                                    z: 10

                                                    Rectangle {
                                                        id: laneSoloButton
                                                        width: 22
                                                        height: 18
                                                        radius: 4
                                                        color: modelData.soloed ? "#db7e26" : "#1b2129"
                                                        border.width: 1
                                                        border.color: modelData.soloed ? "#ffd7b4" : "#2c3540"

                                                        Text {
                                                            anchors.centerIn: parent
                                                            text: "S"
                                                            color: modelData.soloed ? "#fff4e9" : "#d6dfe9"
                                                            font.pixelSize: 10
                                                            font.bold: true
                                                        }

                                                        MouseArea {
                                                            anchors.fill: parent
                                                            preventStealing: true
                                                            cursorShape: Qt.PointingHandCursor
                                                            onClicked: function(mouse) {
                                                                root.forceActiveFocus()
                                                                nodeEditorController.setLaneSoloed(modelData.laneId, !modelData.soloed)
                                                                mouse.accepted = true
                                                            }
                                                        }
                                                    }

                                                    Rectangle {
                                                        id: laneMuteButton
                                                        width: 22
                                                        height: 18
                                                        radius: 4
                                                        color: modelData.muted ? "#2f3741" : "#1b2129"
                                                        border.width: 1
                                                        border.color: modelData.muted ? "#4b5661" : "#2c3540"

                                                        Text {
                                                            anchors.centerIn: parent
                                                            text: "M"
                                                            color: modelData.muted ? "#9ba6b1" : "#d6dfe9"
                                                            font.pixelSize: 10
                                                            font.bold: true
                                                        }

                                                        MouseArea {
                                                            anchors.fill: parent
                                                            preventStealing: true
                                                            cursorShape: Qt.PointingHandCursor
                                                            onClicked: function(mouse) {
                                                                root.forceActiveFocus()
                                                                nodeEditorController.setLaneMuted(modelData.laneId, !modelData.muted)
                                                                mouse.accepted = true
                                                            }
                                                        }
                                                    }
                                                }

                                                MouseArea {
                                                    anchors.fill: parent
                                                    z: 5
                                                    acceptedButtons: Qt.LeftButton
                                                    onClicked: {
                                                        root.forceActiveFocus()
                                                        nodeEditorController.selectLaneHeader(modelData.laneId)
                                                    }
                                                }
                                            }

                                            Rectangle {
                                                id: laneTimeline
                                                width: parent.width - root.laneHeaderWidth
                                                height: parent.height
                                                color: "#0f151d"
                                                clip: true

                                                Rectangle {
                                                    x: 0
                                                    width: parent.width
                                                    height: 1
                                                    anchors.verticalCenter: parent.verticalCenter
                                                    color: "#273241"
                                                }

                                                Repeater {
                                                    model: 5

                                                    delegate: Rectangle {
                                                        width: 1
                                                        height: laneTimeline.height
                                                        x: Math.round((laneTimeline.width - width) * (index / 4))
                                                        color: "#1a2430"
                                                    }
                                                }

                                                Repeater {
                                                    model: modelData.clips || []

                                                    delegate: ClipWaveformQuickItem {
                                                        id: clipDelegate
                                                        required property var modelData
                                                        z: 10
                                                        x: Math.round(laneTimeline.width * (modelData.clipOffsetRatio || 0))
                                                        y: 0
                                                        width: Math.max(72, Math.round(laneTimeline.width * (modelData.clipWidthRatio || 0.08)))
                                                        height: laneTimeline.height
                                                        clip: true
                                                        clipRangeHandlesVisible: false
                                                        playheadVisible: false
                                                        contentMargin: 0
                                                        textureSize: Qt.size(Math.max(1, width), Math.max(1, height))
                                                        visible: modelData.hasWaveform && width > 1 && height > 1
                                                        waveformState: modelData.waveformState

                                                        Rectangle {
                                                            anchors.fill: parent
                                                            anchors.margins: 0
                                                            color: "transparent"
                                                            border.width: clipDelegate.modelData.clipId === nodeEditorController.selectedClipId ? 1 : 0
                                                            border.color: "#9ec7f0"
                                                            z: 2
                                                        }

                                                        Text {
                                                            anchors.left: parent.left
                                                            anchors.right: parent.right
                                                            anchors.top: parent.top
                                                            anchors.leftMargin: 8
                                                            anchors.rightMargin: 8
                                                            anchors.topMargin: 4
                                                            text: clipDelegate.modelData.title
                                                            color: "#eef2f6"
                                                            font.pixelSize: 11
                                                            font.weight: Font.Medium
                                                            elide: Text.ElideRight
                                                            z: 3
                                                        }

                                                        MouseArea {
                                                            anchors.fill: parent
                                                            acceptedButtons: Qt.LeftButton
                                                            onClicked: function(mouse) {
                                                                root.forceActiveFocus()
                                                                nodeEditorController.selectLane(laneDelegate.modelData.laneId)
                                                                nodeEditorController.setPlayheadFromRatio((clipDelegate.x + mouse.x) / Math.max(1, laneTimeline.width))
                                                            }
                                                            onDoubleClicked: {
                                                                root.forceActiveFocus()
                                                                nodeEditorController.selectClip(laneDelegate.modelData.laneId, clipDelegate.modelData.clipId)
                                                            }
                                                        }
                                                    }
                                                }

                                                MouseArea {
                                                    anchors.fill: parent
                                                    z: 1
                                                    onClicked: function(mouse) {
                                                        root.forceActiveFocus()
                                                        nodeEditorController.selectLane(modelData.laneId)
                                                        nodeEditorController.setPlayheadFromRatio(mouse.x / Math.max(1, width))
                                                    }
                                                }

                                                Rectangle {
                                                    visible: nodeEditorController.showTimeline
                                                        && root.editMarkerBlinkOn
                                                        && nodeEditorController.insertionMarkerStationary
                                                        && modelData.laneId === nodeEditorController.selectedLaneId
                                                    x: laneTimeline.width * nodeEditorController.insertionMarkerRatio
                                                    y: 0
                                                    width: 1
                                                    height: laneTimeline.height
                                                    color: "#9ec7f0"
                                                    z: 20
                                                }
                                            }
                                        }
                                    }
                                }

                                Text {
                                    visible: nodeEditorController.nodeTrackCount === 0
                                    width: parent.width
                                    text: "No lanes yet."
                                    color: "#94a3b3"
                                    font.pixelSize: 12
                                }
                            }

                            Rectangle {
                                visible: nodeEditorController.showTimeline
                                    && nodeEditorController.nodeTrackCount > 0
                                    && nodeEditorController.playbackActive
                                x: root.laneHeaderWidth + ((parent.width - root.laneHeaderWidth) * nodeEditorController.playheadRatio)
                                y: 0
                                width: 1
                                height: parent.height
                                color: "#9ec7f0"
                                z: 20
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
