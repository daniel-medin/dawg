import QtQuick 2.15

Rectangle {
    id: root

    color: "#0d1117"

    Column {
        anchors.fill: parent
        anchors.margins: 14
        spacing: 10

        Text {
            text: nodeEditorController.titleText
            color: "#eef2f6"
            font.pixelSize: 18
            font.weight: Font.DemiBold
        }

        Rectangle {
            width: parent.width
            height: 112
            radius: 8
            color: "#121820"
            border.width: 1
            border.color: "#202936"

            Column {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 6

                Text {
                    text: nodeEditorController.selectedNodeText
                    color: "#d9e2ec"
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                }

                Text {
                    text: nodeEditorController.bodyText
                    color: "#94a3b3"
                    font.pixelSize: 13
                    wrapMode: Text.WordWrap
                }
            }
        }
    }
}
