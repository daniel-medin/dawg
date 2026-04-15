import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Item {
    id: root
    visible: filePickerController.visible
    implicitWidth: 840
    implicitHeight: 580
    clip: true

    readonly property real overlayMargin: 16
    readonly property real availableWidth: parent
        ? Math.max(420, parent.width - overlayMargin * 2)
        : implicitWidth
    readonly property real availableHeight: parent
        ? Math.max(320, parent.height - overlayMargin * 2)
        : implicitHeight

    width: Math.min(implicitWidth, availableWidth)
    height: Math.min(implicitHeight, availableHeight)
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: overlayMargin

    AppTheme {
        id: theme
    }

    onVisibleChanged: {
        if (!visible) {
            return
        }

        if (filePickerController.saveMode) {
            fileNameField.forceActiveFocus()
            fileNameField.selectAll()
        } else {
            pathField.forceActiveFocus()
            pathField.selectAll()
        }
    }

    Keys.onEscapePressed: filePickerController.cancel()

    Rectangle {
        anchors.fill: parent
        radius: 14
        color: "#14181f"
        border.width: 1
        border.color: "#2b3442"

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 14
            spacing: 10

            Label {
                Layout.fillWidth: true
                text: filePickerController.title
                color: theme.titleText
                font.pixelSize: 16
                font.weight: Font.DemiBold
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Button {
                    text: "Up"
                    onClicked: filePickerController.goUp()

                    contentItem: Label {
                        text: parent.text
                        color: theme.titleText
                        font.pixelSize: 12
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    background: Rectangle {
                        radius: 8
                        color: parent.down
                            ? theme.pressedFill
                            : (parent.hovered ? theme.hoverFill : "#18202b")
                        border.width: 1
                        border.color: "#324155"
                    }
                }

                TextField {
                    id: pathField
                    Layout.fillWidth: true
                    text: filePickerController.currentPath
                    selectByMouse: true
                    color: theme.titleText
                    selectedTextColor: "#0f141b"
                    selectionColor: "#76a9de"
                    placeholderTextColor: theme.subtitleText
                    onAccepted: filePickerController.currentPath = text

                    background: Rectangle {
                        radius: 8
                        color: "#10151c"
                        border.width: 1
                        border.color: pathField.activeFocus ? "#76a9de" : "#2b3442"
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                visible: filePickerController.saveMode

                Label {
                    text: "File"
                    color: theme.subtitleText
                }

                TextField {
                    id: fileNameField
                    Layout.fillWidth: true
                    text: filePickerController.fileName
                    selectByMouse: true
                    color: theme.titleText
                    selectedTextColor: "#0f141b"
                    selectionColor: "#76a9de"
                    placeholderTextColor: theme.subtitleText
                    onTextEdited: filePickerController.fileName = text
                    onAccepted: filePickerController.acceptSelection()

                    background: Rectangle {
                        radius: 8
                        color: "#10151c"
                        border.width: 1
                        border.color: fileNameField.activeFocus ? "#76a9de" : "#2b3442"
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 0
                Layout.preferredHeight: 0
                spacing: 12

                Rectangle {
                    Layout.preferredWidth: 140
                    Layout.fillHeight: true
                    Layout.minimumHeight: 0
                    radius: 10
                    color: "#10151c"
                    border.width: 1
                    border.color: "#202834"

                    ListView {
                        anchors.fill: parent
                        anchors.margins: 6
                        model: filePickerController.sidebarLocations
                        spacing: 4

                        delegate: Button {
                            required property var modelData
                            width: ListView.view.width
                            text: modelData.label
                            flat: true
                            onClicked: filePickerController.openSidebarLocation(modelData.path)

                            contentItem: Label {
                                text: parent.text
                                color: theme.titleText
                                font.pixelSize: 12
                                horizontalAlignment: Text.AlignLeft
                                verticalAlignment: Text.AlignVCenter
                                leftPadding: 8
                                elide: Text.ElideRight
                            }

                            background: Rectangle {
                                radius: 8
                                color: parent.down
                                    ? theme.pressedFill
                                    : (parent.hovered ? theme.hoverFill : "transparent")
                            }
                        }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 0
                    radius: 10
                    color: "#10151c"
                    border.width: 1
                    border.color: "#202834"

                    ListView {
                        id: listView
                        anchors.fill: parent
                        anchors.margins: 6
                        clip: true
                        model: filePickerController.entries
                        spacing: 2

                        delegate: Rectangle {
                            id: row
                            required property int index
                            required property string name
                            required property string path
                            required property bool directory
                            required property string sizeText

                            width: ListView.view.width
                            height: 34
                            radius: 8
                            color: filePickerController.selectedPath === path
                                ? "#213247"
                                : (rowArea.containsMouse ? "#18202b" : "transparent")
                            border.width: filePickerController.selectedPath === path ? 1 : 0
                            border.color: "#4f78a3"

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 10
                                anchors.rightMargin: 10
                                spacing: 8

                                Label {
                                    text: row.directory ? "\u25B8" : "\u2022"
                                    color: row.directory ? "#9ec3ef" : "#91a0b1"
                                    font.pixelSize: 12
                                }

                                Label {
                                    Layout.fillWidth: true
                                    text: row.name
                                    color: "#e7eef6"
                                    font.pixelSize: 12
                                    elide: Text.ElideRight
                                }

                                Label {
                                    text: row.sizeText
                                    color: "#91a0b1"
                                    font.pixelSize: 11
                                }
                            }

                            MouseArea {
                                id: rowArea
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: filePickerController.selectEntry(row.index)
                                onDoubleClicked: filePickerController.activateEntry(row.index)
                            }
                        }

                        ScrollBar.vertical: ScrollBar {}
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignBottom
                spacing: 8

                Label {
                    Layout.fillWidth: true
                    text: filePickerController.selectedPath
                    color: theme.subtitleText
                    font.pixelSize: 11
                    elide: Text.ElideMiddle
                }

                Button {
                    text: "Cancel"
                    onClicked: filePickerController.cancel()

                    contentItem: Label {
                        text: parent.text
                        color: theme.titleText
                        font.pixelSize: 12
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    background: Rectangle {
                        radius: 8
                        color: parent.down
                            ? theme.pressedFill
                            : (parent.hovered ? theme.hoverFill : "#18202b")
                        border.width: 1
                        border.color: "#324155"
                    }
                }

                Button {
                    text: filePickerController.actionText
                    highlighted: true
                    enabled: filePickerController.saveMode
                        ? filePickerController.fileName.length > 0
                        : (filePickerController.directoryMode
                        ? filePickerController.currentPath.length > 0
                        : filePickerController.selectedPath.length > 0)
                    onClicked: filePickerController.acceptSelection()

                    contentItem: Label {
                        text: parent.text
                        color: enabled ? "#eef3f8" : theme.menuItemDisabled
                        font.pixelSize: 12
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    background: Rectangle {
                        radius: 8
                        color: !parent.enabled
                            ? "#1c222b"
                            : (parent.down ? "#2e5f94" : (parent.hovered ? "#3b78bc" : "#315f93"))
                        border.width: 1
                        border.color: !parent.enabled ? "#2b3442" : "#76a9de"
                    }
                }
            }
        }
    }
}
