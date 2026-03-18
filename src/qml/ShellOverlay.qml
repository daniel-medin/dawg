import QtQuick 2.15
import QtQuick.Controls 2.15

Item {
    id: root

    visible: shellOverlay.visible

    Rectangle {
        id: tipsBubble

        visible: shellOverlay.canvasTipsVisible
        x: shellOverlay.canvasTipsX
        y: shellOverlay.canvasTipsY
        width: Math.min(shellOverlay.canvasTipsMaxWidth, tipsText.implicitWidth + 20)
        height: tipsText.implicitHeight + 16
        radius: 8
        color: Qt.rgba(11 / 255, 15 / 255, 20 / 255, 0.61)
        border.width: 1
        border.color: Qt.rgba(80 / 255, 98 / 255, 123 / 255, 0.7)

        Text {
            id: tipsText

            anchors.fill: parent
            anchors.margins: 8
            text: shellOverlay.canvasTipsMessage
            color: "#dce4ec"
            font.pixelSize: 13
            wrapMode: Text.WordWrap
        }
    }

    Rectangle {
        id: statusBubble

        visible: shellOverlay.statusVisible
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.leftMargin: 16
        anchors.bottomMargin: 16
        width: Math.min(shellOverlay.statusMaxWidth, statusText.implicitWidth + 24)
        height: statusText.implicitHeight + 16
        radius: 8
        color: Qt.rgba(11 / 255, 15 / 255, 20 / 255, 0.82)
        border.width: 1
        border.color: "#324155"

        Text {
            id: statusText

            anchors.fill: parent
            anchors.margins: 8
            text: shellOverlay.statusMessage
            color: "#f2f5f8"
            font.pixelSize: 13
            wrapMode: Text.WordWrap
        }
    }

    MouseArea {
        visible: shellOverlay.trackGainPopupVisible
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        z: 9
        onPressed: shellOverlay.dismissTrackGainPopup()
    }

    Rectangle {
        id: trackGainPopup

        visible: shellOverlay.trackGainPopupVisible
        width: 72
        height: 182
        radius: 10
        color: "#f70c1016"
        border.width: 1
        border.color: "#2a3644"
        z: 10

        readonly property real preferredX: shellOverlay.trackGainPopupAnchorX + 14
        readonly property real preferredY: shellOverlay.trackGainPopupAnchorY - (height / 2)

        x: Math.max(8, Math.min(root.width - width - 8, preferredX))
        y: Math.max(8, Math.min(root.height - height - 8, preferredY))

        Column {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 8

            Slider {
                id: gainSlider

                anchors.horizontalCenter: parent.horizontalCenter
                orientation: Qt.Vertical
                from: shellOverlay.trackGainPopupMinimum
                to: shellOverlay.trackGainPopupMaximum
                stepSize: 1
                live: true
                value: shellOverlay.trackGainPopupSliderValue
                height: 132

                onMoved: shellOverlay.setTrackGainPopupSliderValueFromUi(Math.round(value))

                background: Rectangle {
                    x: (parent.width - width) / 2
                    y: parent.topPadding
                    width: 8
                    height: parent.availableHeight
                    radius: 4
                    color: "#0b1016"
                    border.width: 1
                    border.color: "#1d2733"

                    Rectangle {
                        width: parent.width - 2
                        x: 1
                        y: gainSlider.visualPosition * (parent.height - height)
                        height: parent.height - y - 1
                        radius: 3
                        color: "#3d6ea5"
                    }
                }

                handle: Rectangle {
                    x: (gainSlider.width - width) / 2
                    y: gainSlider.topPadding + gainSlider.visualPosition * (gainSlider.availableHeight - height)
                    width: 16
                    height: 12
                    radius: 6
                    color: "#d7dee7"
                    border.width: 1
                    border.color: "#eff3f7"
                }
            }

            Text {
                width: parent.width
                text: shellOverlay.trackGainPopupValue
                color: "#eef2f6"
                font.pixelSize: 12
                horizontalAlignment: Text.AlignHCenter
            }
        }

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.NoButton
            onPressed: function(mouse) { mouse.accepted = true }
        }

        onVisibleChanged: {
            if (visible) {
                gainSlider.forceActiveFocus()
            }
        }
    }
}
