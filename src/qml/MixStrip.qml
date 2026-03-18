import QtQuick 2.15

Rectangle {
    id: root

    property bool masterStrip: true
    property string titleText: "Master"
    property string detailText: "Main Out"
    property string footerText: "MASTER"
    property color accentColor: "#f0f4f8"
    property real gainDb: 0.0
    property bool muted: false
    property bool soloEnabled: false
    property bool soloed: false
    property real meterLevel: 0.0

    signal gainDragged(real gainDb)
    signal muteToggled(bool muted)
    signal soloToggled(bool soloed)

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

    color: root.masterStrip ? "#141b24" : "#0f141b"
    border.width: 1
    border.color: root.masterStrip ? "#43566f" : "#1b2430"
    radius: 10
    implicitWidth: 94
    implicitHeight: 220
    clip: true

    Item {
        id: contentHost

        anchors.fill: parent
        anchors.margins: 8

        Row {
            id: buttonRow

            anchors.top: parent.top
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 4

            Rectangle {
                id: soloButton
                width: 22
                height: 18
                radius: 4
                color: !root.soloEnabled ? "#171c22" : (root.soloed ? "#db7e26" : "#1b2129")
                border.width: 1
                border.color: !root.soloEnabled ? "#232b35" : (root.soloed ? "#ffd7b4" : "#2c3540")
                opacity: root.soloEnabled ? 1.0 : 0.4

                Text {
                    anchors.centerIn: parent
                    text: "S"
                    color: root.soloEnabled ? (root.soloed ? "#fff4e9" : "#d6dfe9") : "#9ba6b1"
                    font.pixelSize: 10
                    font.bold: true
                }

                MouseArea {
                    anchors.fill: parent
                    enabled: root.soloEnabled
                    preventStealing: true
                    cursorShape: root.soloEnabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                    onClicked: {
                        root.soloed = !root.soloed
                        root.soloToggled(root.soloed)
                    }
                }
            }

            Rectangle {
                id: muteButton
                width: 22
                height: 18
                radius: 4
                color: root.muted ? "#2f3741" : "#1b2129"
                border.width: 1
                border.color: root.muted ? "#4b5661" : "#2c3540"

                Text {
                    anchors.centerIn: parent
                    text: "M"
                    color: root.muted ? "#9ba6b1" : "#d6dfe9"
                    font.pixelSize: 10
                    font.bold: true
                }

                MouseArea {
                    anchors.fill: parent
                    preventStealing: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: {
                        root.muted = !root.muted
                        root.muteToggled(root.muted)
                    }
                }
            }
        }

        Rectangle {
            id: accentBar

            anchors.top: buttonRow.bottom
            anchors.topMargin: 6
            width: parent.width
            height: 6
            radius: 3
            color: root.accentColor
        }

        Text {
            id: titleLabel

            anchors.top: accentBar.bottom
            anchors.topMargin: 6
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            color: "#eef2f6"
            font.pixelSize: 12
            font.bold: true
            text: root.titleText
        }

        Text {
            id: detailLabel

            anchors.top: titleLabel.bottom
            anchors.topMargin: 4
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            color: "#95a4b5"
            font.pixelSize: 10
            text: root.detailText
        }

        Text {
            id: footerLabel

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            horizontalAlignment: Text.AlignHCenter
            color: "#6e8094"
            font.pixelSize: 9
            font.letterSpacing: 1.0
            text: root.footerText
        }

        Text {
            id: gainLabel

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: footerLabel.top
            anchors.bottomMargin: 4
            horizontalAlignment: Text.AlignHCenter
            color: "#cad3dc"
            font.pixelSize: 10
            text: root.gainLabelText(root.gainDb)
        }

        Item {
            id: faderArea

            property real areaHeight: Math.max(24, gainLabel.y - (detailLabel.y + detailLabel.height) - 12)
            x: 0
            y: detailLabel.y + detailLabel.height + 6
            width: parent.width
            height: areaHeight

            Row {
                anchors.centerIn: parent
                height: parent.height
                spacing: 8

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
                        preventStealing: true
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

                Rectangle {
                    id: meterTrack
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
            }
        }
    }
}
