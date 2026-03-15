import QtQuick 2.15
import Dawg 1.0

Rectangle {
    id: root

    color: "#0f141b"

    function scrollHandleWidth() {
        var trackWidth = scrollTrack.width
        var maxRange = clipWaveform.scrollMaximum + clipWaveform.scrollPageStep
        if (trackWidth <= 0 || maxRange <= 0) {
            return 24
        }
        return Math.max(24, trackWidth * clipWaveform.scrollPageStep / maxRange)
    }

    function scrollHandleX() {
        if (!clipWaveform.scrollVisible || clipWaveform.scrollMaximum <= 0) {
            return scrollTrack.x
        }
        return scrollTrack.x
            + (scrollTrack.width - scrollHandle.width)
            * (clipWaveform.scrollValue / clipWaveform.scrollMaximum)
    }

    function viewStartForLocalX(localX) {
        if (clipWaveform.scrollMaximum <= 0) {
            return 0
        }
        var available = Math.max(1, scrollTrack.width - scrollHandle.width)
        var ratio = Math.max(0, Math.min(1, (localX - scrollTrack.x - scrollHandle.width * 0.5) / available))
        return Math.round(ratio * clipWaveform.scrollMaximum)
    }

    Column {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 10

        Row {
            width: parent.width
            spacing: 8

            Text {
                width: parent.width - (loopButton.visible ? loopButton.width + 8 : 0)
                text: clipEditorController.titleText
                color: "#f1f4f7"
                font.pixelSize: 18
                font.weight: Font.DemiBold
                elide: Text.ElideRight
            }

            Rectangle {
                id: loopButton
                visible: clipEditorController.showLoopButton
                width: 108
                height: 26
                radius: 6
                color: clipEditorController.loopEnabled ? "#2b4d31" : "#141a21"
                border.width: 1
                border.color: clipEditorController.loopEnabled ? "#4f8a58" : "#2a3541"

                Text {
                    anchors.centerIn: parent
                    text: "Loop Sound"
                    color: clipEditorController.loopEnabled ? "#eef7f0" : "#d5dce4"
                    font.pixelSize: 12
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: clipEditorController.handleLoopToggled(!clipEditorController.loopEnabled)
                }
            }
        }

        Row {
            visible: clipEditorController.showInfoBar
            width: parent.width
            spacing: 18

            Repeater {
                model: [
                    clipEditorController.sourceText,
                    clipEditorController.rangeText,
                    clipEditorController.durationText,
                    clipEditorController.positionText
                ]

                Text {
                    text: modelData
                    color: "#aeb8c4"
                    font.pixelSize: 12
                }
            }
        }

        Row {
            visible: clipEditorController.showEditorContent
            width: parent.width
            height: Math.max(160, parent.height - y)
            spacing: 12

            Loader {
                id: gainStripLoader
                width: 76
                height: parent.height
                source: "qrc:/qml/ClipGainStrip.qml"

                onLoaded: {
                    item.gainDb = clipEditorController.gainDb
                    item.meterLevel = clipEditorController.meterLevel
                    item.gainDragged.connect(function(gainDb) {
                        clipEditorController.handleGainDragged(gainDb)
                    })
                }
            }

            Connections {
                target: clipEditorController

                function onStateChanged() {
                    if (gainStripLoader.item) {
                        gainStripLoader.item.gainDb = clipEditorController.gainDb
                        gainStripLoader.item.meterLevel = clipEditorController.meterLevel
                    }
                }
            }

            Column {
                width: parent.width - gainStripLoader.width - parent.spacing
                height: parent.height
                spacing: 6

                ClipWaveformQuickItem {
                    id: clipWaveform
                    objectName: "clipWaveform"
                    width: parent.width
                    height: parent.height - (scrollTrack.visible ? 18 : 0)
                }

                Rectangle {
                    id: scrollTrack
                    visible: clipWaveform.scrollVisible
                    width: parent.width
                    height: 12
                    radius: 6
                    color: "#0d1218"
                    border.width: 1
                    border.color: "#1c2631"

                    Rectangle {
                        id: scrollHandle
                        width: root.scrollHandleWidth()
                        height: 10
                        x: root.scrollHandleX()
                        y: 1
                        radius: 5
                        color: scrollMouseArea.pressed ? "#50657b" : "#334252"
                    }

                    MouseArea {
                        id: scrollMouseArea
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor

                        onPressed: function(mouse) {
                            clipWaveform.setViewStartMs(root.viewStartForLocalX(mouse.x))
                        }

                        onPositionChanged: function(mouse) {
                            if (pressed) {
                                clipWaveform.setViewStartMs(root.viewStartForLocalX(mouse.x))
                            }
                        }
                    }
                }
            }
        }

        Item {
            visible: clipEditorController.showEmptyState
            width: parent.width
            height: Math.max(160, parent.height - y)

            Rectangle {
                width: parent.width
                height: 96
                radius: 8
                color: "#12171d"
                border.width: 1
                border.color: "#232c35"
                anchors.top: parent.top

                Column {
                    anchors.fill: parent
                    anchors.margins: 14
                    spacing: 4

                    Text {
                        text: clipEditorController.emptyTitleText
                        color: "#eef2f6"
                        font.pixelSize: 16
                        font.weight: Font.DemiBold
                    }

                    Text {
                        text: clipEditorController.emptyBodyText
                        color: "#9ca8b6"
                        font.pixelSize: 13
                        wrapMode: Text.WordWrap
                    }

                    Text {
                        visible: clipEditorController.showEmptyAction
                        text: clipEditorController.emptyActionText
                        color: "#eef3f8"
                        font.pixelSize: 13
                        font.weight: Font.DemiBold

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: clipEditorController.requestAttachAudio()
                        }
                    }
                }
            }
        }
    }
}
