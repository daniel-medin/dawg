import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Item {
    id: root
    visible: contextMenuController.visible
    focus: contextMenuController.visible
    implicitWidth: menuPanel.width
    implicitHeight: menuPanel.implicitHeight
    width: implicitWidth
    height: implicitHeight

    AppTheme {
        id: theme
    }

    Keys.onEscapePressed: contextMenuController.dismiss()

    Rectangle {
        id: menuPanel

        width: 220
        height: implicitHeight
        implicitHeight: contentColumn.implicitHeight + 16
        radius: 10
        color: "#14181f"
        border.width: 1
        border.color: "#324155"

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            onPressed: function(mouse) {
                mouse.accepted = true
            }
            onReleased: function(mouse) {
                mouse.accepted = true
            }
        }

        ColumnLayout {
            id: contentColumn

            anchors.fill: parent
            anchors.margins: 8
            spacing: 4

            Label {
                Layout.fillWidth: true
                visible: contextMenuController.title.length > 0
                text: contextMenuController.title
                color: theme.titleText
                font.pixelSize: 12
                font.weight: Font.DemiBold
                elide: Text.ElideRight
                leftPadding: 6
                rightPadding: 6
                topPadding: 4
                bottomPadding: 6
            }

            Repeater {
                model: contextMenuController.items

                delegate: Item {
                    id: menuItemRoot

                    required property var modelData

                    Layout.fillWidth: true
                    implicitHeight: modelData.separator ? 10 : 30

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.leftMargin: 6
                        anchors.rightMargin: 6
                        height: modelData.separator ? 1 : parent.height
                        radius: modelData.separator ? 0 : 6
                        color: modelData.separator
                            ? "#2a3340"
                            : (itemMouseArea.containsMouse ? "#223146" : "transparent")
                        visible: true

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 10
                            visible: !modelData.separator

                            Label {
                                text: modelData.checkable
                                    ? (modelData.checked ? "\u2713" : "")
                                    : ""
                                color: theme.titleText
                                font.pixelSize: 11
                                Layout.preferredWidth: 14
                            }

                            Label {
                                Layout.fillWidth: true
                                text: modelData.text
                                color: modelData.enabled ? "#eef3f8" : "#738396"
                                font.pixelSize: 12
                                elide: Text.ElideRight
                            }
                        }
                    }

                    MouseArea {
                        id: itemMouseArea

                        anchors.fill: parent
                        enabled: !modelData.separator && modelData.enabled
                        hoverEnabled: true
                        preventStealing: true
                        onPressed: function(mouse) {
                            mouse.accepted = true
                        }
                        onReleased: function(mouse) {
                            contextMenuController.triggerItem(modelData.key)
                            mouse.accepted = true
                        }
                    }
                }
            }
        }
    }
}
