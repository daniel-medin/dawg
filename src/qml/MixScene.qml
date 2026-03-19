import QtQuick 2.15
import QtQuick.Controls 2.15

Rectangle {
    id: root

    color: "#080b10"

    Flickable {
        id: flickArea
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        anchors.topMargin: 10
        anchors.bottomMargin: 10
        contentWidth: stripsRow.width
        contentHeight: stripsRow.height
        clip: true
        interactive: false
        boundsMovement: Flickable.StopAtBounds
        boundsBehavior: Flickable.StopAtBounds

        ScrollBar.horizontal: ScrollBar {
            policy: flickArea.contentWidth > flickArea.width ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
        }

        Row {
            id: stripsRow

            spacing: 10
            height: flickArea.height

            MixStrip {
                id: masterStrip

                property var strip: mixController.masterStrip

                masterStrip: strip ? strip.masterStrip : true
                titleText: strip ? strip.titleText : "Master"
                detailText: strip ? strip.detailText : "Main Out"
                footerText: strip ? strip.footerText : "MASTER"
                accentColor: strip ? strip.accentColor : "#f0f4f8"
                gainDb: strip ? strip.gainDb : 0.0
                muted: strip ? strip.muted : false
                soloEnabled: strip ? strip.soloEnabled : false
                soloed: strip ? strip.soloed : false
                playbackActive: mixController.playbackActive
                meterResetToken: mixController.meterResetToken
                useStereoMeter: strip ? strip.useStereoMeter : true
                meterLevel: strip ? strip.meterLevel : 0.0
                meterLeftLevel: strip ? strip.meterLeftLevel : meterLevel
                meterRightLevel: strip ? strip.meterRightLevel : meterLevel
                height: stripsRow.height
                onGainDragged: function(gainDb) {
                    mixController.setMasterGainDb(gainDb)
                }
                onMuteToggled: function(muted) {
                    mixController.setMasterMuted(muted)
                }
            }

            Item {
                visible: mixController.laneStrips.length === 0
                width: 220
                height: stripsRow.height

                Text {
                    anchors.centerIn: parent
                    text: "No mix lanes yet."
                    color: "#8d9aae"
                    font.pixelSize: 13
                }
            }

            Repeater {
                model: mixController.laneStrips

                MixStrip {
                    required property var modelData

                    property var strip: modelData

                    masterStrip: strip.masterStrip
                    titleText: strip.titleText
                    detailText: strip.detailText
                    footerText: strip.footerText
                    accentColor: strip.accentColor
                    gainDb: strip.gainDb
                    muted: strip.muted
                    soloEnabled: strip.soloEnabled
                    soloed: strip.soloed
                    playbackActive: mixController.playbackActive
                    meterResetToken: mixController.meterResetToken
                    useStereoMeter: strip.useStereoMeter
                    meterLevel: strip.meterLevel
                    meterLeftLevel: strip.meterLeftLevel
                    meterRightLevel: strip.meterRightLevel
                    height: stripsRow.height
                    onGainDragged: function(gainDb) {
                        mixController.setLaneGainDb(strip.laneIndex, gainDb)
                    }
                    onMuteToggled: function(muted) {
                        mixController.setLaneMuted(strip.laneIndex, muted)
                    }
                    onSoloToggled: function(soloed) {
                        mixController.setLaneSoloed(strip.laneIndex, soloed)
                    }
                }
            }
        }
    }
}
