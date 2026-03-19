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
    property bool playbackActive: false
    property int meterResetToken: 0
    property bool useStereoMeter: masterStrip
    property real meterLevel: 0.0
    property real meterLeftLevel: meterLevel
    property real meterRightLevel: meterLevel

    signal gainDragged(real gainDb)
    signal muteToggled(bool muted)
    signal soloToggled(bool soloed)

    readonly property real minGainDb: -100.0
    readonly property real maxGainDb: 12.0
    readonly property real handleHeight: 16
    readonly property real meterFloorDb: -72.0
    readonly property real meterZeroNormalized: 2.0 / 3.0
    readonly property int meterSegmentCount: 96
    readonly property real meterSegmentGap: 0.5
    readonly property real meterReleasePerSecond: 0.45
    readonly property real peakReleasePerSecond: 0.18
    readonly property int peakHoldDurationMs: 3000
    readonly property real clipThreshold: 0.988

    property real displayedLeftLevel: 0.0
    property real displayedRightLevel: 0.0
    property real heldPeakLeftLevel: 0.0
    property real heldPeakRightLevel: 0.0
    property real readoutPeakLeftLevel: 0.0
    property real readoutPeakRightLevel: 0.0
    property bool clipLatchedLeft: false
    property bool clipLatchedRight: false
    property real leftPeakHoldDeadlineMs: 0.0
    property real rightPeakHoldDeadlineMs: 0.0
    property real lastMeterTickMs: 0.0

    function clamp(value, minValue, maxValue) {
        return Math.max(minValue, Math.min(maxValue, value))
    }

    function gainToNormalized(db) {
        return clamp((db - minGainDb) / (maxGainDb - minGainDb), 0.0, 1.0)
    }

    function gainToFaderPosition(db) {
        var clampedDb = clamp(db, minGainDb, maxGainDb)
        if (clampedDb <= 0.0) {
            return clamp(((clampedDb - minGainDb) / (0.0 - minGainDb)) * meterZeroNormalized, 0.0, meterZeroNormalized)
        }
        return clamp(
            meterZeroNormalized + (clampedDb / maxGainDb) * (1.0 - meterZeroNormalized),
            meterZeroNormalized,
            1.0)
    }

    function normalizedToGain(normalized) {
        var db = minGainDb + clamp(normalized, 0.0, 1.0) * (maxGainDb - minGainDb)
        return Math.round(db * 10.0) / 10.0
    }

    function faderPositionToGain(position) {
        var clampedPosition = clamp(position, 0.0, 1.0)
        var db = 0.0
        if (clampedPosition <= meterZeroNormalized) {
            db = minGainDb + (clampedPosition / meterZeroNormalized) * (0.0 - minGainDb)
        } else {
            db = ((clampedPosition - meterZeroNormalized) / (1.0 - meterZeroNormalized)) * maxGainDb
        }
        return Math.round(clamp(db, minGainDb, maxGainDb) * 10.0) / 10.0
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
        var meterDb = 20.0 * Math.log(level) / Math.LN10
        return meterPositionForDb(meterDb)
    }

    function meterPositionForDb(db) {
        var clampedDb = Math.max(meterFloorDb, Math.min(maxGainDb, db))
        if (clampedDb <= 0.0) {
            return clamp(((clampedDb - meterFloorDb) / (0.0 - meterFloorDb)) * meterZeroNormalized, 0.0, meterZeroNormalized)
        }
        return clamp(
            meterZeroNormalized + (clampedDb / maxGainDb) * (1.0 - meterZeroNormalized),
            meterZeroNormalized,
            1.0)
    }

    function meterDbAtPosition(position) {
        var clampedPosition = clamp(position, 0.0, 1.0)
        if (clampedPosition <= meterZeroNormalized) {
            return meterFloorDb + (clampedPosition / meterZeroNormalized) * (0.0 - meterFloorDb)
        }
        return ((clampedPosition - meterZeroNormalized) / (1.0 - meterZeroNormalized)) * maxGainDb
    }

    function levelToDbText(level) {
        if (level <= 0.00001) {
            return "-inf"
        }
        return Number(levelToDbValue(level)).toFixed(1)
    }

    function levelToDbValue(level) {
        if (level <= 0.00001) {
            return minGainDb
        }
        return Math.max(minGainDb, Math.min(12.0, 20.0 * Math.log(level) / Math.LN10))
    }

    function meterBarLevel(index) {
        if (!useStereoMeter) {
            return displayedLeftLevel
        }
        return index === 0 ? displayedLeftLevel : displayedRightLevel
    }

    function heldPeakLevel(index) {
        if (!useStereoMeter) {
            return heldPeakLeftLevel
        }
        return index === 0 ? heldPeakLeftLevel : heldPeakRightLevel
    }

    function clipLatched(index) {
        if (!useStereoMeter) {
            return clipLatchedLeft
        }
        return index === 0 ? clipLatchedLeft : clipLatchedRight
    }

    function segmentColor(position) {
        var db = meterDbAtPosition(position)
        if (db >= 0.0) {
            return "#ff5b4d"
        }
        if (db >= -9.0) {
            return "#ffb33b"
        }
        return "#2fe06d"
    }

    function peakReadoutText() {
        if (useStereoMeter) {
            return "L " + levelToDbText(readoutPeakLeftLevel) + "\nR " + levelToDbText(readoutPeakRightLevel)
        }
        return "PK " + levelToDbText(readoutPeakLeftLevel)
    }

    function resetPeakHistory() {
        heldPeakLeftLevel = 0.0
        heldPeakRightLevel = 0.0
        readoutPeakLeftLevel = 0.0
        readoutPeakRightLevel = 0.0
        clipLatchedLeft = false
        clipLatchedRight = false
        leftPeakHoldDeadlineMs = 0.0
        rightPeakHoldDeadlineMs = 0.0
    }

    function clearMeterState() {
        displayedLeftLevel = 0.0
        displayedRightLevel = 0.0
        lastMeterTickMs = 0.0
        resetPeakHistory()
    }

    function updateChannelState(channelName, sourceLevel, nowMs, deltaSeconds) {
        var displayedName = "displayed" + channelName + "Level"
        var heldName = "heldPeak" + channelName + "Level"
        var readoutName = "readoutPeak" + channelName + "Level"
        var clipName = "clipLatched" + channelName
        var deadlineName = channelName.toLowerCase() + "PeakHoldDeadlineMs"

        var displayedLevel = root[displayedName]
        if (sourceLevel >= displayedLevel) {
            displayedLevel = sourceLevel
        } else {
            displayedLevel = Math.max(sourceLevel, displayedLevel - root.meterReleasePerSecond * deltaSeconds)
        }
        root[displayedName] = displayedLevel

        var heldPeak = root[heldName]
        if (sourceLevel >= heldPeak) {
            heldPeak = sourceLevel
            root[deadlineName] = nowMs + root.peakHoldDurationMs
        } else if (nowMs >= root[deadlineName]) {
            heldPeak = Math.max(displayedLevel, heldPeak - root.peakReleasePerSecond * deltaSeconds)
        }
        root[heldName] = heldPeak

        if (sourceLevel > root[readoutName]) {
            root[readoutName] = sourceLevel
        }
        if (sourceLevel >= root.clipThreshold) {
            root[clipName] = true
        }
    }

    color: root.masterStrip ? "#141b24" : "#0f141b"
    border.width: 1
    border.color: root.masterStrip ? "#253140" : "#1b2430"
    radius: 10
    implicitWidth: 94
    implicitHeight: 220
    clip: true

    onMeterResetTokenChanged: clearMeterState()
    onUseStereoMeterChanged: clearMeterState()

    Timer {
        interval: 33
        running: true
        repeat: true
        onTriggered: {
            var nowMs = Date.now()
            var deltaSeconds = root.lastMeterTickMs > 0.0
                ? Math.max(0.001, (nowMs - root.lastMeterTickMs) / 1000.0)
                : 0.033
            root.lastMeterTickMs = nowMs

            root.updateChannelState("Left", root.useStereoMeter ? root.meterLeftLevel : root.meterLevel, nowMs, deltaSeconds)
            if (root.useStereoMeter) {
                root.updateChannelState("Right", root.meterRightLevel, nowMs, deltaSeconds)
            } else {
                root.displayedRightLevel = root.displayedLeftLevel
                root.heldPeakRightLevel = root.heldPeakLeftLevel
                root.readoutPeakRightLevel = root.readoutPeakLeftLevel
                root.clipLatchedRight = root.clipLatchedLeft
                root.rightPeakHoldDeadlineMs = root.leftPeakHoldDeadlineMs
            }
        }
    }

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
                visible: !root.masterStrip
                width: visible ? 22 : 0
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
                        root.soloToggled(!root.soloed)
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
                        root.muteToggled(!root.muted)
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
            id: peakReadoutLabel

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: gainLabel.top
            anchors.bottomMargin: 4
            horizontalAlignment: Text.AlignHCenter
            color: (root.clipLatchedLeft || root.clipLatchedRight) ? "#ffb6ae" : "#90a1b3"
            font.pixelSize: 8
            lineHeight: 0.95
            text: root.peakReadoutText()
        }

        MouseArea {
            anchors.fill: peakReadoutLabel
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: root.resetPeakHistory()
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

            property real areaHeight: Math.max(24, peakReadoutLabel.y - (detailLabel.y + detailLabel.height) - 12)
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
                        height: root.gainToFaderPosition(root.gainDb) * (faderTrack.height - 10) + 5
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
                            return 1 + (1.0 - root.gainToFaderPosition(root.gainDb)) * available
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
                            var position = 1.0 - ((mouseY - 1 - root.handleHeight * 0.5) / Math.max(1, available))
                            root.gainDb = root.faderPositionToGain(position)
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

                Item {
                    id: meterRack
                    width: root.useStereoMeter ? 20 : 12
                    height: parent.height

                    Row {
                        anchors.fill: parent
                        spacing: root.useStereoMeter ? 2 : 0

                        Repeater {
                            model: root.useStereoMeter ? 2 : 1

                            Item {
                                property int meterBarIndex: index
                                width: root.useStereoMeter ? 9 : meterRack.width
                                height: meterRack.height
                                clip: true

                                Rectangle {
                                    anchors.fill: parent
                                    radius: 4
                                    color: "#0b1016"
                                    border.width: root.useStereoMeter ? 0 : 1
                                    border.color: "#1d2733"
                                }

                                Item {
                                    id: segmentArea

                                    anchors.fill: parent
                                    anchors.topMargin: 6
                                    anchors.bottomMargin: 2
                                    anchors.leftMargin: 1
                                    anchors.rightMargin: 1
                                    property int meterBarIndex: parent.meterBarIndex

                                    readonly property real segmentHeight: Math.max(
                                        2,
                                        (height - ((root.meterSegmentCount - 1) * root.meterSegmentGap)) / root.meterSegmentCount)

                                    Repeater {
                                        model: root.meterSegmentCount

                                        Rectangle {
                                            readonly property real segmentPosition: (index + 1) / root.meterSegmentCount
                                            width: segmentArea.width
                                            height: segmentArea.segmentHeight
                                            x: 0
                                            y: segmentArea.height - ((index + 1) * height) - (index * root.meterSegmentGap)
                                            radius: 1
                                            color: root.segmentColor(segmentPosition)
                                            opacity: segmentPosition <= root.meterNormalized(root.meterBarLevel(segmentArea.meterBarIndex)) ? 1.0 : 0.12
                                        }
                                    }
                                }

                                Rectangle {
                                    width: parent.width
                                    height: 1
                                    color: "#b8c6d6"
                                    opacity: 0.55
                                    y: {
                                        var normalized = root.meterZeroNormalized
                                        return (segmentArea.y + segmentArea.height) - (normalized * segmentArea.height)
                                    }
                                }

                                Rectangle {
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.top: parent.top
                                    height: 4
                                    radius: 2
                                    color: root.clipLatched(index) ? "#ff5b4d" : "#161b21"
                                    border.width: root.clipLatched(index) ? 1 : 0
                                    border.color: "#ffd1cb"
                                }

                                Rectangle {
                                    width: parent.width
                                    height: 2
                                    radius: 1
                                    visible: root.heldPeakLevel(index) > 0.0001
                                    color: "#f4f8fb"
                                    y: {
                                        var normalized = root.meterNormalized(root.heldPeakLevel(index))
                                        return (segmentArea.y + segmentArea.height) - (normalized * segmentArea.height) - height * 0.5
                                    }
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: root.resetPeakHistory()
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
