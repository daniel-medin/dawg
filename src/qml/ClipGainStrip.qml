import QtQuick 2.15

Rectangle {
    id: root

    property real gainDb: 0.0
    property real meterLevel: 0.0
    property bool showMeter: true

    signal gainDragged(real gainDb)

    readonly property real minGainDb: -100.0
    readonly property real maxGainDb: 12.0
    readonly property real handleHeight: 16

    function clamp(value, minValue, maxValue) {
        return Math.max(minValue, Math.min(maxValue, value))
    }

    function gainToNormalized(db) {
        return clamp((db - minGainDb) / (maxGainDb - minGainDb), 0.0, 1.0)
    }

    function normalizedToGain(normalized) {
        var db = minGainDb + clamp(normalized, 0.0, 1.0) * (maxGainDb - minGainDb)
        return Math.round(db * 10.0) / 10.0
    }

    function gainLabelText(db) {
        if (db <= minGainDb + 0.001) {
            return "-inf"
        }
        return Number(db).toFixed(1) + " dB"
    }

    function meterNormalized(level) {
        if (level <= 0.0) {
            return 0.0
        }
        var meterDb = Math.max(minGainDb, Math.min(0.0, 20.0 * Math.log(level) / Math.LN10))
        return clamp((meterDb - minGainDb) / (maxGainDb - minGainDb), 0.0, 1.0)
    }

    color: "#0f141b"
    border.width: 1
    border.color: "#1b2430"
    radius: 8
    implicitWidth: 76
    implicitHeight: 180

    Column {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 6

        Item {
            width: parent.width
            height: Math.max(128, parent.height - 34)

            Row {
                anchors.centerIn: parent
                height: parent.height
                spacing: 6

                Rectangle {
                    id: meterTrack
                    visible: root.showMeter
                    width: 12
                    height: parent.height
                    radius: 4
                    color: "#0b1016"
                    border.width: 1
                    border.color: "#1d2733"
                    clip: true

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: root.meterNormalized(root.meterLevel) * parent.height
                        radius: 3
                        gradient: Gradient {
                            GradientStop { position: 0.0; color: "#ff5b4d" }
                            GradientStop { position: 0.08; color: "#ffb33b" }
                            GradientStop { position: 0.22; color: "#2fe06d" }
                            GradientStop { position: 1.0; color: "#2fe06d" }
                        }
                    }
                }

                Rectangle {
                    id: faderTrack
                    width: 18
                    height: parent.height
                    radius: 8
                    color: "#12171d"
                    border.width: 1
                    border.color: "#263241"

                    Rectangle {
                        width: 6
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        radius: 3
                        color: "#1f2935"
                    }

                    Rectangle {
                        id: faderFill
                        width: 6
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.bottom: parent.bottom
                        height: root.gainToNormalized(root.gainDb) * (faderTrack.height - 10) + 5
                        radius: 3
                        color: "#39495d"
                    }

                    Rectangle {
                        id: handle
                        width: 16
                        height: root.handleHeight
                        radius: 8
                        x: 1
                        y: {
                            var available = faderTrack.height - height - 2
                            return 1 + (1.0 - root.gainToNormalized(root.gainDb)) * available
                        }
                        color: "#f2f5f8"
                        border.width: 1
                        border.color: "#3e4d61"
                    }

                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor

                        function applyFromY(mouseY) {
                            var available = faderTrack.height - root.handleHeight - 2
                            var normalized = 1.0 - ((mouseY - 1 - root.handleHeight * 0.5) / Math.max(1, available))
                            root.gainDb = root.normalizedToGain(normalized)
                            root.gainDragged(root.gainDb)
                        }

                        onPressed: function(mouse) {
                            if (mouse.modifiers & Qt.ControlModifier) {
                                root.gainDb = 0.0
                                root.gainDragged(root.gainDb)
                                return
                            }
                            applyFromY(mouse.y)
                        }

                        onPositionChanged: function(mouse) {
                            if (pressed) {
                                applyFromY(mouse.y)
                            }
                        }
                    }
                }
            }
        }

        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            color: "#cad3dc"
            font.pixelSize: 10
            text: root.gainLabelText(root.gainDb)
        }
    }
}
