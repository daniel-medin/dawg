import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Window 2.15

Rectangle {
    id: root
    property bool windowActive: root.Window.window ? root.Window.window.active : windowChrome.active
    property real inactiveTitleOpacity: 0.56
    property real titleTextOpacity: root.windowActive ? 1.0 : inactiveTitleOpacity
    property color activeTitleColor: "#ffffff"
    property color inactiveTitleColor: "#d3d9e2"
    property color titleTextColor: root.windowActive ? activeTitleColor : inactiveTitleColor
    property color menuPopupTextColor: root.windowActive ? "#ffffff" : "#d3d9e2"
    property color menuPopupShortcutColor: root.windowActive ? "#98a5b5" : "#748192"

    color: theme.titleBarBackground
    border.width: 1
    border.color: theme.titleBarBorder
    implicitHeight: 44
    clip: false

    AppTheme {
        id: theme
    }

    MouseArea {
        id: dragArea
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        z: -1

        onPressed: function(mouse) {
            windowChrome.startSystemMove()
        }
    }

    function closeAllMenus() {
        for (var i = 0; i < menuRepeater.count; i++) {
            var item = menuRepeater.itemAt(i)
            if (item && item.menuRef) {
                item.menuRef.close()
            }
        }
    }

    function hasOpenMenu() {
        for (var i = 0; i < menuRepeater.count; i++) {
            var item = menuRepeater.itemAt(i)
            if (item && item.menuRef && item.menuRef.opened) {
                return true
            }
        }
        return false
    }

    function globalPoint(localX, localY) {
        var mapped = root.mapToGlobal(Qt.point(localX, localY))
        return mapped ? mapped : Qt.point(0, 0)
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

    Label {
        id: centeredProjectTitle
        anchors.verticalCenter: parent.verticalCenter
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.max(120, root.width - 560)
        text: windowChrome.projectTitle
        visible: text.length > 0
        color: root.titleTextColor
        opacity: root.titleTextOpacity
        font.pixelSize: 14
        font.weight: Font.DemiBold
        horizontalAlignment: Text.AlignHCenter
        elide: Text.ElideRight
        z: 1
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 0
        spacing: 8

        Image {
            source: windowChrome.iconSource
            sourceSize.width: 18
            sourceSize.height: 18
            Layout.alignment: Qt.AlignVCenter
            fillMode: Image.PreserveAspectFit
        }

        Label {
            text: windowChrome.appTitle
            color: root.titleTextColor
            opacity: root.titleTextOpacity
            font.pixelSize: 14
            font.weight: Font.DemiBold
            elide: Text.ElideRight
            Layout.preferredWidth: 90
            Layout.maximumWidth: 120
            Layout.alignment: Qt.AlignVCenter
        }

        Repeater {
            id: menuRepeater
            model: actionRegistry.menus

            delegate: Item {
                id: menuRoot
                required property var modelData
                property alias menuRef: menuPopup
                property bool actionTriggerInProgress: false

                width: menuButton.implicitWidth
                height: root.height

                ToolButton {
                    id: menuButton

                    anchors.verticalCenter: parent.verticalCenter
                    text: menuRoot.modelData.title
                    hoverEnabled: true
                    flat: true
                    implicitWidth: contentItem.implicitWidth + 20
                    implicitHeight: 32

                    contentItem: Label {
                        text: menuButton.text
                        color: root.titleTextColor
                        opacity: root.titleTextOpacity
                        font.pixelSize: 13
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    background: Rectangle {
                        radius: 6
                        color: menuButton.down ? theme.pressedFill : (menuButton.hovered ? theme.hoverFill : "transparent")
                    }

                    function openCurrentMenu() {
                        var targetParent = menuPopup.parent ? menuPopup.parent : root
                        var local = menuButton.mapToItem(targetParent, 0, menuButton.height + 4)
                        menuPopup.popupX = local.x
                        menuPopup.popupY = local.y
                        menuPopup.open()
                    }

                    onClicked: {
                        var wasOpen = menuPopup.opened
                        root.closeAllMenus()
                        if (!wasOpen) {
                            openCurrentMenu()
                        }
                    }

                    onHoveredChanged: {
                        if (menuButton.hovered && !menuPopup.opened && root.hasOpenMenu()) {
                            root.closeAllMenus()
                            openCurrentMenu()
                        }
                    }
                }

                Popup {
                    id: menuPopup

                    property real popupX: 0
                    property real popupY: 0
                    property var activeSubmenuEntry: null
                    property var activeSubmenuAnchor: null
                    property real submenuPopupX: 0
                    property real submenuPopupY: 0

                    function closeSubmenu() {
                        activeSubmenuEntry = null
                        activeSubmenuAnchor = null
                        submenuPopup.close()
                    }

                    function openSubmenu(anchorItem, entry) {
                        if (!anchorItem || !entry || !entry.hasSubmenu) {
                            closeSubmenu()
                            return
                        }

                        activeSubmenuEntry = entry
                        activeSubmenuAnchor = anchorItem

                        var targetParent = submenuPopup.parent ? submenuPopup.parent : root
                        var local = anchorItem.mapToItem(targetParent, anchorItem.width + 4, -6)
                        var popupWidth = Math.max(240, submenuColumn.implicitWidth + submenuPopup.leftPadding + submenuPopup.rightPadding)
                        var popupHeight = submenuColumn.implicitHeight + submenuPopup.topPadding + submenuPopup.bottomPadding
                        var desiredX = local.x
                        var desiredY = local.y
                        var maxX = Math.max(8, targetParent.width - popupWidth - 8)
                        var maxY = Math.max(8, targetParent.height - popupHeight - 8)

                        if (desiredX + popupWidth > targetParent.width - 8) {
                            desiredX = anchorItem.mapToItem(targetParent, -popupWidth - 4, -6).x
                        }

                        submenuPopupX = Math.max(8, Math.min(maxX, desiredX))
                        submenuPopupY = Math.max(8, Math.min(maxY, desiredY))
                        submenuPopup.open()
                    }

                    parent: root.Window.window ? root.Window.window.contentItem : root
                    x: popupX
                    y: popupY
                    z: 1001
                    modal: false
                    focus: true
                    padding: 6
                    closePolicy: Popup.CloseOnEscape
                    implicitWidth: Math.max(240, menuColumn.implicitWidth + leftPadding + rightPadding)
                    implicitHeight: menuColumn.implicitHeight + topPadding + bottomPadding
                    onClosed: closeSubmenu()

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
                            model: menuRoot.modelData.items

                            delegate: Item {
                                id: menuItemRoot
                                required property var modelData
                                implicitWidth: itemRow.implicitWidth + 20
                                implicitHeight: modelData.separator ? 10 : 32

                                Rectangle {
                                    anchors.fill: parent
                                    radius: modelData.separator ? 0 : 6
                                    color: hoverArea.containsMouse && !modelData.separator && modelData.enabled
                                        ? theme.menuItemHover
                                        : "transparent"
                                }

                                Rectangle {
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.verticalCenter: parent.verticalCenter
                                    anchors.leftMargin: 8
                                    anchors.rightMargin: 8
                                    height: modelData.separator ? 1 : 0
                                    visible: modelData.separator
                                    color: theme.menuBorder
                                }

                                RowLayout {
                                    id: itemRow
                                    anchors.fill: parent
                                    anchors.leftMargin: 10
                                    anchors.rightMargin: 10
                                    spacing: 10
                                    visible: !modelData.separator

                                    Text {
                                        text: modelData.checkable
                                            ? (modelData.checked ? "\u2713" : "")
                                            : ""
                                        color: modelData.enabled ? root.menuPopupTextColor : theme.menuItemDisabled
                                        font.pixelSize: 13
                                        horizontalAlignment: Text.AlignHCenter
                                        verticalAlignment: Text.AlignVCenter
                                        Layout.preferredWidth: 14
                                    }

                                    Text {
                                        text: modelData.text
                                        color: modelData.enabled ? root.menuPopupTextColor : theme.menuItemDisabled
                                        font.pixelSize: 13
                                        verticalAlignment: Text.AlignVCenter
                                        Layout.fillWidth: true
                                    }

                                    Text {
                                        text: modelData.shortcut
                                        visible: !modelData.hasSubmenu && modelData.shortcut.length > 0
                                        color: root.menuPopupShortcutColor
                                        font.pixelSize: 12
                                        verticalAlignment: Text.AlignVCenter
                                    }

                                    Text {
                                        text: ">"
                                        visible: modelData.hasSubmenu
                                        color: modelData.enabled ? root.menuPopupShortcutColor : theme.menuItemDisabled
                                        font.pixelSize: 12
                                        verticalAlignment: Text.AlignVCenter
                                    }
                                }

                                MouseArea {
                                    id: hoverArea
                                    anchors.fill: parent
                                    enabled: !modelData.separator && modelData.enabled
                                    hoverEnabled: true

                                    onEntered: {
                                        if (modelData.hasSubmenu) {
                                            menuPopup.openSubmenu(menuItemRoot, modelData)
                                        } else {
                                            menuPopup.closeSubmenu()
                                        }
                                    }

                                    onClicked: {
                                        if (modelData.hasSubmenu) {
                                            menuPopup.openSubmenu(menuItemRoot, modelData)
                                            return
                                        }
                                        var entry = modelData
                                        menuRoot.actionTriggerInProgress = true
                                        submenuPopup.close()
                                        menuPopup.close()
                                        entry.trigger()
                                        menuRoot.actionTriggerInProgress = false
                                    }
                                }
                            }
                        }
                    }

                    Popup {
                        id: submenuPopup

                        parent: root.Window.window ? root.Window.window.contentItem : root
                        x: menuPopup.submenuPopupX
                        y: menuPopup.submenuPopupY
                        z: 1002
                        modal: false
                        focus: true
                        padding: 6
                        closePolicy: Popup.CloseOnEscape
                        implicitWidth: Math.max(240, submenuColumn.implicitWidth + leftPadding + rightPadding)
                        implicitHeight: submenuColumn.implicitHeight + topPadding + bottomPadding

                        background: Rectangle {
                            color: theme.menuBackground
                            border.color: theme.menuBorder
                            border.width: 1
                            radius: 6
                        }

                        contentItem: Column {
                            id: submenuColumn
                            spacing: 2

                            Repeater {
                                model: menuPopup.activeSubmenuEntry ? menuPopup.activeSubmenuEntry.subItems : []

                                delegate: Item {
                                    required property var modelData
                                    implicitWidth: submenuItemRow.implicitWidth + 20
                                    implicitHeight: modelData.separator ? 10 : 32

                                    Rectangle {
                                        anchors.fill: parent
                                        radius: modelData.separator ? 0 : 6
                                        color: submenuHoverArea.containsMouse && !modelData.separator && modelData.enabled
                                            ? theme.menuItemHover
                                            : "transparent"
                                    }

                                    Rectangle {
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.verticalCenter: parent.verticalCenter
                                        anchors.leftMargin: 8
                                        anchors.rightMargin: 8
                                        height: modelData.separator ? 1 : 0
                                        visible: modelData.separator
                                        color: theme.menuBorder
                                    }

                                    RowLayout {
                                        id: submenuItemRow
                                        anchors.fill: parent
                                        anchors.leftMargin: 10
                                        anchors.rightMargin: 10
                                        spacing: 10
                                        visible: !modelData.separator

                                        Text {
                                            text: modelData.checkable
                                                ? (modelData.checked ? "\u2713" : "")
                                                : ""
                                            color: modelData.enabled ? root.menuPopupTextColor : theme.menuItemDisabled
                                            font.pixelSize: 13
                                            horizontalAlignment: Text.AlignHCenter
                                            verticalAlignment: Text.AlignVCenter
                                            Layout.preferredWidth: 14
                                        }

                                        Text {
                                            text: modelData.text
                                            color: modelData.enabled ? root.menuPopupTextColor : theme.menuItemDisabled
                                            font.pixelSize: 13
                                            verticalAlignment: Text.AlignVCenter
                                            Layout.fillWidth: true
                                        }

                                        Text {
                                            text: modelData.shortcut
                                            visible: modelData.shortcut.length > 0
                                            color: root.menuPopupShortcutColor
                                            font.pixelSize: 12
                                            verticalAlignment: Text.AlignVCenter
                                        }
                                    }

                                    MouseArea {
                                        id: submenuHoverArea
                                        anchors.fill: parent
                                        enabled: !modelData.separator && modelData.enabled
                                        hoverEnabled: true

                                        onClicked: {
                                            var entry = modelData
                                            menuRoot.actionTriggerInProgress = true
                                            submenuPopup.close()
                                            menuPopup.close()
                                            entry.trigger()
                                            menuRoot.actionTriggerInProgress = false
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                MouseArea {
                    visible: menuPopup.opened && !menuRoot.actionTriggerInProgress
                    parent: root.Window.window ? root.Window.window.contentItem : root
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    y: root.y + root.height
                    height: Math.max(0, parent.height - y)
                    z: 1000
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    hoverEnabled: false

                    onPressed: function(mouse) {
                        if (root.pointInsideItem(menuPopup, parent, mouse.x, mouse.y)
                                || root.pointInsideItem(submenuPopup, parent, mouse.x, mouse.y)) {
                            mouse.accepted = false
                            return
                        }

                        root.closeAllMenus()
                        mouse.accepted = false
                    }
                }
            }
        }

        Label {
            text: windowChrome.frameText
            color: theme.subtitleText
            font.pixelSize: 12
            horizontalAlignment: Text.AlignRight
            elide: Text.ElideLeft
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
        }

        Item {
            Layout.fillHeight: true
            Layout.preferredWidth: 2

            Rectangle {
                anchors.fill: parent
                color: resizeArea.pressed
                    ? theme.resizeHandlePressed
                    : (resizeArea.containsMouse ? theme.resizeHandleHover : "transparent")
            }

            MouseArea {
                id: resizeArea
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton
                hoverEnabled: true
                onPressed: function(mouse) {
                    windowChrome.startSystemResize(Qt.RightEdge)
                }
            }
        }

        Item {
            Layout.fillHeight: true
            Layout.preferredWidth: 80

            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                hoverEnabled: true
                onPressed: function(mouse) {
                    var gp = root.globalPoint(mouse.x, mouse.y)
                    if (mouse.button === Qt.RightButton) {
                        windowChrome.showSystemMenu(gp.x, gp.y)
                    } else {
                        windowChrome.startSystemMove()
                    }
                }
                onDoubleClicked: function(mouse) {
                    if (mouse.button === Qt.LeftButton) {
                        windowChrome.toggleMaximized()
                    }
                }
            }
        }

        Row {
            spacing: 0
            Layout.alignment: Qt.AlignRight | Qt.AlignVCenter

            ToolButton {
                id: minimizeButton
                text: "\u2013"
                hoverEnabled: true
                onClicked: windowChrome.minimize()
                contentItem: Label {
                    text: minimizeButton.text
                    color: theme.titleText
                    font.pixelSize: 15
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    color: minimizeButton.down ? theme.pressedFill : (minimizeButton.hovered ? theme.hoverFill : "transparent")
                }
                width: 46
                height: root.height
            }

            ToolButton {
                id: maximizeButton
                text: windowChrome.maximized ? "\u2752" : "\u2610"
                hoverEnabled: true
                onClicked: windowChrome.toggleMaximized()
                contentItem: Label {
                    text: maximizeButton.text
                    color: theme.titleText
                    font.pixelSize: 12
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    color: maximizeButton.down ? theme.pressedFill : (maximizeButton.hovered ? theme.hoverFill : "transparent")
                }
                width: 46
                height: root.height
            }

            ToolButton {
                id: closeButton
                text: "\u2715"
                hoverEnabled: true
                onClicked: windowChrome.closeWindow()
                contentItem: Label {
                    text: closeButton.text
                    color: theme.titleText
                    font.pixelSize: 12
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    color: closeButton.down ? theme.closePressedFill : (closeButton.hovered ? theme.closeHoverFill : "transparent")
                }
                width: 46
                height: root.height
            }
        }
    }
}
