import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15

Item {
    id: root
    visible: dialogController.visible
    implicitWidth: 480
    implicitHeight: contentColumn.implicitHeight + 28
    clip: true

    readonly property real overlayMargin: 16
    readonly property real availableWidth: parent
        ? Math.max(280, parent.width - overlayMargin * 2)
        : implicitWidth
    readonly property real availableHeight: parent
        ? Math.max(180, parent.height - overlayMargin * 2)
        : implicitHeight

    width: Math.min(implicitWidth, availableWidth)
    height: Math.min(implicitHeight, availableHeight)

    AppTheme {
        id: theme
    }

    onVisibleChanged: {
        if (!visible) {
            return
        }

        if (dialogController.inputMode) {
            inputField.forceActiveFocus()
            inputField.selectAll()
        } else {
            root.forceActiveFocus()
        }
    }

    Keys.onEscapePressed: dialogController.respond("cancel", inputField.text)

    Rectangle {
        anchors.fill: parent
        radius: 14
        color: "#14181f"
        border.width: 1
        border.color: "#2b3442"

        ColumnLayout {
            id: contentColumn

            anchors.fill: parent
            anchors.margins: 14
            spacing: 10

            Label {
                Layout.fillWidth: true
                text: dialogController.title
                color: theme.titleText
                font.pixelSize: 16
                font.weight: Font.DemiBold
                wrapMode: Text.WordWrap
            }

            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: dialogController.inputMode ? 72 : 0
                Layout.preferredHeight: bodyColumn.implicitHeight
                clip: true

                ScrollBar.horizontal.policy: ScrollBar.AlwaysOff

                contentWidth: availableWidth

                Column {
                    id: bodyColumn
                    width: root.width - 28
                    spacing: 10

                    Label {
                        width: parent.width
                        visible: !dialogController.inputMode && dialogController.message.length > 0
                        text: dialogController.message
                        color: "#dbe3ec"
                        font.pixelSize: 13
                        wrapMode: Text.WordWrap
                    }

                    Label {
                        width: parent.width
                        visible: !dialogController.inputMode && dialogController.informativeText.length > 0
                        text: dialogController.informativeText
                        color: theme.subtitleText
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                    }

                    Label {
                        width: parent.width
                        visible: dialogController.inputMode
                        text: dialogController.inputLabel
                        color: "#dbe3ec"
                        font.pixelSize: 13
                        wrapMode: Text.WordWrap
                    }

                    TextField {
                        id: inputField

                        width: parent.width
                        visible: dialogController.inputMode
                        text: dialogController.inputText
                        selectByMouse: true
                        onTextChanged: dialogController.inputText = text
                        onVisibleChanged: {
                            if (visible) {
                                forceActiveFocus()
                                selectAll()
                            }
                        }
                        onAccepted: dialogController.respond("ok", text)
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignBottom
                spacing: 8

                Item {
                    Layout.fillWidth: true
                }

                Repeater {
                    model: dialogController.buttons

                    delegate: Button {
                        id: dialogButton

                        required property var modelData

                        text: modelData.text
                        flat: !modelData.accent
                        highlighted: modelData.accent
                        focusPolicy: Qt.TabFocus

                        background: Rectangle {
                            radius: 8
                            color: dialogButton.down
                                ? (modelData.destructive ? "#5f2e2e" : "#2e5f94")
                                : (dialogButton.hovered
                                    ? (modelData.destructive ? "#6f3434" : (modelData.accent ? "#3b78bc" : "#1e2630"))
                                    : (modelData.destructive ? "#4c2828" : (modelData.accent ? "#315f93" : "#18202b")))
                            border.width: 1
                            border.color: modelData.destructive
                                ? "#8f5555"
                                : (modelData.accent ? "#76a9de" : "#324155")
                        }

                        contentItem: Label {
                            text: dialogButton.text
                            color: "#eef3f8"
                            font.pixelSize: 12
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }

                        onClicked: dialogController.respond(modelData.key, inputField.text)

                    }
                }
            }
        }
    }
}
