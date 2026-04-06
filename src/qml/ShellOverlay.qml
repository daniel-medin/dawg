import QtQuick 2.15
import QtQuick.Controls 2.15

Item {
    id: root

    visible: shellOverlay.visible

    Rectangle {
        visible: shellOverlay.topProgressVisible
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 2
        color: "#15271d"
        z: 20
    }

    Rectangle {
        visible: shellOverlay.topProgressVisible
        anchors.top: parent.top
        anchors.left: parent.left
        height: 2
        width: Math.max(2, parent.width * shellOverlay.topProgress)
        color: "#2ecf72"
        z: 21
    }

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
        width: 96
        height: 224
        radius: 12
        color: "#ee0f141b"
        border.width: 1
        border.color: "#324155"
        z: 10

        readonly property real preferredX: shellOverlay.trackGainPopupAnchorX + 14
        readonly property real preferredY: shellOverlay.trackGainPopupAnchorY - (height / 2)
        readonly property real gainDb: shellOverlay.trackGainPopupSliderValue <= shellOverlay.trackGainPopupMinimum
            ? -100.0
            : shellOverlay.trackGainPopupSliderValue / 10.0

        x: Math.max(8, Math.min(root.width - width - 8, preferredX))
        y: Math.max(8, Math.min(root.height - height - 8, preferredY))

        ClipGainStrip {
            id: gainFader

            anchors.centerIn: parent
            gainDb: trackGainPopup.gainDb
            meterLevel: 0.0
            showMeter: false

            onGainDragged: function(gainDb) {
                var sliderValue = gainDb <= -99.95 ? shellOverlay.trackGainPopupMinimum : Math.round(gainDb * 10.0)
                shellOverlay.setTrackGainPopupSliderValueFromUi(sliderValue)
            }
        }

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.NoButton
            onPressed: function(mouse) { mouse.accepted = true }
        }

        onVisibleChanged: {
            if (visible) {
                gainFader.forceActiveFocus()
            }
        }
    }
}
