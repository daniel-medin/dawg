import QtQuick 2.15

Rectangle {
    id: root

    property string listText: ""
    property bool dragging: false
    property point dragOffset: Qt.point(0, 0)

    signal closeClicked()
    signal positionChanged(int x, int y)

    width: 300
    height: 400
    radius: 8
    color: Qt.rgba(11 / 255, 15 / 255, 20 / 255, 0.9)
    border.width: 1
    border.color: "#253142"

    Rectangle {
        id: titleBar
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 36
        radius: 8
        color: Qt.rgba(17 / 255, 24 / 255, 33 / 255, 0.9)

        Text {
            id: titleText
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: 12
            text: "Debug"
            color: "#f3f5f7"
            font.pixelSize: 13
            font.weight: Font.DemiBold
        }

        Rectangle {
            id: closeButton
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.rightMargin: 8
            width: 22
            height: 22
            radius: 4
            color: closeButtonArea.containsMouse ? "#223146" : "#18202b"
            border.width: 1
            border.color: "#324155"

            Text {
                anchors.centerIn: parent
                text: "x"
                color: "#ecf1f6"
                font.pixelSize: 12
            }

            MouseArea {
                id: closeButtonArea
                anchors.fill: parent
                hoverEnabled: true
                onClicked: root.closeClicked()
            }
        }
    }

    Rectangle {
        id: textLabel
        anchors.top: titleBar.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 8
        radius: 8
        color: Qt.rgba(11 / 255, 15 / 255, 20 / 255, 0.9)
        border.width: 1
        border.color: "#253142"

        Text {
            id: debugText
            anchors.fill: parent
            anchors.margins: 10
            text: root.listText
            color: "#d8dde4"
            font.pixelSize: 9
            wrapMode: Text.WordWrap
            textFormat: Text.PlainText
        }
    }
}
