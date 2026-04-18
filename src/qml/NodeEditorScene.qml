import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import QtQuick.Window 2.15
import Dawg 1.0

Rectangle {
    id: root

    color: "#0d1117"
    property int laneHeaderWidth: 148
    property color menuPopupTextColor: "#eef2f6"
    property string activeMenuKind: ""
    property var activeMenuItems: []
    property var activeMenuAnchor: null
    property bool editMarkerBlinkOn: true
    property real meterTickMs: 0.0
    property bool meterDecayActive: false
    property real timelineZoom: 1.0
    property real timelineStartRatio: 0.0
    property var clipVerticalZooms: ({})
    property bool clipDragGhostVisible: false
    property real clipDragGhostX: 0.0
    property real clipDragGhostY: 0.0
    property real clipDragGhostWidth: 1.0
    property real clipDragGhostHeight: 1.0
    property real clipDragGhostVerticalZoom: 1.0
    property real clipDragGhostOpacity: 0.62
    property string clipDragGhostTitle: ""
    property var clipDragGhostWaveformState: ({})
    property bool clipPreviewActive: false
    property string clipPreviewLaneId: ""
    property string clipPreviewClipId: ""
    property real clipPreviewStartRatio: 0.0
    property real clipPreviewEndRatio: 0.0
    property bool pendingClipDropVisible: false
    property bool pendingClipDropCopyClip: false
    property string pendingClipDropSourceLaneId: ""
    property string pendingClipDropSourceClipId: ""
    property real pendingClipDropX: 0.0
    property real pendingClipDropY: 0.0
    property real pendingClipDropWidth: 1.0
    property real pendingClipDropHeight: 1.0
    property string pendingClipDropTitle: ""
    property var pendingClipDropWaveformState: ({})
    property real pendingClipDropVerticalZoom: 1.0
    property bool timelineSelectionVisible: false
    property bool timelineSelectionDragActive: false
    property real timelineSelectionStartRatio: 0.0
    property real timelineSelectionEndRatio: 0.0
    property int timelineSelectionStartLaneIndex: -1
    property int timelineSelectionEndLaneIndex: -1
    property bool timelineSelectionTrackHighlightActive: false
    property int timelineSelectionTrackHighlightStartLaneIndex: -1
    property int timelineSelectionTrackHighlightEndLaneIndex: -1
    property int timelineSelectionAutoScrollDirection: 0
    property real timelineSelectionDragX: 0.0
    property real timelineSelectionDragViewportWidth: 1.0
    property real timelineSelectionDragRootY: 0.0
    property bool timelineSelectionReleaseGuardActive: false
    readonly property real meterFloorDb: -72.0
    readonly property real meterZeroNormalized: 2.0 / 3.0
    readonly property real meterReleaseDbPerSecond: Math.abs(meterFloorDb)
    readonly property real meterSilenceThresholdLevel: Math.pow(10.0, (meterFloorDb + 3.0) / 20.0)
    readonly property int laneMeterSegmentCount: 28
    readonly property real laneMeterSegmentGap: 0.5
    readonly property real timelineMinZoom: 1.0
    readonly property real timelineMaxZoom: 64.0
    readonly property real timelineVisibleRatio: 1.0 / Math.max(timelineMinZoom, timelineZoom)
    readonly property real clipEdgeHandleWidth: 7.0
    readonly property int laneHeight: 72

    function timelineContentWidth(viewportWidth) {
        return Math.max(1, viewportWidth)
    }

    function clamp(value, minValue, maxValue) {
        return Math.max(minValue, Math.min(maxValue, value))
    }

    function timelineRatioForX(x, viewportWidth) {
        return clamp(
            timelineStartRatio + (x / Math.max(1, viewportWidth)) * timelineVisibleRatio,
            0.0,
            1.0)
    }

    function timelineXForRatio(ratio, viewportWidth) {
        return ((ratio - timelineStartRatio) / Math.max(0.0001, timelineVisibleRatio)) * Math.max(1, viewportWidth)
    }

    function timelineWidthForRatio(ratio, viewportWidth) {
        return Math.max(1, (ratio / Math.max(0.0001, timelineVisibleRatio)) * Math.max(1, viewportWidth))
    }

    function clampTimelineStart() {
        timelineStartRatio = clamp(timelineStartRatio, 0.0, Math.max(0.0, 1.0 - timelineVisibleRatio))
    }

    function zoomTimelineAt(x, viewportWidth, wheelDelta) {
        if (!nodeEditorController.showTimeline || wheelDelta === 0)
            return

        var anchorRatio = timelineRatioForX(x, viewportWidth)
        var anchorViewportRatio = clamp(x / Math.max(1, viewportWidth), 0.0, 1.0)
        var nextZoom = clamp(
            timelineZoom * (wheelDelta > 0 ? 1.18 : (1.0 / 1.18)),
            timelineMinZoom,
            timelineMaxZoom)
        if (Math.abs(nextZoom - timelineZoom) < 0.001)
            return

        var nextVisibleRatio = 1.0 / nextZoom
        timelineZoom = nextZoom
        timelineStartRatio = clamp(
            anchorRatio - anchorViewportRatio * nextVisibleRatio,
            0.0,
            Math.max(0.0, 1.0 - nextVisibleRatio))
    }

    function clipTrimEdgeAt(localX, clipWidth) {
        if (clipWidth <= 0)
            return 0
        var edgeWidth = Math.min(clipEdgeHandleWidth, clipWidth * 0.5)
        var edgeSlack = clipWidth > 6 ? 2 : 1
        if (localX <= edgeWidth + edgeSlack)
            return -1
        if (localX >= clipWidth - edgeWidth - edgeSlack)
            return 1
        return 0
    }

    function laneIdForRootY(rootY, fallbackLaneId) {
        var index = laneIndexForRootY(rootY)
        var tracks = nodeEditorController.nodeTracks || []
        if (index >= 0 && index < tracks.length && tracks[index].laneId !== undefined)
            return tracks[index].laneId
        return fallbackLaneId
    }

    function laneIndexForRootY(rootY) {
        if (!lanesColumn)
            return -1

        var tracks = nodeEditorController.nodeTracks || []
        if (tracks.length <= 0)
            return -1

        var local = root.mapToItem(lanesColumn, 0, rootY)
        var laneStep = laneHeight + lanesColumn.spacing
        var totalHeight = tracks.length * laneStep - lanesColumn.spacing
        if (local.y < 0 || local.y >= Math.max(1, totalHeight))
            return -1
        var index = Math.floor(local.y / Math.max(1, laneStep))
        return index
    }

    function laneTopForIndex(index) {
        return index * (laneHeight + lanesColumn.spacing)
    }

    function timelineSelectionTopLaneIndex() {
        return Math.min(timelineSelectionStartLaneIndex, timelineSelectionEndLaneIndex)
    }

    function timelineSelectionBottomLaneIndex() {
        return Math.max(timelineSelectionStartLaneIndex, timelineSelectionEndLaneIndex)
    }

    function laneWithinTimelineSelection(laneIndex) {
        if (!timelineSelectionVisible
            || timelineSelectionStartLaneIndex < 0
            || timelineSelectionEndLaneIndex < 0)
            return false

        return laneIndex >= timelineSelectionTopLaneIndex()
            && laneIndex <= timelineSelectionBottomLaneIndex()
    }

    function clearTimelineSelectionTrackHighlight() {
        timelineSelectionTrackHighlightActive = false
        timelineSelectionTrackHighlightStartLaneIndex = -1
        timelineSelectionTrackHighlightEndLaneIndex = -1
    }

    function updateTimelineSelectionTrackHighlight(startLaneIndex, endLaneIndex) {
        var tracks = nodeEditorController.nodeTracks || []
        if (tracks.length <= 0
            || startLaneIndex < 0
            || startLaneIndex >= tracks.length) {
            clearTimelineSelectionTrackHighlight()
            return
        }

        timelineSelectionTrackHighlightActive = true
        timelineSelectionTrackHighlightStartLaneIndex = startLaneIndex
        if (endLaneIndex >= 0 && endLaneIndex < tracks.length)
            timelineSelectionTrackHighlightEndLaneIndex = endLaneIndex
        else
            timelineSelectionTrackHighlightEndLaneIndex = -1
    }

    function laneWithinTimelineSelectionTrackHighlight(laneIndex) {
        if (!timelineSelectionTrackHighlightActive
            || timelineSelectionTrackHighlightStartLaneIndex < 0
            || timelineSelectionTrackHighlightEndLaneIndex < 0)
            return false

        var topLaneIndex = Math.min(
            timelineSelectionTrackHighlightStartLaneIndex,
            timelineSelectionTrackHighlightEndLaneIndex)
        var bottomLaneIndex = Math.max(
            timelineSelectionTrackHighlightStartLaneIndex,
            timelineSelectionTrackHighlightEndLaneIndex)
        return laneIndex >= topLaneIndex && laneIndex <= bottomLaneIndex
    }

    function timelineSelectionVisualStartLaneIndex() {
        if (timelineSelectionDragActive && timelineSelectionTrackHighlightActive)
            return timelineSelectionTrackHighlightStartLaneIndex
        return timelineSelectionStartLaneIndex
    }

    function timelineSelectionVisualEndLaneIndex() {
        if (timelineSelectionDragActive && timelineSelectionTrackHighlightActive)
            return timelineSelectionTrackHighlightEndLaneIndex
        return timelineSelectionEndLaneIndex
    }

    function syncDragPlayheadToRatio(ratio) {
        if (!nodeEditorController.showTimeline)
            return

        nodeEditorController.setPlayheadFromRatio(clamp(ratio, 0.0, 1.0))
    }

    function hideClipDragGhost() {
        clipDragGhostVisible = false
        clipDragGhostWaveformState = ({})
    }

    function showClipDragGhost(x, y, width, height, title, waveformState, verticalZoom, opacity) {
        clipDragGhostX = x
        clipDragGhostY = y
        clipDragGhostWidth = Math.max(1, width)
        clipDragGhostHeight = Math.max(1, height)
        clipDragGhostTitle = title || ""
        clipDragGhostWaveformState = waveformState
        clipDragGhostVerticalZoom = verticalZoom
        clipDragGhostOpacity = opacity === undefined ? 0.62 : opacity
        clipDragGhostVisible = true
    }

    function clipZoomKey(laneId, clipId) {
        return String(laneId) + ":" + String(clipId)
    }

    function clipVerticalZoomForKey(key) {
        var value = clipVerticalZooms[key]
        return value === undefined ? 1.0 : value
    }

    function setClipVerticalZoom(key, zoom) {
        var nextZoom = clamp(zoom, 0.5, 8.0)
        if (Math.abs(clipVerticalZoomForKey(key) - nextZoom) < 0.001)
            return

        var next = {}
        for (var existingKey in clipVerticalZooms)
            next[existingKey] = clipVerticalZooms[existingKey]
        next[key] = nextZoom
        clipVerticalZooms = next
    }

    function clearClipPreview() {
        clipPreviewActive = false
        clipPreviewLaneId = ""
        clipPreviewClipId = ""
        clipPreviewStartRatio = 0.0
        clipPreviewEndRatio = 0.0
    }

    function clearPendingClipDrop() {
        pendingClipDropVisible = false
        pendingClipDropCopyClip = false
        pendingClipDropSourceLaneId = ""
        pendingClipDropSourceClipId = ""
        pendingClipDropX = 0.0
        pendingClipDropY = 0.0
        pendingClipDropWidth = 1.0
        pendingClipDropHeight = 1.0
        pendingClipDropTitle = ""
        pendingClipDropWaveformState = ({})
        pendingClipDropVerticalZoom = 1.0
    }

    function showPendingClipDrop(
        sourceLaneId,
        sourceClipId,
        copyClip,
        x,
        y,
        width,
        height,
        title,
        waveformState,
        verticalZoom) {
        pendingClipDropSourceLaneId = String(sourceLaneId)
        pendingClipDropSourceClipId = String(sourceClipId)
        pendingClipDropCopyClip = !!copyClip
        pendingClipDropX = x
        pendingClipDropY = y
        pendingClipDropWidth = Math.max(1, width)
        pendingClipDropHeight = Math.max(1, height)
        pendingClipDropTitle = title || ""
        pendingClipDropWaveformState = waveformState || ({})
        pendingClipDropVerticalZoom = verticalZoom === undefined ? 1.0 : verticalZoom
        pendingClipDropVisible = true
    }

    function isPendingMoveSourceClip(laneId, clipId) {
        return pendingClipDropVisible
            && !pendingClipDropCopyClip
            && pendingClipDropSourceLaneId === String(laneId)
            && pendingClipDropSourceClipId === String(clipId)
    }

    function clearTimelineSelection(updateController) {
        timelineSelectionVisible = false
        timelineSelectionDragActive = false
        timelineSelectionReleaseGuardActive = false
        timelineSelectionStartRatio = 0.0
        timelineSelectionEndRatio = 0.0
        timelineSelectionStartLaneIndex = -1
        timelineSelectionEndLaneIndex = -1
        clearTimelineSelectionTrackHighlight()
        stopTimelineSelectionAutoScroll()
        if (updateController === undefined || updateController)
            nodeEditorController.clearTimelineSelectionState()
    }

    function syncTimelineSelectionFromController() {
        var keepDragActive = timelineSelectionDragActive
        if (!nodeEditorController.hasTimelineSelection) {
            if (keepDragActive && timelineSelectionVisible)
                return
            clearTimelineSelection(false)
            return
        }

        var durationMs = Math.max(1, nodeEditorController.nodeDurationMs)
        timelineSelectionVisible = true
        timelineSelectionDragActive = keepDragActive
        timelineSelectionStartRatio = clamp(nodeEditorController.timelineSelectionStartMs / durationMs, 0.0, 1.0)
        timelineSelectionEndRatio = clamp(nodeEditorController.timelineSelectionEndMs / durationMs, 0.0, 1.0)
        timelineSelectionStartLaneIndex = nodeEditorController.timelineSelectionStartLaneIndex
        timelineSelectionEndLaneIndex = nodeEditorController.timelineSelectionEndLaneIndex
        if (!keepDragActive)
            clearTimelineSelectionTrackHighlight()
        if (!keepDragActive)
            stopTimelineSelectionAutoScroll()
    }

    function beginTimelineSelection(startRatio, startLaneIndex, highlightTracks) {
        var tracks = nodeEditorController.nodeTracks || []
        if (tracks.length <= 0)
            return false
        var laneStartValid = startLaneIndex >= 0 && startLaneIndex < tracks.length
        if (highlightTracks && !laneStartValid)
            return false
        var normalizedLaneIndex = laneStartValid
            ? startLaneIndex
            : clamp(startLaneIndex, 0, tracks.length - 1)
        var normalizedRatio = clamp(startRatio, 0.0, 1.0)
        timelineSelectionVisible = true
        timelineSelectionDragActive = true
        timelineSelectionStartRatio = normalizedRatio
        timelineSelectionEndRatio = normalizedRatio
        timelineSelectionStartLaneIndex = normalizedLaneIndex
        timelineSelectionEndLaneIndex = normalizedLaneIndex
        if (highlightTracks)
            updateTimelineSelectionTrackHighlight(normalizedLaneIndex, normalizedLaneIndex)
        else
            clearTimelineSelectionTrackHighlight()
        return true
    }

    function updateTimelineSelection(endRatio, endLaneIndex) {
        var tracks = nodeEditorController.nodeTracks || []
        if (!timelineSelectionDragActive || tracks.length <= 0)
            return
        var normalizedEndRatio = clamp(endRatio, 0.0, 1.0)
        timelineSelectionEndRatio = normalizedEndRatio
        if (timelineSelectionTrackHighlightActive)
            updateTimelineSelectionTrackHighlight(timelineSelectionTrackHighlightStartLaneIndex, endLaneIndex)
        if (endLaneIndex >= 0 && endLaneIndex < tracks.length)
            timelineSelectionEndLaneIndex = endLaneIndex
        nodeEditorController.setTimelineSelectionState(
            true,
            timelineSelectionStartRatio,
            timelineSelectionEndRatio,
            timelineSelectionStartLaneIndex,
            timelineSelectionEndLaneIndex)
        nodeEditorController.setPlayheadFromRatio(normalizedEndRatio)
    }

    function finishTimelineSelection() {
        timelineSelectionDragActive = false
        clearTimelineSelectionTrackHighlight()
        stopTimelineSelectionAutoScroll()
    }

    function timelineSelectionMinRatio() {
        return Math.min(timelineSelectionStartRatio, timelineSelectionEndRatio)
    }

    function timelineSelectionMaxRatio() {
        return Math.max(timelineSelectionStartRatio, timelineSelectionEndRatio)
    }

    function timelineSelectionIntersectsClip(laneIndex, clipStartRatio, clipEndRatio) {
        if (!timelineSelectionVisible
            || timelineSelectionVisualStartLaneIndex() < 0
            || timelineSelectionVisualEndLaneIndex() < 0)
            return false

        var topLaneIndex = Math.min(
            timelineSelectionVisualStartLaneIndex(),
            timelineSelectionVisualEndLaneIndex())
        var bottomLaneIndex = Math.max(
            timelineSelectionVisualStartLaneIndex(),
            timelineSelectionVisualEndLaneIndex())
        if (laneIndex < topLaneIndex || laneIndex > bottomLaneIndex)
            return false

        var selectionStartRatio = timelineSelectionMinRatio()
        var selectionEndRatio = timelineSelectionMaxRatio()
        return clipEndRatio > selectionStartRatio && clipStartRatio < selectionEndRatio
    }

    function timelineSelectionClipLocalStartRatio(laneIndex, clipStartRatio, clipEndRatio) {
        if (!timelineSelectionIntersectsClip(laneIndex, clipStartRatio, clipEndRatio))
            return 0.0
        var overlapStartRatio = Math.max(timelineSelectionMinRatio(), clipStartRatio)
        return clamp(
            (overlapStartRatio - clipStartRatio) / Math.max(0.0001, clipEndRatio - clipStartRatio),
            0.0,
            1.0)
    }

    function timelineSelectionClipLocalEndRatio(laneIndex, clipStartRatio, clipEndRatio) {
        if (!timelineSelectionIntersectsClip(laneIndex, clipStartRatio, clipEndRatio))
            return 0.0
        var overlapEndRatio = Math.min(timelineSelectionMaxRatio(), clipEndRatio)
        return clamp(
            (overlapEndRatio - clipStartRatio) / Math.max(0.0001, clipEndRatio - clipStartRatio),
            0.0,
            1.0)
    }

    function updateTimelineSelectionAutoScroll(localX, viewportWidth, rootY) {
        timelineSelectionDragX = localX
        timelineSelectionDragViewportWidth = Math.max(1, viewportWidth)
        timelineSelectionDragRootY = rootY
        var maxStart = Math.max(0.0, 1.0 - timelineVisibleRatio)
        var edgeMargin = 28
        if (!timelineSelectionDragActive || maxStart <= 0.0) {
            timelineSelectionAutoScrollDirection = 0
            return
        }
        if (localX <= edgeMargin && timelineStartRatio > 0.0001) {
            timelineSelectionAutoScrollDirection = -1
        } else if (localX >= viewportWidth - edgeMargin && timelineStartRatio < maxStart - 0.0001) {
            timelineSelectionAutoScrollDirection = 1
        } else {
            timelineSelectionAutoScrollDirection = 0
        }
    }

    function stopTimelineSelectionAutoScroll() {
        timelineSelectionAutoScrollDirection = 0
    }

    function setClipPreview(laneId, clipId, startRatio, endRatio) {
        clipPreviewActive = true
        clipPreviewLaneId = String(laneId)
        clipPreviewClipId = String(clipId)
        clipPreviewStartRatio = clamp(startRatio, 0.0, 1.0)
        clipPreviewEndRatio = clamp(Math.max(startRatio, endRatio), 0.0, 1.0)
    }

    function effectiveClipStartRatio(laneId, clipId, fallbackStartRatio) {
        if (clipPreviewActive
            && clipPreviewLaneId === String(laneId)
            && clipPreviewClipId === String(clipId))
            return clipPreviewStartRatio
        return fallbackStartRatio
    }

    function effectiveClipEndRatio(laneId, clipId, fallbackEndRatio) {
        if (clipPreviewActive
            && clipPreviewLaneId === String(laneId)
            && clipPreviewClipId === String(clipId))
            return clipPreviewEndRatio
        return fallbackEndRatio
    }

    function clipIsPreviewTarget(laneId, clipId) {
        return clipPreviewActive
            && clipPreviewLaneId === String(laneId)
            && clipPreviewClipId === String(clipId)
    }

    function clipHasLeftOverlap(laneId, clips, clipId, startRatio, endRatio) {
        var effectiveStartRatio = effectiveClipStartRatio(laneId, clipId, startRatio)
        var effectiveEndRatio = effectiveClipEndRatio(laneId, clipId, endRatio)
        var laneClips = clips || []
        for (var i = 0; i < laneClips.length; ++i) {
            var otherClip = laneClips[i]
            if (!otherClip || otherClip.clipId === clipId)
                continue

            var otherStartRatio = otherClip.clipOffsetRatio || 0
            var otherWidthRatio = Math.max(0.0, otherClip.clipWidthRatio || 0.0)
            var otherEndRatio = Math.min(1.0, otherStartRatio + otherWidthRatio)
            otherStartRatio = effectiveClipStartRatio(laneId, otherClip.clipId, otherStartRatio)
            otherEndRatio = effectiveClipEndRatio(laneId, otherClip.clipId, otherEndRatio)
            if (otherStartRatio < effectiveStartRatio && otherEndRatio > effectiveStartRatio && effectiveEndRatio > effectiveStartRatio)
                return true
        }

        return false
    }

    function clipHasRightOverlap(laneId, clips, clipId, startRatio, endRatio) {
        var effectiveStartRatio = effectiveClipStartRatio(laneId, clipId, startRatio)
        var effectiveEndRatio = effectiveClipEndRatio(laneId, clipId, endRatio)
        var laneClips = clips || []
        for (var i = 0; i < laneClips.length; ++i) {
            var otherClip = laneClips[i]
            if (!otherClip || otherClip.clipId === clipId)
                continue

            var otherStartRatio = otherClip.clipOffsetRatio || 0
            var otherWidthRatio = Math.max(0.0, otherClip.clipWidthRatio || 0.0)
            var otherEndRatio = Math.min(1.0, otherStartRatio + otherWidthRatio)
            otherStartRatio = effectiveClipStartRatio(laneId, otherClip.clipId, otherStartRatio)
            otherEndRatio = effectiveClipEndRatio(laneId, otherClip.clipId, otherEndRatio)
            if (otherStartRatio < effectiveEndRatio && otherEndRatio > effectiveEndRatio && effectiveEndRatio > effectiveStartRatio)
                return true
        }

        return false
    }

    onTimelineZoomChanged: clampTimelineStart()

    function levelToMeterDb(level) {
        if (level <= 0.00001)
            return meterFloorDb
        return Math.max(meterFloorDb, Math.min(12.0, 20.0 * Math.log(level) / Math.LN10))
    }

    function meterDbToLevel(db) {
        if (db <= meterFloorDb + 0.001)
            return 0.0
        return Math.pow(10.0, db / 20.0)
    }

    function meterPositionForDb(db) {
        var clampedDb = Math.max(meterFloorDb, Math.min(12.0, db))
        if (clampedDb <= 0.0)
            return clamp(((clampedDb - meterFloorDb) / (0.0 - meterFloorDb)) * meterZeroNormalized, 0.0, meterZeroNormalized)
        return clamp(meterZeroNormalized + (clampedDb / 12.0) * (1.0 - meterZeroNormalized), meterZeroNormalized, 1.0)
    }

    function meterNormalized(level) {
        return level <= 0.0 ? 0.0 : meterPositionForDb(levelToMeterDb(level))
    }

    function meterDbAtPosition(position) {
        var clampedPosition = clamp(position, 0.0, 1.0)
        if (clampedPosition <= meterZeroNormalized)
            return meterFloorDb + (clampedPosition / meterZeroNormalized) * (0.0 - meterFloorDb)
        return ((clampedPosition - meterZeroNormalized) / (1.0 - meterZeroNormalized)) * 12.0
    }

    function meterSegmentColor(position) {
        var db = meterDbAtPosition(position)
        if (db >= 0.0)
            return "#ff5b4d"
        if (db >= -9.0)
            return "#ffb33b"
        return "#2fe06d"
    }

    function nextDisplayedMeterLevel(displayedLevel, sourceLevel, deltaSeconds) {
        var effectiveSourceLevel = sourceLevel >= meterSilenceThresholdLevel ? sourceLevel : 0.0
        if (effectiveSourceLevel >= displayedLevel)
            return effectiveSourceLevel
        var displayedDb = levelToMeterDb(displayedLevel)
        var sourceDb = levelToMeterDb(effectiveSourceLevel)
        return meterDbToLevel(Math.max(sourceDb, displayedDb - meterReleaseDbPerSecond * deltaSeconds))
    }

    AppTheme {
        id: theme
    }

    Timer {
        interval: 16
        repeat: true
        running: root.visible
            && nodeEditorController.nodeTrackCount > 0
            && (nodeEditorController.playbackActive || root.meterDecayActive)
        onTriggered: root.meterTickMs = Date.now()
    }

    Timer {
        id: meterDecayStopTimer

        interval: 1100
        repeat: false
        onTriggered: root.meterDecayActive = false
    }

    Timer {
        id: timelineSelectionAutoScrollTimer

        interval: 16
        repeat: true
        running: root.timelineSelectionDragActive && root.timelineSelectionAutoScrollDirection !== 0
        onTriggered: {
            var maxStart = Math.max(0.0, 1.0 - root.timelineVisibleRatio)
            if (maxStart <= 0.0) {
                root.stopTimelineSelectionAutoScroll()
                return
            }
            var nextStart = root.clamp(
                root.timelineStartRatio + root.timelineSelectionAutoScrollDirection * root.timelineVisibleRatio * 0.03,
                0.0,
                maxStart)
            if (Math.abs(nextStart - root.timelineStartRatio) < 0.0001) {
                root.stopTimelineSelectionAutoScroll()
                return
            }
            root.timelineStartRatio = nextStart
            root.updateTimelineSelection(
                root.timelineRatioForX(root.timelineSelectionDragX, root.timelineSelectionDragViewportWidth),
                root.laneIndexForRootY(root.timelineSelectionDragRootY))
        }
    }

    Connections {
        target: nodeEditorController

        function onPlaybackStateChanged() {
            if (nodeEditorController.playbackActive) {
                meterDecayStopTimer.stop()
                root.meterDecayActive = false
                return
            }

            root.meterDecayActive = true
            meterDecayStopTimer.restart()
        }

        function onTimelineSelectionChanged() {
            root.syncTimelineSelectionFromController()
        }

        function onTimelineSelectionCleared() {
            root.clearTimelineSelection(false)
        }
    }

    Timer {
        interval: 500
        repeat: true
        running: nodeEditorController.showTimeline
            && nodeEditorController.nodeTrackCount > 0
        onTriggered: root.editMarkerBlinkOn = !root.editMarkerBlinkOn
        onRunningChanged: {
            if (running)
                root.editMarkerBlinkOn = true
        }
    }

    function closeMenu() {
        activeMenuKind = ""
        activeMenuItems = []
        activeMenuAnchor = null
    }

    function openMenu(kind, items, anchorItem) {
        if (menuPopup.opened && activeMenuKind === kind && activeMenuAnchor === anchorItem) {
            menuPopup.close()
            closeMenu()
            return
        }

        activeMenuKind = kind
        activeMenuItems = items
        activeMenuAnchor = anchorItem
        var targetParent = menuPopup.parent ? menuPopup.parent : root
        var local = anchorItem.mapToItem(targetParent, 0, anchorItem.height + 4)
        var popupWidth = Math.max(220, menuPopup.implicitWidth)
        var popupHeight = menuPopup.implicitHeight
        var maxX = Math.max(0, targetParent.width - popupWidth - 8)
        var maxY = Math.max(0, targetParent.height - popupHeight - 8)
        menuPopup.popupX = Math.max(8, Math.min(maxX, local.x))
        menuPopup.popupY = Math.max(8, Math.min(maxY, local.y))
        menuPopup.open()
    }

    function triggerMenuAction(actionKey) {
        var menuKind = activeMenuKind
        menuPopup.close()
        closeMenu()

        if (menuKind === "file") {
            mainWindowBridge.requestNodeEditorFileAction(actionKey)
        } else if (menuKind === "edit") {
            mainWindowBridge.requestNodeEditorEditAction(actionKey)
        } else if (menuKind === "audio" || menuKind === "track") {
            mainWindowBridge.requestNodeEditorAudioAction(actionKey)
        }
    }

    function menuItemEnabled(item) {
        if (!item)
            return false
        switch (item.key) {
        case "open":
            return nodeEditorController.canOpenNode
        case "save":
            return nodeEditorController.canSaveNode
        case "saveAs":
            return nodeEditorController.canSaveNodeAs
        case "export":
            return nodeEditorController.canExportNode
        case "copyClip":
        case "cutClip":
            return nodeEditorController.selectedClipId !== ""
                || nodeEditorController.hasTimelineSelection
        case "splitClip":
            return nodeEditorController.selectedClipId !== ""
        case "pasteClip":
            return nodeEditorController.canPasteClip
        case "createTrack":
        case "newLane":
            return nodeEditorController.hasSelection
        case "import":
            return nodeEditorController.hasSelection
        default:
            return item.enabled !== false
        }
    }

    function pointInsideItem(item, targetParent, x, y) {
        if (!item || !targetParent)
            return false
        var topLeft = item.mapToItem(targetParent, 0, 0)
        return x >= topLeft.x
            && x <= topLeft.x + item.width
            && y >= topLeft.y
            && y <= topLeft.y + item.height
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 40
            color: "#10151c"
            border.width: 1
            border.color: "#1e2732"

            Row {
                anchors.left: parent.left
                anchors.leftMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                spacing: 4

                ToolButton {
                    id: fileMenuButton
                    text: "File"
                    hoverEnabled: true

                    onClicked: {
                        root.openMenu("file", [
                            { key: "open", text: "Open Node..." },
                            { key: "save", text: "Save Node" },
                            { key: "saveAs", text: "Save Node As..." },
                            { key: "export", text: "Export Node..." }
                        ], fileMenuButton)
                    }

                    contentItem: Label {
                        text: fileMenuButton.text
                        color: theme.titleText
                        font.pixelSize: 13
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    background: Rectangle {
                        radius: 6
                        color: fileMenuButton.down
                            ? theme.pressedFill
                            : (fileMenuButton.hovered ? theme.hoverFill : "transparent")
                    }
                }

                ToolButton {
                    id: editMenuButton
                    text: "Edit"
                    hoverEnabled: true

                    onClicked: {
                        root.openMenu("edit", [
                            { key: "copyClip", text: "Copy Clip    Ctrl+C" },
                            { key: "cutClip", text: "Cut To Clipboard    Ctrl+X" },
                            { key: "splitClip", text: "Cut Clip    Ctrl+E" },
                            { key: "pasteClip", text: "Paste Clip    Ctrl+V" }
                        ], editMenuButton)
                    }

                    contentItem: Label {
                        text: editMenuButton.text
                        color: theme.titleText
                        font.pixelSize: 13
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    background: Rectangle {
                        radius: 6
                        color: editMenuButton.down
                            ? theme.pressedFill
                            : (editMenuButton.hovered ? theme.hoverFill : "transparent")
                    }
                }

                ToolButton {
                    id: trackMenuButton
                    text: "Track"
                    hoverEnabled: true

                    onClicked: {
                        root.openMenu("track", [
                            { key: "createTrack", text: "Create Track" }
                        ], trackMenuButton)
                    }

                    contentItem: Label {
                        text: trackMenuButton.text
                        color: theme.titleText
                        font.pixelSize: 13
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    background: Rectangle {
                        radius: 6
                        color: trackMenuButton.down
                            ? theme.pressedFill
                            : (trackMenuButton.hovered ? theme.hoverFill : "transparent")
                    }
                }

                ToolButton {
                    id: audioMenuButton
                    text: "Audio"
                    hoverEnabled: true

                    onClicked: {
                        root.openMenu("audio", [
                            { key: "import", text: "Import Audio..." }
                        ], audioMenuButton)
                    }

                    contentItem: Label {
                        text: audioMenuButton.text
                        color: theme.titleText
                        font.pixelSize: 13
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }

                    background: Rectangle {
                        radius: 6
                        color: audioMenuButton.down
                            ? theme.pressedFill
                            : (audioMenuButton.hovered ? theme.hoverFill : "transparent")
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                width: Math.max(120, parent.width - 300)
                text: nodeEditorController.selectedNodeName
                color: theme.titleText
                font.pixelSize: 14
                font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignHCenter
                elide: Text.ElideRight
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.margins: 12
            spacing: 0

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 0
                color: "#0b1016"

                ColumnLayout {
                    id: lanesPanelColumn
                    anchors.fill: parent
                    spacing: 0

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 28
                        color: "#101720"

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: root.laneHeaderWidth + 10
                            anchors.rightMargin: 10
                            spacing: 12

                            Text {
                                Layout.alignment: Qt.AlignVCenter
                                text: nodeEditorController.timelineStartText
                                color: "#7f8b99"
                                font.pixelSize: 11
                            }

                            Item {
                                Layout.fillWidth: true
                            }

                            Text {
                                Layout.alignment: Qt.AlignVCenter
                                text: nodeEditorController.playheadText
                                color: "#9ec7f0"
                                font.pixelSize: 11
                                font.weight: Font.DemiBold
                            }

                            Item {
                                Layout.fillWidth: true
                            }

                            Text {
                                Layout.alignment: Qt.AlignVCenter
                                text: nodeEditorController.timelineEndText
                                color: "#7f8b99"
                                font.pixelSize: 11
                            }
                        }

                        MouseArea {
                            x: root.laneHeaderWidth
                            y: 0
                            width: Math.max(1, parent.width - root.laneHeaderWidth)
                            height: parent.height
                            enabled: nodeEditorController.showTimeline
                            acceptedButtons: Qt.LeftButton
                            preventStealing: true
                            property real pressX: 0.0
                            property bool selectionStarted: false
                            onPressed: function(mouse) {
                                pressX = mouse.x
                                selectionStarted = false
                                root.timelineSelectionReleaseGuardActive = false
                                root.forceActiveFocus()
                                mouse.accepted = true
                            }
                            onPositionChanged: function(mouse) {
                                if (!pressed)
                                    return
                                if (!selectionStarted && Math.abs(mouse.x - pressX) < 3)
                                    return
                                if (!selectionStarted) {
                                    selectionStarted = root.beginTimelineSelection(
                                        root.timelineRatioForX(pressX, width),
                                        0,
                                        false)
                                    if (!selectionStarted)
                                        return
                                }
                                root.updateTimelineSelection(
                                    root.timelineRatioForX(mouse.x, width),
                                    Math.max(0, (nodeEditorController.nodeTracks || []).length - 1))
                                root.updateTimelineSelectionAutoScroll(
                                    mouse.x,
                                    width,
                                    mapToItem(root, mouse.x, mouse.y).y)
                                mouse.accepted = true
                            }
                            onReleased: function(mouse) {
                                if (root.timelineSelectionReleaseGuardActive) {
                                    root.timelineSelectionReleaseGuardActive = false
                                    mouse.accepted = true
                                    return
                                }
                                if (selectionStarted) {
                                    root.updateTimelineSelection(
                                        root.timelineRatioForX(mouse.x, width),
                                        Math.max(0, (nodeEditorController.nodeTracks || []).length - 1))
                                    root.finishTimelineSelection()
                                    root.stopTimelineSelectionAutoScroll()
                                    mouse.accepted = true
                                    return
                                }
                                root.clearTimelineSelection()
                                root.forceActiveFocus()
                                nodeEditorController.setPlayheadFromRatio(root.timelineRatioForX(mouse.x, width))
                                mouse.accepted = true
                            }
                            onCanceled: {
                                selectionStarted = false
                                root.stopTimelineSelectionAutoScroll()
                                if (root.timelineSelectionDragActive) {
                                    root.timelineSelectionReleaseGuardActive = true
                                    root.finishTimelineSelection()
                                }
                            }
                            onWheel: function(wheel) {
                                root.forceActiveFocus()
                                var delta = wheel.angleDelta.y !== 0 ? wheel.angleDelta.y : wheel.angleDelta.x
                                root.zoomTimelineAt(wheel.x, width, delta)
                                wheel.accepted = true
                            }
                        }
                    }

                    ScrollView {
                        id: lanesScroll
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        contentWidth: root.timelineContentWidth(width)
                        contentHeight: lanesContent.height
                        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                        ScrollBar.vertical.policy: ScrollBar.AsNeeded

                        Item {
                            id: lanesContent
                            width: lanesScroll.contentWidth
                            height: lanesColumn.implicitHeight

                            Column {
                                id: lanesColumn
                                width: parent.width
                                spacing: 1

                                Repeater {
                                    model: nodeEditorController.nodeTracks

                                    delegate: Rectangle {
                                        id: laneDelegate
                                        required property var modelData
                                        readonly property int laneIndex: index
                                        readonly property bool laneDirectlySelected: !root.timelineSelectionDragActive
                                            && (
                                                String(modelData.laneId) === String(nodeEditorController.selectedLaneHeaderId)
                                                || (
                                                    String(nodeEditorController.selectedClipId) !== ""
                                                    && String(modelData.laneId) === String(nodeEditorController.selectedLaneId)
                                                )
                                            )
                                        readonly property bool laneRangeSelected: root.timelineSelectionDragActive
                                            && root.laneWithinTimelineSelectionTrackHighlight(laneIndex)
                                        readonly property bool laneSelectionHighlighted: laneDirectlySelected || laneRangeSelected
                                        width: parent.width
                                        height: root.laneHeight
                                        color: "#121922"

                                        Row {
                                            anchors.fill: parent
                                            spacing: 0

                                            Rectangle {
                                                width: root.laneHeaderWidth
                                                height: parent.height
                                                color: laneDelegate.laneSelectionHighlighted
                                                    ? "#1b2733"
                                                    : "#151e28"
                                                border.width: 0
                                                border.color: "#9ec7f0"

                                                Text {
                                                    anchors.left: parent.left
                                                    anchors.right: parent.right
                                                    anchors.top: parent.top
                                                    anchors.leftMargin: 10
                                                    anchors.rightMargin: 72
                                                    anchors.topMargin: 12
                                                    text: modelData.title
                                                    color: "#eef2f6"
                                                    font.pixelSize: 13
                                                    font.weight: Font.Medium
                                                    elide: Text.ElideRight
                                                }

                                                Text {
                                                    anchors.left: parent.left
                                                    anchors.right: parent.right
                                                    anchors.top: parent.top
                                                    anchors.leftMargin: 10
                                                    anchors.rightMargin: 72
                                                    anchors.topMargin: 34
                                                    text: modelData.subtitle
                                                    color: "#91a0b0"
                                                    font.pixelSize: 11
                                                    elide: Text.ElideRight
                                                }

                                                Item {
                                                    id: laneVuMeter

                                                    anchors.right: laneButtonStack.left
                                                    anchors.rightMargin: 6
                                                    anchors.top: parent.top
                                                    anchors.topMargin: 4
                                                    anchors.bottom: parent.bottom
                                                    anchors.bottomMargin: 4
                                                    width: useStereoMeter ? 18 : 10
                                                    z: 10

                                                    property int meterToken: nodeEditorController.laneMeterToken
                                                    property int resetToken: nodeEditorController.meterResetToken
                                                    property bool useStereoMeter: {
                                                        var token = meterToken
                                                        return nodeEditorController.laneUsesStereoMeter(modelData.laneId)
                                                    }
                                                    property real sourceLeftLevel: {
                                                        var token = meterToken
                                                        return nodeEditorController.laneMeterLeftLevel(modelData.laneId)
                                                    }
                                                    property real sourceRightLevel: {
                                                        var token = meterToken
                                                        return nodeEditorController.laneMeterRightLevel(modelData.laneId)
                                                    }
                                                    property real displayedLeftLevel: 0.0
                                                    property real displayedRightLevel: 0.0
                                                    property real lastMeterTickMs: 0.0

                                                    onResetTokenChanged: {
                                                        displayedLeftLevel = 0.0
                                                        displayedRightLevel = 0.0
                                                        lastMeterTickMs = 0.0
                                                    }

                                                    Connections {
                                                        target: root

                                                        function onMeterTickMsChanged() {
                                                            var nowMs = root.meterTickMs > 0.0 ? root.meterTickMs : Date.now()
                                                            var deltaSeconds = laneVuMeter.lastMeterTickMs > 0.0
                                                                ? Math.max(0.001, (nowMs - laneVuMeter.lastMeterTickMs) / 1000.0)
                                                                : 0.016
                                                            laneVuMeter.lastMeterTickMs = nowMs
                                                            laneVuMeter.displayedLeftLevel = root.nextDisplayedMeterLevel(
                                                                laneVuMeter.displayedLeftLevel,
                                                                laneVuMeter.useStereoMeter ? laneVuMeter.sourceLeftLevel : Math.max(laneVuMeter.sourceLeftLevel, laneVuMeter.sourceRightLevel),
                                                                deltaSeconds)
                                                            if (laneVuMeter.useStereoMeter) {
                                                                laneVuMeter.displayedRightLevel = root.nextDisplayedMeterLevel(
                                                                    laneVuMeter.displayedRightLevel,
                                                                    laneVuMeter.sourceRightLevel,
                                                                    deltaSeconds)
                                                            } else {
                                                                laneVuMeter.displayedRightLevel = laneVuMeter.displayedLeftLevel
                                                            }
                                                        }
                                                    }

                                                    Row {
                                                        anchors.fill: parent
                                                        spacing: laneVuMeter.useStereoMeter ? 2 : 0

                                                        Repeater {
                                                            model: laneVuMeter.useStereoMeter ? 2 : 1

                                                            Item {
                                                                property int meterBarIndex: index
                                                                width: laneVuMeter.useStereoMeter ? 8 : laneVuMeter.width
                                                                height: laneVuMeter.height
                                                                clip: true

                                                                Rectangle {
                                                                    anchors.fill: parent
                                                                    color: "#0b1016"
                                                                    border.width: laneVuMeter.useStereoMeter ? 0 : 1
                                                                    border.color: "#1d2733"
                                                                }

                                                                Item {
                                                                    id: laneSegmentArea

                                                                    anchors.fill: parent
                                                                    anchors.topMargin: 2
                                                                    anchors.bottomMargin: 2
                                                                    anchors.leftMargin: 1
                                                                    anchors.rightMargin: 1

                                                                    readonly property real segmentHeight: Math.max(
                                                                        1,
                                                                        (height - ((root.laneMeterSegmentCount - 1) * root.laneMeterSegmentGap)) / root.laneMeterSegmentCount)
                                                                    readonly property real barLevel: parent.meterBarIndex === 0
                                                                        ? laneVuMeter.displayedLeftLevel
                                                                        : laneVuMeter.displayedRightLevel

                                                                    Repeater {
                                                                        model: root.laneMeterSegmentCount

                                                                        Rectangle {
                                                                            readonly property real segmentPosition: (index + 1) / root.laneMeterSegmentCount
                                                                            x: 0
                                                                            y: laneSegmentArea.height - ((index + 1) * height) - (index * root.laneMeterSegmentGap)
                                                                            width: laneSegmentArea.width
                                                                            height: laneSegmentArea.segmentHeight
                                                                            radius: 1
                                                                            color: root.meterSegmentColor(segmentPosition)
                                                                            opacity: segmentPosition <= root.meterNormalized(laneSegmentArea.barLevel) ? 1.0 : 0.12
                                                                        }
                                                                    }
                                                                }

                                                                Rectangle {
                                                                    width: parent.width
                                                                    height: 1
                                                                    color: "#b8c6d6"
                                                                    opacity: 0.45
                                                                    y: (laneSegmentArea.y + laneSegmentArea.height) - (root.meterZeroNormalized * laneSegmentArea.height)
                                                                }
                                                            }
                                                        }
                                                    }
                                                }

                                                Column {
                                                    id: laneButtonStack

                                                    anchors.right: parent.right
                                                    anchors.rightMargin: 8
                                                    anchors.verticalCenter: parent.verticalCenter
                                                    spacing: 4
                                                    z: 10

                                                    Rectangle {
                                                        id: laneSoloButton
                                                        width: 22
                                                        height: 18
                                                        radius: 4
                                                        color: modelData.soloed ? "#db7e26" : "#1b2129"
                                                        border.width: 1
                                                        border.color: modelData.soloed ? "#ffd7b4" : "#2c3540"

                                                        Text {
                                                            anchors.centerIn: parent
                                                            text: "S"
                                                            color: modelData.soloed ? "#fff4e9" : "#d6dfe9"
                                                            font.pixelSize: 10
                                                            font.bold: true
                                                        }

                                                        MouseArea {
                                                            anchors.fill: parent
                                                            preventStealing: true
                                                            cursorShape: Qt.PointingHandCursor
                                                            onClicked: function(mouse) {
                                                                root.forceActiveFocus()
                                                                nodeEditorController.setLaneSoloed(modelData.laneId, !modelData.soloed)
                                                                mouse.accepted = true
                                                            }
                                                        }
                                                    }

                                                    Rectangle {
                                                        id: laneMuteButton
                                                        width: 22
                                                        height: 18
                                                        radius: 4
                                                        color: modelData.muted ? "#2f3741" : "#1b2129"
                                                        border.width: 1
                                                        border.color: modelData.muted ? "#4b5661" : "#2c3540"

                                                        Text {
                                                            anchors.centerIn: parent
                                                            text: "M"
                                                            color: modelData.muted ? "#9ba6b1" : "#d6dfe9"
                                                            font.pixelSize: 10
                                                            font.bold: true
                                                        }

                                                        MouseArea {
                                                            anchors.fill: parent
                                                            preventStealing: true
                                                            cursorShape: Qt.PointingHandCursor
                                                            onClicked: function(mouse) {
                                                                root.forceActiveFocus()
                                                                nodeEditorController.setLaneMuted(modelData.laneId, !modelData.muted)
                                                                mouse.accepted = true
                                                            }
                                                        }
                                                    }
                                                }

                                                MouseArea {
                                                    anchors.fill: parent
                                                    z: 5
                                                    acceptedButtons: Qt.LeftButton
                                                    onClicked: {
                                                        root.forceActiveFocus()
                                                        root.clearTimelineSelection()
                                                        nodeEditorController.selectLaneHeader(modelData.laneId)
                                                    }
                                                }
                                            }

                                            Rectangle {
                                                id: laneTimeline
                                                width: parent.width - root.laneHeaderWidth
                                                height: parent.height
                                                color: "#0f151d"
                                                clip: true

                                                Rectangle {
                                                    x: 0
                                                    width: parent.width
                                                    height: 1
                                                    anchors.verticalCenter: parent.verticalCenter
                                                    color: "#273241"
                                                }

                                                Repeater {
                                                    model: 5

                                                    delegate: Rectangle {
                                                        width: 1
                                                        height: laneTimeline.height
                                                        x: Math.round((laneTimeline.width - width) * (index / 4))
                                                        color: "#1a2430"
                                                    }
                                                }

                                                Repeater {
                                                    model: modelData.clips || []

                                                    delegate: ClipWaveformQuickItem {
                                                        id: clipDelegate
                                                        required property var modelData
                                                        readonly property real clipStartRatio: modelData.clipOffsetRatio || 0
                                                        readonly property real clipWidthRatio: Math.max(0.0, modelData.clipWidthRatio || 0.08)
                                                        readonly property real clipEndRatio: Math.min(1.0, clipStartRatio + clipWidthRatio)
                                                        readonly property string clipZoomKey: root.clipZoomKey(
                                                            laneDelegate.modelData.laneId,
                                                            modelData.clipId)
                                                        readonly property real minimumClipRatio: Math.max(
                                                            1.0 / Math.max(1, nodeEditorController.nodeDurationMs),
                                                            1.0 / Math.max(1, laneTimeline.width))
                                                        readonly property real minimumStartRatio: Math.max(
                                                            0.0,
                                                            clipStartRatio - ((modelData.clipSourceStartMs || 0) / Math.max(1, nodeEditorController.nodeDurationMs)))
                                                        readonly property real maximumEndRatio: Math.min(
                                                            1.0,
                                                            clipEndRatio + (Math.max(
                                                                0,
                                                                (modelData.clipSourceDurationMs || 0) - (modelData.clipSourceEndMs || 0))
                                                                / Math.max(1, nodeEditorController.nodeDurationMs)))
                                                        readonly property real visualStartRatio: trimPreviewActive ? trimPreviewStartRatio : clipStartRatio
                                                        readonly property real visualEndRatio: trimPreviewActive ? trimPreviewEndRatio : clipEndRatio
                                                        readonly property int visualClipSourceStartMs: Math.round(root.clamp(
                                                            (modelData.clipSourceStartMs || 0)
                                                                + ((visualStartRatio - clipStartRatio) * nodeEditorController.nodeDurationMs),
                                                            0,
                                                            Math.max(0, (modelData.clipSourceDurationMs || 1) - 1)))
                                                        readonly property int visualClipSourceEndMs: Math.round(root.clamp(
                                                            (modelData.clipSourceStartMs || 0)
                                                                + ((visualEndRatio - clipStartRatio) * nodeEditorController.nodeDurationMs),
                                                            visualClipSourceStartMs + 1,
                                                            Math.max(visualClipSourceStartMs + 1, modelData.clipSourceDurationMs || 1)))
                                                        readonly property real baseX: Math.round(root.timelineXForRatio(clipStartRatio, laneTimeline.width))
                                                        readonly property real visualBaseX: Math.round(root.timelineXForRatio(visualStartRatio, laneTimeline.width))
                                                        readonly property real maxOffsetRatio: Math.max(0.0, 1.0 - clipWidthRatio)
                                                        readonly property bool leftEdgeOverlapped: root.clipHasLeftOverlap(
                                                            laneDelegate.modelData.laneId,
                                                            laneDelegate.modelData.clips,
                                                            modelData.clipId,
                                                            clipStartRatio,
                                                            clipEndRatio)
                                                        readonly property bool rightEdgeOverlapped: root.clipHasRightOverlap(
                                                            laneDelegate.modelData.laneId,
                                                            laneDelegate.modelData.clips,
                                                            modelData.clipId,
                                                            clipStartRatio,
                                                            clipEndRatio)
                                                        readonly property bool clipSelected: clipDelegate.modelData.clipId === nodeEditorController.selectedClipId
                                                        readonly property int hoveredTrimEdge: (clipMouseArea.containsMouse
                                                                && !suppressTrimCursor)
                                                            ? root.clipTrimEdgeAt(clipMouseArea.mouseX, width)
                                                            : 0
                                                        property bool dragPreviewActive: false
                                                        property bool copyDragPreviewActive: false
                                                        property bool clipDragMoved: false
                                                        property bool ignoreNextClick: false
                                                        property bool trimPreviewActive: false
                                                        property bool clipTrimMoved: false
                                                        property bool editArmed: false
                                                        property bool suppressTrimCursor: false
                                                        property bool timelineSelectionStarted: false
                                                        property int trimDragMode: 0
                                                        property real trimPreviewStartRatio: clipStartRatio
                                                        property real trimPreviewEndRatio: clipEndRatio
                                                        property real dragX: baseX
                                                        property real dragStartClipX: 0
                                                        property real dragStartPointerX: 0
                                                        property real dragStartRootY: 0
                                                        property real dragCurrentRootY: 0
                                                        property real timelineSelectionPressX: 0
                                                        property real timelineSelectionPressRootY: 0
                                                        z: trimPreviewActive ? 14 : 10
                                                        x: visualBaseX
                                                        y: 0
                                                        width: Math.max(
                                                            root.clipEdgeHandleWidth * 2,
                                                            Math.round(root.timelineWidthForRatio(Math.max(minimumClipRatio, visualEndRatio - visualStartRatio), laneTimeline.width)))
                                                        height: laneTimeline.height
                                                        opacity: root.isPendingMoveSourceClip(
                                                                laneDelegate.modelData.laneId,
                                                                clipDelegate.modelData.clipId)
                                                            ? 0.0
                                                            : (dragPreviewActive ? 0.68 : 1.0)
                                                        clip: true
                                                        clipRangeOnly: true
                                                        clipRangeHandlesVisible: false
                                                        playheadVisible: false
                                                        contentMargin: 0
                                                        invertedColors: clipDelegate.clipSelected
                                                            && !clipDelegate.dragPreviewActive
                                                        invertedSegmentStartRatio: root.timelineSelectionClipLocalStartRatio(
                                                            laneDelegate.laneIndex,
                                                            visualStartRatio,
                                                            visualEndRatio)
                                                        invertedSegmentEndRatio: root.timelineSelectionClipLocalEndRatio(
                                                            laneDelegate.laneIndex,
                                                            visualStartRatio,
                                                            visualEndRatio)
                                                        textureSize: Qt.size(Math.max(1, width), Math.max(1, height))
                                                        visible: modelData.hasWaveform && width > 1 && height > 1
                                                        waveformState: modelData.waveformState
                                                        previewWaveformState: ({
                                                            "active": trimPreviewActive,
                                                            "clipStartMs": visualClipSourceStartMs,
                                                            "clipEndMs": visualClipSourceEndMs
                                                        })
                                                        Component.onCompleted: {
                                                            clipDelegate.verticalZoom = root.clipVerticalZoomForKey(clipZoomKey)
                                                        }
                                                        onClipZoomKeyChanged: {
                                                            clipDelegate.verticalZoom = root.clipVerticalZoomForKey(clipZoomKey)
                                                        }
                                                        onVerticalZoomChanged: {
                                                            root.setClipVerticalZoom(clipZoomKey, clipDelegate.verticalZoom)
                                                        }

                                                        Rectangle {
                                                            anchors.fill: parent
                                                            anchors.margins: 0
                                                            color: "transparent"
                                                            border.width: clipDelegate.modelData.clipId === nodeEditorController.selectedClipId ? 1 : 0
                                                            border.color: "#9ec7f0"
                                                            z: 2
                                                        }

                                                        Rectangle {
                                                            anchors.fill: parent
                                                            color: "#9ec7f0"
                                                            opacity: clipDelegate.dragPreviewActive && !clipDelegate.trimPreviewActive ? 0.10 : 0.0
                                                            z: 2
                                                        }

                                                        Rectangle {
                                                            anchors.fill: parent
                                                            color: "transparent"
                                                            border.width: clipDelegate.dragPreviewActive && !clipDelegate.trimPreviewActive ? 1 : 0
                                                            border.color: "#d1e7ff"
                                                            opacity: 0.95
                                                            z: 3
                                                        }

                                                        Rectangle {
                                                            visible: clipTitle.paintedWidth > 0
                                                                && !clipDelegate.trimPreviewActive
                                                            x: 4
                                                            y: 2
                                                            width: Math.min(
                                                                Math.max(0, parent.width - 8),
                                                                clipTitle.paintedWidth + 10)
                                                            height: clipTitle.implicitHeight + 4
                                                            radius: 3
                                                            color: "#99000000"
                                                            z: 3
                                                        }

                                                        Text {
                                                            id: clipTitle
                                                            visible: !clipDelegate.trimPreviewActive
                                                            anchors.left: parent.left
                                                            anchors.right: parent.right
                                                            anchors.top: parent.top
                                                            anchors.leftMargin: 8
                                                            anchors.rightMargin: 8
                                                            anchors.topMargin: 4
                                                            text: clipDelegate.modelData.title
                                                            color: "#eef2f6"
                                                            font.pixelSize: 11
                                                            font.weight: Font.Medium
                                                            elide: Text.ElideRight
                                                            z: 4
                                                        }

                                                        Timer {
                                                            id: clearClipClickGuardTimer
                                                            interval: 1
                                                            repeat: false
                                                            onTriggered: {
                                                                clipDelegate.ignoreNextClick = false
                                                                clipDelegate.clipDragMoved = false
                                                                clipDelegate.clipTrimMoved = false
                                                            }
                                                        }

                                                        MouseArea {
                                                            id: clipMouseArea
                                                            anchors.fill: parent
                                                            acceptedButtons: Qt.LeftButton
                                                            preventStealing: true
                                                            hoverEnabled: true
                                                            cursorShape: clipDelegate.hoveredTrimEdge !== 0
                                                                ? Qt.SizeHorCursor
                                                                : Qt.PointingHandCursor
                                                            onEntered: {
                                                                clipDelegate.suppressTrimCursor = false
                                                            }
                                                            onExited: {
                                                                clipDelegate.suppressTrimCursor = false
                                                            }
                                                            onPressed: function(mouse) {
                                                                var pointer = mapToItem(laneTimeline, mouse.x, mouse.y)
                                                                var rootPoint = mapToItem(root, mouse.x, mouse.y)
                                                                root.forceActiveFocus()
                                                                root.clearTimelineSelection()
                                                                root.clearPendingClipDrop()
                                                                clipDelegate.suppressTrimCursor = false
                                                                var pressedTrimEdge = root.clipTrimEdgeAt(mouse.x, width)
                                                                var copyDragRequested = (mouse.modifiers & Qt.ControlModifier) !== 0
                                                                clipDelegate.timelineSelectionStarted = false
                                                                clipDelegate.timelineSelectionPressX = pointer.x
                                                                clipDelegate.timelineSelectionPressRootY = rootPoint.y
                                                                clipDelegate.editArmed = clipDelegate.clipSelected
                                                                    || pressedTrimEdge !== 0
                                                                    || copyDragRequested
                                                                if (!clipDelegate.editArmed) {
                                                                    clipDelegate.trimDragMode = 0
                                                                    clipDelegate.clipDragMoved = false
                                                                    clipDelegate.clipTrimMoved = false
                                                                    mouse.accepted = true
                                                                    return
                                                                }
                                                                clipDelegate.trimDragMode = pressedTrimEdge
                                                                root.hideClipDragGhost()
                                                                clipDelegate.trimPreviewStartRatio = clipDelegate.clipStartRatio
                                                                clipDelegate.trimPreviewEndRatio = clipDelegate.clipEndRatio
                                                                clipDelegate.dragStartPointerX = pointer.x
                                                                clipDelegate.dragStartRootY = mapToItem(root, mouse.x, mouse.y).y
                                                                clipDelegate.dragCurrentRootY = clipDelegate.dragStartRootY
                                                                clipDelegate.dragStartClipX = clipDelegate.x
                                                                clipDelegate.dragX = clipDelegate.x
                                                                clipDelegate.clipDragMoved = false
                                                                clipDelegate.clipTrimMoved = false
                                                                mouse.accepted = true
                                                            }
                                                            onPositionChanged: function(mouse) {
                                                                if (!clipMouseArea.pressed) {
                                                                    clipDelegate.suppressTrimCursor = false
                                                                    return
                                                                }
                                                                if (!clipDelegate.editArmed) {
                                                                    var selectionPointer = mapToItem(laneTimeline, mouse.x, mouse.y)
                                                                    var selectionRootPoint = mapToItem(root, mouse.x, mouse.y)
                                                                    if (!clipDelegate.timelineSelectionStarted
                                                                        && Math.abs(selectionPointer.x - clipDelegate.timelineSelectionPressX) < 3
                                                                        && Math.abs(selectionRootPoint.y - clipDelegate.timelineSelectionPressRootY) < 3)
                                                                        return
                                                                if (!clipDelegate.timelineSelectionStarted) {
                                                                    clipDelegate.timelineSelectionStarted = root.beginTimelineSelection(
                                                                        root.timelineRatioForX(clipDelegate.timelineSelectionPressX, laneTimeline.width),
                                                                        root.laneIndexForRootY(clipDelegate.timelineSelectionPressRootY),
                                                                        true)
                                                                    if (!clipDelegate.timelineSelectionStarted)
                                                                        return
                                                                }
                                                                root.updateTimelineSelection(
                                                                    root.timelineRatioForX(selectionPointer.x, laneTimeline.width),
                                                                    root.laneIndexForRootY(selectionRootPoint.y))
                                                                root.updateTimelineSelectionAutoScroll(
                                                                    selectionPointer.x,
                                                                    laneTimeline.width,
                                                                    selectionRootPoint.y)
                                                                mouse.accepted = true
                                                                return
                                                            }
                                                                if (!clipDelegate.editArmed)
                                                                    return

                                                                var pointer = mapToItem(laneTimeline, mouse.x, mouse.y)
                                                                if (clipDelegate.trimDragMode !== 0) {
                                                                    if (!clipDelegate.clipTrimMoved)
                                                                        nodeEditorController.selectLane(laneDelegate.modelData.laneId)
                                                                    var trimRatio = root.timelineRatioForX(pointer.x, laneTimeline.width)
                                                                    if (clipDelegate.trimDragMode < 0) {
                                                                        clipDelegate.trimPreviewStartRatio = root.clamp(
                                                                            trimRatio,
                                                                            clipDelegate.minimumStartRatio,
                                                                            Math.max(
                                                                                clipDelegate.minimumStartRatio,
                                                                                clipDelegate.clipEndRatio - clipDelegate.minimumClipRatio))
                                                                    } else {
                                                                        clipDelegate.trimPreviewEndRatio = root.clamp(
                                                                            trimRatio,
                                                                            Math.min(
                                                                                clipDelegate.maximumEndRatio,
                                                                                clipDelegate.clipStartRatio + clipDelegate.minimumClipRatio),
                                                                            clipDelegate.maximumEndRatio)
                                                                    }
                                                                    clipDelegate.clipTrimMoved = true
                                                                    clipDelegate.trimPreviewActive = true
                                                                    root.setClipPreview(
                                                                        laneDelegate.modelData.laneId,
                                                                        clipDelegate.modelData.clipId,
                                                                        clipDelegate.trimPreviewStartRatio,
                                                                        clipDelegate.trimPreviewEndRatio)
                                                                    mouse.accepted = true
                                                                    return
                                                                }

                                                                var dragDelta = pointer.x - clipDelegate.dragStartPointerX
                                                                var rootPointer = mapToItem(root, mouse.x, mouse.y)
                                                                clipDelegate.dragCurrentRootY = rootPointer.y
                                                                var laneDragDelta = rootPointer.y - clipDelegate.dragStartRootY
                                                                if (!clipDelegate.clipDragMoved
                                                                    && Math.abs(dragDelta) < 3
                                                                    && Math.abs(laneDragDelta) < 3)
                                                                    return

                                                                if (!clipDelegate.clipDragMoved)
                                                                    nodeEditorController.selectLane(laneDelegate.modelData.laneId)
                                                                clipDelegate.clipDragMoved = true
                                                                clipDelegate.dragPreviewActive = true
                                                                clipDelegate.copyDragPreviewActive = (mouse.modifiers & Qt.ControlModifier) !== 0
                                                                var minDragX = root.timelineXForRatio(0.0, laneTimeline.width)
                                                                var maxDragX = root.timelineXForRatio(clipDelegate.maxOffsetRatio, laneTimeline.width)
                                                                clipDelegate.dragX = root.clamp(
                                                                    clipDelegate.dragStartClipX + dragDelta,
                                                                    Math.min(minDragX, maxDragX),
                                                                    Math.max(minDragX, maxDragX))
                                                                var targetLaneId = root.laneIdForRootY(
                                                                    rootPointer.y,
                                                                    laneDelegate.modelData.laneId)
                                                                var previewStartRatio = root.timelineRatioForX(clipDelegate.dragX, laneTimeline.width)
                                                                root.syncDragPlayheadToRatio(previewStartRatio)
                                                                var showInlinePreview = !clipDelegate.copyDragPreviewActive
                                                                    && targetLaneId === laneDelegate.modelData.laneId
                                                                if (showInlinePreview) {
                                                                    root.setClipPreview(
                                                                        laneDelegate.modelData.laneId,
                                                                        clipDelegate.modelData.clipId,
                                                                        previewStartRatio,
                                                                        Math.min(1.0, previewStartRatio + clipDelegate.clipWidthRatio))
                                                                } else {
                                                                    root.clearClipPreview()
                                                                }
                                                                var targetLaneIndex = root.laneIndexForRootY(rootPointer.y)
                                                                root.showClipDragGhost(
                                                                    root.laneHeaderWidth + clipDelegate.dragX,
                                                                    targetLaneIndex >= 0 ? root.laneTopForIndex(targetLaneIndex) : laneDelegate.y,
                                                                    clipDelegate.width,
                                                                    laneTimeline.height,
                                                                    clipDelegate.modelData.title,
                                                                    clipDelegate.modelData.waveformState,
                                                                    clipDelegate.verticalZoom,
                                                                    clipDelegate.copyDragPreviewActive ? 0.62 : 1.0)
                                                                mouse.accepted = true
                                                            }
                                                            onReleased: function(mouse) {
                                                                clipDelegate.suppressTrimCursor = true
                                                                if (!clipDelegate.editArmed) {
                                                                    if (clipDelegate.timelineSelectionStarted) {
                                                                        var selectionReleasePointer = mapToItem(laneTimeline, mouse.x, mouse.y)
                                                                        var selectionReleasePoint = mapToItem(root, mouse.x, mouse.y)
                                                                        root.updateTimelineSelection(
                                                                            root.timelineRatioForX(selectionReleasePointer.x, laneTimeline.width),
                                                                            root.laneIndexForRootY(selectionReleasePoint.y))
                                                                        root.finishTimelineSelection()
                                                                        root.stopTimelineSelectionAutoScroll()
                                                                        clipDelegate.ignoreNextClick = true
                                                                        clearClipClickGuardTimer.restart()
                                                                    }
                                                                    clipDelegate.timelineSelectionStarted = false
                                                                    clipDelegate.trimDragMode = 0
                                                                    clipDelegate.editArmed = false
                                                                    mouse.accepted = true
                                                                    return
                                                                }
                                                                if (clipDelegate.clipTrimMoved && clipDelegate.trimDragMode !== 0) {
                                                                    var pointer = mapToItem(laneTimeline, mouse.x, mouse.y)
                                                                    clipDelegate.dragPreviewActive = false
                                                                    clipDelegate.copyDragPreviewActive = false
                                                                    clipDelegate.trimPreviewActive = false
                                                                    root.clearClipPreview()
                                                                    root.clearPendingClipDrop()
                                                                    root.hideClipDragGhost()
                                                                    nodeEditorController.trimClipToRatio(
                                                                        laneDelegate.modelData.laneId,
                                                                        clipDelegate.modelData.clipId,
                                                                        clipDelegate.trimDragMode < 0,
                                                                        root.timelineRatioForX(pointer.x, laneTimeline.width))
                                                                    clipDelegate.ignoreNextClick = true
                                                                    clearClipClickGuardTimer.restart()
                                                                } else if (clipDelegate.clipDragMoved) {
                                                                    var nextRatio = root.timelineRatioForX(clipDelegate.dragX, laneTimeline.width)
                                                                    var releasePoint = mapToItem(root, mouse.x, mouse.y)
                                                                    var targetLaneId = root.laneIdForRootY(
                                                                        releasePoint.y,
                                                                        laneDelegate.modelData.laneId)
                                                                    var dropCopyClip = (mouse.modifiers & Qt.ControlModifier) !== 0
                                                                    var targetLaneIndex = root.laneIndexForRootY(releasePoint.y)
                                                                    root.syncDragPlayheadToRatio(nextRatio)
                                                                    clipDelegate.dragPreviewActive = false
                                                                    clipDelegate.copyDragPreviewActive = false
                                                                    clipDelegate.trimPreviewActive = false
                                                                    root.clearClipPreview()
                                                                    root.hideClipDragGhost()
                                                                    root.showPendingClipDrop(
                                                                        laneDelegate.modelData.laneId,
                                                                        clipDelegate.modelData.clipId,
                                                                        dropCopyClip,
                                                                        root.laneHeaderWidth + clipDelegate.dragX,
                                                                        targetLaneIndex >= 0 ? root.laneTopForIndex(targetLaneIndex) : laneDelegate.y,
                                                                        clipDelegate.width,
                                                                        laneTimeline.height,
                                                                        clipDelegate.modelData.title,
                                                                        clipDelegate.modelData.waveformState,
                                                                        clipDelegate.verticalZoom)
                                                                    Qt.callLater(function() {
                                                                        nodeEditorController.dropClipToRatio(
                                                                            laneDelegate.modelData.laneId,
                                                                            clipDelegate.modelData.clipId,
                                                                            targetLaneId,
                                                                            nextRatio,
                                                                            dropCopyClip)
                                                                        root.clearPendingClipDrop()
                                                                    })
                                                                    clipDelegate.ignoreNextClick = true
                                                                    clearClipClickGuardTimer.restart()
                                                                } else {
                                                                    clipDelegate.clipDragMoved = false
                                                                }
                                                                clipDelegate.trimDragMode = 0
                                                                clipDelegate.editArmed = false
                                                                mouse.accepted = true
                                                            }
                                                            onCanceled: {
                                                                root.clearPendingClipDrop()
                                                                clipDelegate.suppressTrimCursor = true
                                                                clipDelegate.dragPreviewActive = false
                                                                clipDelegate.copyDragPreviewActive = false
                                                                clipDelegate.clipDragMoved = false
                                                                clipDelegate.trimPreviewActive = false
                                                                clipDelegate.clipTrimMoved = false
                                                                clipDelegate.timelineSelectionStarted = false
                                                                clipDelegate.editArmed = false
                                                                clipDelegate.trimDragMode = 0
                                                                root.stopTimelineSelectionAutoScroll()
                                                                if (root.timelineSelectionDragActive)
                                                                    root.finishTimelineSelection()
                                                                root.clearClipPreview()
                                                                root.hideClipDragGhost()
                                                            }
                                                            onClicked: function(mouse) {
                                                                if (clipDelegate.ignoreNextClick) {
                                                                    clipDelegate.ignoreNextClick = false
                                                                    return
                                                                }
                                                                root.forceActiveFocus()
                                                                root.clearTimelineSelection()
                                                                nodeEditorController.selectLane(laneDelegate.modelData.laneId)
                                                                nodeEditorController.setPlayheadFromRatio(root.timelineRatioForX(clipDelegate.x + mouse.x, laneTimeline.width))
                                                            }
                                                            onDoubleClicked: {
                                                                root.forceActiveFocus()
                                                                root.clearTimelineSelection()
                                                                nodeEditorController.selectClip(laneDelegate.modelData.laneId, clipDelegate.modelData.clipId)
                                                            }
                                                        }
                                                    }
                                                }

                                                Repeater {
                                                    model: modelData.clips || []

                                                    delegate: Item {
                                                        required property var modelData
                                                        readonly property real clipStartRatio: modelData.clipOffsetRatio || 0
                                                        readonly property real clipWidthRatio: Math.max(0.0, modelData.clipWidthRatio || 0.08)
                                                        readonly property real clipEndRatio: Math.min(1.0, clipStartRatio + clipWidthRatio)
                                                        readonly property bool suppressPreviewOverlapMarker: root.clipPreviewActive
                                                            && root.clipPreviewLaneId === String(laneDelegate.modelData.laneId)
                                                            && root.clipPreviewClipId !== String(modelData.clipId)
                                                        readonly property real effectiveStartRatio: root.effectiveClipStartRatio(
                                                            laneDelegate.modelData.laneId,
                                                            modelData.clipId,
                                                            clipStartRatio)
                                                        readonly property real effectiveEndRatio: root.effectiveClipEndRatio(
                                                            laneDelegate.modelData.laneId,
                                                            modelData.clipId,
                                                            clipEndRatio)
                                                        readonly property bool showLeftOverlap: !suppressPreviewOverlapMarker
                                                            && root.clipHasLeftOverlap(
                                                                laneDelegate.modelData.laneId,
                                                                laneDelegate.modelData.clips,
                                                                modelData.clipId,
                                                                clipStartRatio,
                                                                clipEndRatio)
                                                        readonly property bool showRightOverlap: !suppressPreviewOverlapMarker
                                                            && root.clipHasRightOverlap(
                                                                laneDelegate.modelData.laneId,
                                                                laneDelegate.modelData.clips,
                                                                modelData.clipId,
                                                                clipStartRatio,
                                                                clipEndRatio)
                                                        anchors.fill: parent
                                                        z: 15

                                                        Rectangle {
                                                            visible: parent.showLeftOverlap
                                                            x: Math.round(root.timelineXForRatio(parent.effectiveStartRatio, laneTimeline.width))
                                                            y: 0
                                                            width: 1
                                                            height: laneTimeline.height
                                                            color: "#0a0d12"
                                                            opacity: 0.95
                                                        }

                                                        Rectangle {
                                                            visible: parent.showRightOverlap
                                                            x: Math.round(root.timelineXForRatio(parent.effectiveEndRatio, laneTimeline.width)) - 1
                                                            y: 0
                                                            width: 1
                                                            height: laneTimeline.height
                                                            color: "#0a0d12"
                                                            opacity: 0.95
                                                        }
                                                    }
                                                }

                                                MouseArea {
                                                    anchors.fill: parent
                                                    z: 1
                                                    acceptedButtons: Qt.LeftButton
                                                    preventStealing: true
                                                    property real pressX: 0.0
                                                    property real pressRootY: 0.0
                                                    property bool selectionStarted: false
                                                    onPressed: function(mouse) {
                                                        pressX = mouse.x
                                                        pressRootY = mapToItem(root, mouse.x, mouse.y).y
                                                        selectionStarted = false
                                                        root.timelineSelectionReleaseGuardActive = false
                                                        root.forceActiveFocus()
                                                        mouse.accepted = true
                                                    }
                                                    onPositionChanged: function(mouse) {
                                                        if (!pressed)
                                                            return
                                                        var rootPoint = mapToItem(root, mouse.x, mouse.y)
                                                        if (!selectionStarted
                                                            && Math.abs(mouse.x - pressX) < 3
                                                            && Math.abs(rootPoint.y - pressRootY) < 3)
                                                            return
                                                        if (!selectionStarted) {
                                                            selectionStarted = root.beginTimelineSelection(
                                                                root.timelineRatioForX(pressX, width),
                                                                root.laneIndexForRootY(pressRootY),
                                                                true)
                                                            if (!selectionStarted)
                                                                return
                                                        }
                                                        root.updateTimelineSelection(
                                                            root.timelineRatioForX(mouse.x, width),
                                                            root.laneIndexForRootY(rootPoint.y))
                                                        root.updateTimelineSelectionAutoScroll(mouse.x, width, rootPoint.y)
                                                        mouse.accepted = true
                                                    }
                                                    onReleased: function(mouse) {
                                                        if (root.timelineSelectionReleaseGuardActive) {
                                                            root.timelineSelectionReleaseGuardActive = false
                                                            mouse.accepted = true
                                                            return
                                                        }
                                                        if (selectionStarted) {
                                                            var releasePoint = mapToItem(root, mouse.x, mouse.y)
                                                            var releaseLaneIndex = root.laneIndexForRootY(releasePoint.y)
                                                            root.updateTimelineSelection(
                                                                root.timelineRatioForX(mouse.x, width),
                                                                releaseLaneIndex)
                                                            root.finishTimelineSelection()
                                                            root.stopTimelineSelectionAutoScroll()
                                                            mouse.accepted = true
                                                            return
                                                        }
                                                        root.clearTimelineSelection()
                                                        root.forceActiveFocus()
                                                        nodeEditorController.selectLaneHeader(modelData.laneId)
                                                        nodeEditorController.setPlayheadFromRatio(root.timelineRatioForX(mouse.x, width))
                                                        mouse.accepted = true
                                                    }
                                                    onCanceled: {
                                                        selectionStarted = false
                                                        root.stopTimelineSelectionAutoScroll()
                                                        if (root.timelineSelectionDragActive) {
                                                            root.timelineSelectionReleaseGuardActive = true
                                                            root.finishTimelineSelection()
                                                        }
                                                    }
                                                    onWheel: function(wheel) {
                                                        root.forceActiveFocus()
                                                        var delta = wheel.angleDelta.y !== 0 ? wheel.angleDelta.y : wheel.angleDelta.x
                                                        root.zoomTimelineAt(wheel.x, width, delta)
                                                        wheel.accepted = true
                                                    }
                                                }

                                                Rectangle {
                                                    visible: nodeEditorController.showTimeline
                                                        && root.editMarkerBlinkOn
                                                        && nodeEditorController.insertionMarkerStationary
                                                        && modelData.laneId === nodeEditorController.selectedLaneId
                                                    x: root.timelineXForRatio(nodeEditorController.insertionMarkerRatio, laneTimeline.width)
                                                    y: 0
                                                    width: 1
                                                    height: laneTimeline.height
                                                    color: "#9ec7f0"
                                                    z: 20
                                                }
                                            }
                                        }
                                    }
                                }

                                Text {
                                    visible: nodeEditorController.nodeTrackCount === 0
                                    width: parent.width
                                    text: "No lanes yet."
                                    color: "#94a3b3"
                                    font.pixelSize: 12
                                }
                            }

                            Rectangle {
                                visible: root.timelineSelectionVisible
                                    && root.timelineSelectionVisualStartLaneIndex() >= 0
                                    && root.timelineSelectionVisualEndLaneIndex() >= 0
                                    && nodeEditorController.nodeTrackCount > 0
                                readonly property int selectionTopLaneIndex: Math.min(
                                    root.timelineSelectionVisualStartLaneIndex(),
                                    root.timelineSelectionVisualEndLaneIndex())
                                readonly property int selectionBottomLaneIndex: Math.max(
                                    root.timelineSelectionVisualStartLaneIndex(),
                                    root.timelineSelectionVisualEndLaneIndex())
                                x: root.laneHeaderWidth + Math.round(
                                    root.timelineXForRatio(
                                        Math.min(root.timelineSelectionStartRatio, root.timelineSelectionEndRatio),
                                        Math.max(1, lanesContent.width - root.laneHeaderWidth)))
                                y: root.laneTopForIndex(selectionTopLaneIndex)
                                width: Math.max(
                                    1,
                                    Math.round(
                                        root.timelineWidthForRatio(
                                            Math.abs(root.timelineSelectionEndRatio - root.timelineSelectionStartRatio),
                                            Math.max(1, lanesContent.width - root.laneHeaderWidth))))
                                height: root.laneTopForIndex(selectionBottomLaneIndex) + root.laneHeight - root.laneTopForIndex(selectionTopLaneIndex)
                                color: "#669ec7f0"
                                border.width: 1
                                border.color: "#7eb9ee"
                                z: 18
                            }

                            ClipWaveformQuickItem {
                                id: clipDragGhost

                                x: root.clipDragGhostX
                                y: root.clipDragGhostY
                                width: root.clipDragGhostWidth
                                height: root.clipDragGhostHeight
                                z: 19
                                opacity: root.clipDragGhostOpacity
                                visible: root.clipDragGhostVisible
                                    && root.clipDragGhostWaveformState !== null
                                    && width > 1
                                    && height > 1
                                clip: true
                                enabled: false
                                clipRangeOnly: true
                                clipRangeHandlesVisible: false
                                    playheadVisible: false
                                    contentMargin: 0
                                    invertedColors: true
                                    textureSize: Qt.size(Math.max(1, width), Math.max(1, height))
                                    waveformState: root.clipDragGhostWaveformState || ({})
                                    previewWaveformState: ({ "active": false })
                                    verticalZoom: root.clipDragGhostVerticalZoom

                                Rectangle {
                                    anchors.fill: parent
                                    color: "#9ec7f0"
                                    opacity: 0.10
                                    z: 2
                                }

                                Rectangle {
                                    anchors.fill: parent
                                    color: "transparent"
                                    border.width: 1
                                    border.color: "#d1e7ff"
                                    opacity: 0.95
                                    z: 3
                                }

                                Text {
                                    visible: false
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.top: parent.top
                                    anchors.leftMargin: 8
                                    anchors.rightMargin: 8
                                    anchors.topMargin: 4
                                    text: root.clipDragGhostTitle
                                    color: "#f5f9ff"
                                    font.pixelSize: 11
                                    font.weight: Font.Medium
                                    elide: Text.ElideRight
                                    z: 4
                                }
                            }

                            ClipWaveformQuickItem {
                                id: pendingClipDropPreview

                                x: root.pendingClipDropX
                                y: root.pendingClipDropY
                                width: root.pendingClipDropWidth
                                height: root.pendingClipDropHeight
                                z: 19
                                visible: root.pendingClipDropVisible
                                    && root.pendingClipDropWaveformState !== null
                                    && width > 1
                                    && height > 1
                                clip: true
                                enabled: false
                                clipRangeOnly: true
                                clipRangeHandlesVisible: false
                                playheadVisible: false
                                contentMargin: 0
                                invertedColors: true
                                textureSize: Qt.size(Math.max(1, width), Math.max(1, height))
                                waveformState: root.pendingClipDropWaveformState || ({})
                                previewWaveformState: ({ "active": false })
                                verticalZoom: root.pendingClipDropVerticalZoom

                                Rectangle {
                                    visible: pendingClipDropTitleText.paintedWidth > 0
                                    x: 4
                                    y: 2
                                    width: Math.min(
                                        Math.max(0, parent.width - 8),
                                        pendingClipDropTitleText.paintedWidth + 10)
                                    height: pendingClipDropTitleText.implicitHeight + 4
                                    radius: 3
                                    color: "#99000000"
                                    z: 2
                                }

                                Text {
                                    id: pendingClipDropTitleText
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.top: parent.top
                                    anchors.leftMargin: 8
                                    anchors.rightMargin: 8
                                    anchors.topMargin: 4
                                    text: root.pendingClipDropTitle
                                    color: "#eef2f6"
                                    font.pixelSize: 11
                                    font.weight: Font.Medium
                                    elide: Text.ElideRight
                                    z: 3
                                }

                                Rectangle {
                                    anchors.fill: parent
                                    color: "transparent"
                                    border.width: 1
                                    border.color: "#9ec7f0"
                                    z: 4
                                }
                            }

                            Rectangle {
                                visible: nodeEditorController.showTimeline
                                    && nodeEditorController.nodeTrackCount > 0
                                    && nodeEditorController.playbackActive
                                x: root.laneHeaderWidth
                                    + root.timelineXForRatio(
                                        nodeEditorController.playheadRatio,
                                        parent.width - root.laneHeaderWidth)
                                y: 0
                                width: 1
                                height: parent.height
                                color: "#9ec7f0"
                                z: 20
                            }

                        }
                    }
                }
            }
        }
    }

    implicitHeight: 40 + 24 + 28 + Math.max(0, lanesColumn.implicitHeight)

    Popup {
        id: menuPopup

        property real popupX: 0
        property real popupY: 0

        parent: root.Window.window ? root.Window.window.contentItem : root
        x: popupX
        y: popupY
        z: 1001
        modal: false
        focus: true
        padding: 6
        closePolicy: Popup.CloseOnEscape
        implicitWidth: Math.max(220, menuColumn.implicitWidth + leftPadding + rightPadding)
        implicitHeight: menuColumn.implicitHeight + topPadding + bottomPadding

        onClosed: root.closeMenu()

        background: Rectangle {
            color: theme.menuBackground
            border.color: theme.menuBorder
            border.width: 1
            radius: 6
        }

        contentItem: Column {
            id: menuColumn
            spacing: 2

            Repeater {
                model: root.activeMenuItems

                delegate: Item {
                    required property var modelData
                    implicitWidth: 220
                    implicitHeight: 32

                    Rectangle {
                        anchors.fill: parent
                        radius: 6
                        color: hoverArea.containsMouse && root.menuItemEnabled(modelData)
                            ? theme.menuItemHover
                            : "transparent"
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        spacing: 8

                        Label {
                            Layout.fillWidth: true
                            text: modelData.text || ""
                            color: root.menuItemEnabled(modelData) ? root.menuPopupTextColor : theme.menuItemDisabled
                            font.pixelSize: 13
                            elide: Text.ElideRight
                        }
                    }

                    MouseArea {
                        id: hoverArea
                        anchors.fill: parent
                        enabled: root.menuItemEnabled(modelData)
                        hoverEnabled: enabled
                        onClicked: root.triggerMenuAction(modelData.key)
                    }
                }
            }
        }
    }

    MouseArea {
        visible: menuPopup.opened
        parent: root.Window.window ? root.Window.window.contentItem : root
        anchors.fill: parent
        z: 1000
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        hoverEnabled: false

        onPressed: function(mouse) {
            if (root.pointInsideItem(root.activeMenuAnchor, parent, mouse.x, mouse.y)) {
                mouse.accepted = false
                return
            }

            root.closeMenu()
            menuPopup.close()
            mouse.accepted = false
        }
    }
}
