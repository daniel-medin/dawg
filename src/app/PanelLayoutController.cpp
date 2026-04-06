#include "app/PanelLayoutController.h"

#include <algorithm>
#include <cmath>

#include <QApplication>
#include <QDataStream>
#include <QScreen>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include "app/AudioPoolQuickController.h"
#include "app/MainWindow.h"
#include "app/PlayerController.h"
#include "app/ShellLayoutController.h"
#include "ui/NativeVideoViewport.h"
#include "ui/VideoViewportQuickController.h"

namespace
{
void updatePanelActionText(QAction* action, const bool visible, const QString& label)
{
    if (!action)
    {
        return;
    }

    const auto prefix = visible ? QStringLiteral("Hide ") : QStringLiteral("Show ");
    action->setText(prefix + label);
}

QByteArray saveWindowGeometry(QWindow* window)
{
    if (!window)
    {
        return {};
    }

    QByteArray state;
    QDataStream stream(&state, QIODevice::WriteOnly);
    stream << window->geometry();
    return state;
}

bool restoreWindowGeometry(QWindow* window, const QByteArray& state)
{
    if (!window || state.isEmpty())
    {
        return false;
    }

    QRect geometry;
    QDataStream stream(state);
    stream >> geometry;
    if (!geometry.isValid())
    {
        return false;
    }

    window->setGeometry(geometry);
    return true;
}

}

PanelLayoutController::PanelLayoutController(MainWindow& window)
    : m_window(window)
{
}

dawg::project::UiState PanelLayoutController::snapshotProjectUiState() const
{
    const auto timelineVisible = (m_window.m_showTimelineAction && m_window.m_showTimelineAction->isChecked())
        || m_window.m_timelineDetached;
    const auto clipEditorVisible = (m_window.m_showClipEditorAction && m_window.m_showClipEditorAction->isChecked())
        || m_window.m_clipEditorDetached;
    const auto nodeEditorVisible = m_window.m_showNodeEditorAction && m_window.m_showNodeEditorAction->isChecked();
    const auto mixVisible = (m_window.m_showMixAction && m_window.m_showMixAction->isChecked())
        || m_window.m_mixDetached;
    const auto audioPoolVisible = (m_window.m_audioPoolAction && m_window.m_audioPoolAction->isChecked())
        || m_window.m_audioPoolDetached;

    dawg::project::UiState state;
    state.videoDetached = m_window.m_videoDetached;
    state.detachedVideoWindowGeometry = m_window.m_videoDetached && m_window.m_detachedVideoWindow
                                            ? saveWindowGeometry(m_window.m_detachedVideoWindow)
                                            : m_window.m_detachedVideoWindowGeometry;
    state.timelineDetached = m_window.m_timelineDetached;
    state.detachedTimelineWindowGeometry = m_window.m_timelineDetached && m_window.m_detachedTimelineWindow
                                               ? saveWindowGeometry(m_window.m_detachedTimelineWindow)
                                               : m_window.m_detachedTimelineWindowGeometry;
    state.clipEditorDetached = m_window.m_clipEditorDetached;
    state.detachedClipEditorWindowGeometry = m_window.m_clipEditorDetached && m_window.m_detachedClipEditorWindow
                                                 ? saveWindowGeometry(m_window.m_detachedClipEditorWindow)
                                                 : m_window.m_detachedClipEditorWindowGeometry;
    state.mixDetached = m_window.m_mixDetached;
    state.detachedMixWindowGeometry = m_window.m_mixDetached && m_window.m_detachedMixWindow
                                          ? saveWindowGeometry(m_window.m_detachedMixWindow)
                                          : m_window.m_detachedMixWindowGeometry;
    state.audioPoolDetached = m_window.m_audioPoolDetached;
    state.detachedAudioPoolWindowGeometry = m_window.m_audioPoolDetached && m_window.m_detachedAudioPoolWindow
                                                ? saveWindowGeometry(m_window.m_detachedAudioPoolWindow)
                                                : m_window.m_detachedAudioPoolWindowGeometry;
    state.timelineVisible = timelineVisible;
    state.clipEditorVisible = clipEditorVisible;
    state.nodeEditorVisible = nodeEditorVisible;
    state.mixVisible = mixVisible;
    state.audioPoolVisible = audioPoolVisible;
    state.audioPoolShowLength = m_window.m_audioPoolShowLength;
    state.audioPoolShowSize = m_window.m_audioPoolShowSize;
    state.showAllNodeNames = m_window.m_showAllNodeNamesAction && m_window.m_showAllNodeNamesAction->isChecked();
    state.timelineClickSeeks = m_window.m_timelineClickSeeksAction && m_window.m_timelineClickSeeksAction->isChecked();
    state.timelineThumbnailsVisible =
        !m_window.m_showTimelineThumbnailsAction || m_window.m_showTimelineThumbnailsAction->isChecked();
    state.useProxyVideo = m_window.m_useProxyVideoAction && m_window.m_useProxyVideoAction->isChecked();
    state.audioPoolPreferredWidth = m_window.m_audioPoolPreferredWidth;
    state.timelinePreferredHeight = m_window.m_timelinePreferredHeight;
    state.clipEditorPreferredHeight = m_window.m_clipEditorPreferredHeight;
    state.nodeEditorPreferredHeight = m_window.m_nodeEditorPreferredHeight;
    state.mixPreferredHeight = m_window.m_mixPreferredHeight;
    state.windowGeometry = m_window.saveGeometry();
    state.windowMaximized = m_window.isMaximized();
    return state;
}

void PanelLayoutController::applyProjectUiState(const dawg::project::UiState& state)
{
    qInfo().noquote()
        << "Applying UI state:"
        << "timelineVisible=" << state.timelineVisible
        << "clipEditorVisible=" << state.clipEditorVisible
        << "nodeEditorVisible=" << state.nodeEditorVisible
        << "mixVisible=" << state.mixVisible
        << "audioPoolVisible=" << state.audioPoolVisible;
    m_window.m_projectStateChangeInProgress = true;
    m_window.m_detachedVideoWindowGeometry = state.detachedVideoWindowGeometry;
    m_window.m_detachedTimelineWindowGeometry = state.detachedTimelineWindowGeometry;
    m_window.m_detachedClipEditorWindowGeometry = state.detachedClipEditorWindowGeometry;
    m_window.m_detachedMixWindowGeometry = state.detachedMixWindowGeometry;
    m_window.m_detachedAudioPoolWindowGeometry = state.detachedAudioPoolWindowGeometry;
    m_window.m_audioPoolPreferredWidth = std::max(240, state.audioPoolPreferredWidth);
    m_window.m_audioPoolShowLength = state.audioPoolShowLength;
    m_window.m_audioPoolShowSize = state.audioPoolShowSize;
    if (state.contentSplitterSizes.size() == 2)
    {
        m_window.m_audioPoolPreferredWidth = std::max(240, state.contentSplitterSizes[1]);
    }
    if (m_window.m_audioPoolQuickController)
    {
        m_window.m_audioPoolQuickController->setShowLength(state.audioPoolShowLength);
        m_window.m_audioPoolQuickController->setShowSize(state.audioPoolShowSize);
    }
    m_window.m_timelinePreferredHeight = std::max(m_window.timelineMinimumHeight(), state.timelinePreferredHeight);
    m_window.m_clipEditorPreferredHeight = std::max(148, state.clipEditorPreferredHeight);
    m_window.m_nodeEditorPreferredHeight = std::max(148, state.nodeEditorPreferredHeight);
    m_window.m_mixPreferredHeight = std::max(132, state.mixPreferredHeight);
    if (state.mainVerticalSplitterSizes.size() == 4)
    {
        m_window.m_timelinePreferredHeight = std::max(m_window.timelineMinimumHeight(), state.mainVerticalSplitterSizes[1]);
        m_window.m_clipEditorPreferredHeight = std::max(148, state.mainVerticalSplitterSizes[2]);
        m_window.m_mixPreferredHeight = std::max(132, state.mainVerticalSplitterSizes[3]);
    }

    if (!state.windowGeometry.isEmpty())
    {
        m_window.restoreGeometry(state.windowGeometry);
    }
    if (state.windowMaximized)
    {
        m_window.showMaximized();
    }
    else if (m_window.isMaximized())
    {
        m_window.showNormal();
    }

    if (!m_window.isMaximized())
    {
        const auto availableGeometry = QApplication::primaryScreen()
            ? QApplication::primaryScreen()->availableGeometry()
            : QRect{0, 0, 1600, 900};
        auto nextGeometry = m_window.geometry();
        nextGeometry.setWidth(std::max(nextGeometry.width(), m_window.minimumWidth()));
        nextGeometry.setHeight(std::max(nextGeometry.height(), m_window.minimumHeight()));
        if (!availableGeometry.contains(nextGeometry))
        {
            nextGeometry.moveCenter(availableGeometry.center());
            nextGeometry = nextGeometry.intersected(availableGeometry.adjusted(16, 16, -16, -16));
            nextGeometry.setWidth(std::max(nextGeometry.width(), m_window.minimumWidth()));
            nextGeometry.setHeight(std::max(nextGeometry.height(), m_window.minimumHeight()));
        }
        m_window.setGeometry(nextGeometry);
    }

    if (m_window.m_showAllNodeNamesAction)
    {
        const QSignalBlocker blocker{m_window.m_showAllNodeNamesAction};
        m_window.m_showAllNodeNamesAction->setChecked(state.showAllNodeNames);
    }
    m_window.m_videoViewportQuickController->setShowAllLabels(state.showAllNodeNames);
    if (m_window.m_nativeViewport)
    {
        m_window.m_nativeViewport->setShowAllLabels(state.showAllNodeNames);
    }

    if (m_window.m_timelineClickSeeksAction)
    {
        const QSignalBlocker blocker{m_window.m_timelineClickSeeksAction};
        m_window.m_timelineClickSeeksAction->setChecked(state.timelineClickSeeks);
    }
    m_window.setTimelineSeekOnClickEnabled(state.timelineClickSeeks || !m_window.m_controller->isPlaying());

    if (m_window.m_showTimelineThumbnailsAction)
    {
        m_window.m_showTimelineThumbnailsAction->setChecked(state.timelineThumbnailsVisible);
    }
    m_window.setTimelineThumbnailsVisible(state.timelineThumbnailsVisible);

    if (m_window.m_useProxyVideoAction)
    {
        const QSignalBlocker blocker{m_window.m_useProxyVideoAction};
        m_window.m_useProxyVideoAction->setChecked(state.useProxyVideo);
    }
    if (m_window.m_controller)
    {
        m_window.m_controller->setUseProxyVideo(state.useProxyVideo);
    }

    updateTimelineVisibility(state.timelineVisible);
    updateClipEditorVisibility(state.clipEditorVisible);
    updateNodeEditorVisibility(state.nodeEditorVisible);
    updateMixVisibility(state.mixVisible);
    updateAudioPoolVisibility(state.audioPoolVisible);
    m_window.refreshAudioPool();

    syncMainVerticalPanelSizes();

    if (state.videoDetached != m_window.m_videoDetached)
    {
        if (state.videoDetached)
        {
            detachVideo();
        }
        else
        {
            attachVideo();
        }
    }
    if (state.timelineDetached != m_window.m_timelineDetached)
    {
        if (state.timelineDetached && state.timelineVisible)
        {
            detachTimeline();
        }
        else
        {
            attachTimeline();
        }
    }
    if (state.clipEditorDetached != m_window.m_clipEditorDetached)
    {
        if (state.clipEditorDetached && state.clipEditorVisible)
        {
            detachClipEditor();
        }
        else
        {
            attachClipEditor();
        }
    }
    if (state.mixDetached != m_window.m_mixDetached)
    {
        if (state.mixDetached && state.mixVisible)
        {
            detachMix();
        }
        else
        {
            attachMix();
        }
    }
    if (state.audioPoolDetached != m_window.m_audioPoolDetached)
    {
        if (state.audioPoolDetached && state.audioPoolVisible)
        {
            detachAudioPool();
        }
        else
        {
            attachAudioPool();
        }
    }

    m_window.syncShellLayoutViewport();
    updateDetachedPanelUiState();

    qInfo().noquote()
        << "Applied UI state:"
        << "timelineVisible=" << (m_window.m_shellLayoutController && m_window.m_shellLayoutController->timelineVisible())
        << "clipEditorVisible=" << (m_window.m_shellLayoutController && m_window.m_shellLayoutController->clipEditorVisible())
        << "nodeEditorVisible=" << (m_window.m_shellLayoutController && m_window.m_shellLayoutController->nodeEditorVisible())
        << "mixVisible=" << (m_window.m_shellLayoutController && m_window.m_shellLayoutController->mixVisible())
        << "audioPoolVisible=" << (m_window.m_shellLayoutController && m_window.m_shellLayoutController->audioPoolVisible())
        << "preferredAudioPoolWidth=" << m_window.m_audioPoolPreferredWidth
        << "preferredTimelineHeight=" << m_window.m_timelinePreferredHeight
        << "preferredClipEditorHeight=" << m_window.m_clipEditorPreferredHeight
        << "preferredNodeEditorHeight=" << m_window.m_nodeEditorPreferredHeight
        << "preferredMixHeight=" << m_window.m_mixPreferredHeight;

    m_window.m_projectStateChangeInProgress = false;
}

void PanelLayoutController::updateAudioPoolVisibility(const bool visible)
{
    if (m_window.m_audioPoolQuickWidget)
    {
        if (!visible && !m_window.m_audioPoolDetached)
        {
            m_window.m_audioPoolPreferredWidth =
                std::max(240, static_cast<int>(std::lround(m_window.m_audioPoolQuickWidget->width())));
        }
        m_window.m_audioPoolQuickWidget->setVisible(visible && !m_window.m_audioPoolDetached);
    }

    if (m_window.m_shellLayoutController)
    {
        m_window.m_shellLayoutController->setAudioPoolVisible(visible && !m_window.m_audioPoolDetached);
        m_window.m_shellLayoutController->setPreferredSizes(
            m_window.m_audioPoolPreferredWidth,
            m_window.m_timelinePreferredHeight,
            m_window.m_clipEditorPreferredHeight,
            m_window.m_nodeEditorPreferredHeight,
            m_window.m_mixPreferredHeight);
    }
    m_window.syncShellPanelGeometry();

    if (m_window.m_audioPoolAction && m_window.m_audioPoolAction->isChecked() != visible)
    {
        const QSignalBlocker blocker{m_window.m_audioPoolAction};
        m_window.m_audioPoolAction->setChecked(visible);
    }
    updatePanelActionText(m_window.m_audioPoolAction, visible, QStringLiteral("Audio Pool"));
    if (!visible && m_window.m_audioPoolDetached)
    {
        attachAudioPool();
    }
}

void PanelLayoutController::updateTimelineVisibility(const bool visible)
{
    if (m_window.m_timelineQuickWidget)
    {
        if (!visible && !m_window.m_timelineDetached)
        {
            m_window.m_timelinePreferredHeight = std::max(
                m_window.timelineMinimumHeight(),
                static_cast<int>(std::lround(m_window.m_timelineQuickWidget->height())));
        }
        m_window.m_timelineQuickWidget->setVisible(visible && !m_window.m_timelineDetached);
    }

    if (m_window.m_shellLayoutController)
    {
        m_window.m_shellLayoutController->setTimelineVisible(visible && !m_window.m_timelineDetached);
        m_window.m_shellLayoutController->setPreferredSizes(
            m_window.m_audioPoolPreferredWidth,
            m_window.m_timelinePreferredHeight,
            m_window.m_clipEditorPreferredHeight,
            m_window.m_nodeEditorPreferredHeight,
            m_window.m_mixPreferredHeight);
    }
    syncMainVerticalPanelSizes();

    if (m_window.m_showTimelineAction && m_window.m_showTimelineAction->isChecked() != visible)
    {
        const QSignalBlocker blocker{m_window.m_showTimelineAction};
        m_window.m_showTimelineAction->setChecked(visible);
    }
    updatePanelActionText(m_window.m_showTimelineAction, visible, QStringLiteral("Timeline"));
    if (!visible && m_window.m_timelineDetached)
    {
        attachTimeline();
    }
}

void PanelLayoutController::updateClipEditorVisibility(const bool visible)
{
    if (m_window.m_clipEditorQuickWidget)
    {
        if (!visible && !m_window.m_clipEditorDetached)
        {
            m_window.m_clipEditorPreferredHeight =
                std::max(148, static_cast<int>(std::lround(m_window.m_clipEditorQuickWidget->height())));
        }
        m_window.m_clipEditorQuickWidget->setVisible(visible && !m_window.m_clipEditorDetached);
    }

    if (visible)
    {
        m_window.refreshClipEditor();
    }
    else
    {
        m_window.m_clipEditorPreviewTimer.stop();
    }

    if (m_window.m_shellLayoutController)
    {
        m_window.m_shellLayoutController->setClipEditorVisible(visible && !m_window.m_clipEditorDetached);
        m_window.m_shellLayoutController->setPreferredSizes(
            m_window.m_audioPoolPreferredWidth,
            m_window.m_timelinePreferredHeight,
            m_window.m_clipEditorPreferredHeight,
            m_window.m_nodeEditorPreferredHeight,
            m_window.m_mixPreferredHeight);
    }
    syncMainVerticalPanelSizes();

    if (m_window.m_showClipEditorAction && m_window.m_showClipEditorAction->isChecked() != visible)
    {
        const QSignalBlocker blocker{m_window.m_showClipEditorAction};
        m_window.m_showClipEditorAction->setChecked(visible);
    }
    updatePanelActionText(m_window.m_showClipEditorAction, visible, QStringLiteral("Clip Editor"));
    if (!visible && m_window.m_clipEditorDetached)
    {
        attachClipEditor();
    }
}

void PanelLayoutController::updateMixVisibility(const bool visible)
{
    if (m_window.m_mixQuickWidget)
    {
        if (!visible && !m_window.m_mixDetached)
        {
            m_window.m_mixPreferredHeight =
                std::max(132, static_cast<int>(std::lround(m_window.m_mixQuickWidget->height())));
        }
        m_window.m_mixQuickWidget->setVisible(visible && !m_window.m_mixDetached);
    }

    if (visible)
    {
        if (!m_window.m_mixMeterTimer.isActive())
        {
            m_window.m_mixMeterTimer.start();
        }
        m_window.refreshMixView();
    }
    else
    {
        m_window.m_mixMeterTimer.stop();
    }

    if (m_window.m_shellLayoutController)
    {
        m_window.m_shellLayoutController->setMixVisible(visible && !m_window.m_mixDetached);
        m_window.m_shellLayoutController->setPreferredSizes(
            m_window.m_audioPoolPreferredWidth,
            m_window.m_timelinePreferredHeight,
            m_window.m_clipEditorPreferredHeight,
            m_window.m_nodeEditorPreferredHeight,
            m_window.m_mixPreferredHeight);
    }
    syncMainVerticalPanelSizes();

    if (m_window.m_showMixAction && m_window.m_showMixAction->isChecked() != visible)
    {
        const QSignalBlocker blocker{m_window.m_showMixAction};
        m_window.m_showMixAction->setChecked(visible);
    }
    updatePanelActionText(m_window.m_showMixAction, visible, QStringLiteral("Mixer"));
    if (!visible && m_window.m_mixDetached)
    {
        attachMix();
    }
}

void PanelLayoutController::updateNodeEditorVisibility(const bool visible)
{
    if (m_window.m_nodeEditorQuickWidget)
    {
        if (!visible)
        {
            m_window.m_nodeEditorPreferredHeight =
                std::max(148, static_cast<int>(std::lround(m_window.m_nodeEditorQuickWidget->height())));
        }
        m_window.m_nodeEditorQuickWidget->setVisible(visible);
    }

    if (visible)
    {
        m_window.refreshNodeEditor();
    }

    if (m_window.m_shellLayoutController)
    {
        m_window.m_shellLayoutController->setNodeEditorVisible(visible);
        m_window.m_shellLayoutController->setPreferredSizes(
            m_window.m_audioPoolPreferredWidth,
            m_window.m_timelinePreferredHeight,
            m_window.m_clipEditorPreferredHeight,
            m_window.m_nodeEditorPreferredHeight,
            m_window.m_mixPreferredHeight);
    }
    syncMainVerticalPanelSizes();

    if (m_window.m_showNodeEditorAction && m_window.m_showNodeEditorAction->isChecked() != visible)
    {
        const QSignalBlocker blocker{m_window.m_showNodeEditorAction};
        m_window.m_showNodeEditorAction->setChecked(visible);
    }
    updatePanelActionText(m_window.m_showNodeEditorAction, visible, QStringLiteral("Node Editor"));
}

void PanelLayoutController::detachVideo()
{
    if (m_window.m_videoDetached || !m_window.m_detachedVideoWindow)
    {
        return;
    }

    if (m_window.m_controller)
    {
        m_window.m_controller->setNativeVideoPresentationEnabled(false);
    }
    if (m_window.m_videoViewportQuickController)
    {
        m_window.m_videoViewportQuickController->setNativePresentationEnabled(false);
    }
    if (m_window.m_detachedVideoViewportQuickController)
    {
        m_window.m_detachedVideoViewportQuickController->setNativePresentationEnabled(false);
    }

    if (m_window.m_detachedVideoViewportQuickController && m_window.m_controller)
    {
        m_window.m_detachedVideoViewportQuickController->setPresentedFrame(
            m_window.m_lastPresentedFrame,
            m_window.m_controller->currentVideoFrame(),
            m_window.m_controller->videoFrameSize());
        m_window.m_detachedVideoViewportQuickController->setOverlays(m_window.m_controller->currentOverlays());
        m_window.m_detachedVideoViewportQuickController->setShowAllLabels(
            m_window.m_videoViewportQuickController
                ? m_window.m_videoViewportQuickController->showAllLabels()
                : false);
        m_window.m_detachedVideoViewportQuickController->setDisplayScaleFactor(
            m_window.m_videoViewportQuickController
                ? m_window.m_videoViewportQuickController->displayScaleFactor()
                : 1.0);
    }

    if (!m_window.m_detachedVideoWindowGeometry.isEmpty())
    {
        restoreWindowGeometry(m_window.m_detachedVideoWindow, m_window.m_detachedVideoWindowGeometry);
    }
    else
    {
        m_window.m_detachedVideoWindow->resize(QSize{1280, 720});
    }
    m_window.m_videoDetached = true;
    if (m_window.m_shellLayoutController)
    {
        m_window.m_shellLayoutController->setVideoDetached(true);
    }
    updateDetachedVideoUiState();
    syncMainVerticalPanelSizes();
    m_window.m_detachedVideoWindow->show();
    m_window.m_detachedVideoWindow->raise();
    m_window.m_detachedVideoWindow->requestActivate();
    if (!m_window.m_projectStateChangeInProgress && m_window.hasOpenProject())
    {
        m_window.setProjectDirty(true);
    }
    if (!m_window.m_projectStateChangeInProgress)
    {
        m_window.showStatus(QStringLiteral("Video detached."));
    }
}

void PanelLayoutController::attachVideo()
{
    if (!m_window.m_videoDetached || !m_window.m_detachedVideoWindow)
    {
        return;
    }

    m_window.m_detachedVideoWindowGeometry = saveWindowGeometry(m_window.m_detachedVideoWindow);

    m_window.m_detachedVideoWindow->hide();
    m_window.m_videoDetached = false;
    if (m_window.m_controller)
    {
        m_window.m_controller->setNativeVideoPresentationEnabled(shouldUseNativeVideoPresentation());
    }
    if (m_window.m_videoViewportQuickController)
    {
        m_window.m_videoViewportQuickController->setNativePresentationEnabled(
            shouldUseNativeVideoPresentation());
    }
    if (m_window.m_detachedVideoViewportQuickController)
    {
        m_window.m_detachedVideoViewportQuickController->setNativePresentationEnabled(false);
    }
    if (m_window.m_shellLayoutController)
    {
        m_window.m_shellLayoutController->setVideoDetached(false);
    }
    updateDetachedVideoUiState();
    syncMainVerticalPanelSizes();
    if (m_window.m_videoViewportQuickWidget)
    {
        m_window.m_videoViewportQuickWidget->forceActiveFocus();
    }
    if (!m_window.m_projectStateChangeInProgress && m_window.hasOpenProject())
    {
        m_window.setProjectDirty(true);
    }
    if (!m_window.m_projectStateChangeInProgress)
    {
        m_window.showStatus(QStringLiteral("Video attached."));
    }
}

void PanelLayoutController::detachTimeline()
{
    if (m_window.m_timelineDetached || !m_window.m_detachedTimelineWindow || !m_window.m_showTimelineAction)
    {
        return;
    }

    if (!m_window.m_showTimelineAction->isChecked())
    {
        const QSignalBlocker blocker{m_window.m_showTimelineAction};
        m_window.m_showTimelineAction->setChecked(true);
    }

    if (!m_window.m_detachedTimelineWindowGeometry.isEmpty())
    {
        restoreWindowGeometry(m_window.m_detachedTimelineWindow, m_window.m_detachedTimelineWindowGeometry);
    }
    else
    {
        const auto panelSize = m_window.m_timelineQuickWidget
            ? QSize(
                static_cast<int>(std::lround(m_window.m_timelineQuickWidget->width())),
                static_cast<int>(std::lround(m_window.m_timelineQuickWidget->height())))
            : QSize{1100, 260};
        m_window.m_detachedTimelineWindow->resize(panelSize.expandedTo(QSize{720, 220}));
    }

    m_window.m_timelineDetached = true;
    updateTimelineVisibility(true);
    updateDetachedPanelUiState();
    m_window.m_detachedTimelineWindow->show();
    m_window.m_detachedTimelineWindow->raise();
    m_window.m_detachedTimelineWindow->requestActivate();
    if (!m_window.m_projectStateChangeInProgress && m_window.hasOpenProject())
    {
        m_window.setProjectDirty(true);
    }
    if (!m_window.m_projectStateChangeInProgress)
    {
        m_window.showStatus(QStringLiteral("Timeline detached."));
    }
}

void PanelLayoutController::attachTimeline()
{
    if (!m_window.m_timelineDetached || !m_window.m_detachedTimelineWindow)
    {
        return;
    }

    m_window.m_detachedTimelineWindowGeometry = saveWindowGeometry(m_window.m_detachedTimelineWindow);
    m_window.m_detachedTimelineWindow->hide();
    m_window.m_timelineDetached = false;
    updateTimelineVisibility(m_window.m_showTimelineAction && m_window.m_showTimelineAction->isChecked());
    updateDetachedPanelUiState();
    if (m_window.m_timelineQuickWidget && m_window.m_timelineQuickWidget->isVisible())
    {
        m_window.m_timelineQuickWidget->forceActiveFocus();
    }
    if (!m_window.m_projectStateChangeInProgress && m_window.hasOpenProject())
    {
        m_window.setProjectDirty(true);
    }
    if (!m_window.m_projectStateChangeInProgress)
    {
        m_window.showStatus(QStringLiteral("Timeline attached."));
    }
}

void PanelLayoutController::detachClipEditor()
{
    if (m_window.m_clipEditorDetached || !m_window.m_detachedClipEditorWindow || !m_window.m_showClipEditorAction)
    {
        return;
    }

    if (!m_window.m_showClipEditorAction->isChecked())
    {
        const QSignalBlocker blocker{m_window.m_showClipEditorAction};
        m_window.m_showClipEditorAction->setChecked(true);
    }

    if (!m_window.m_detachedClipEditorWindowGeometry.isEmpty())
    {
        restoreWindowGeometry(m_window.m_detachedClipEditorWindow, m_window.m_detachedClipEditorWindowGeometry);
    }
    else
    {
        const auto panelSize = m_window.m_clipEditorQuickWidget
            ? QSize(
                static_cast<int>(std::lround(m_window.m_clipEditorQuickWidget->width())),
                static_cast<int>(std::lround(m_window.m_clipEditorQuickWidget->height())))
            : QSize{960, 340};
        m_window.m_detachedClipEditorWindow->resize(panelSize.expandedTo(QSize{720, 260}));
    }

    m_window.m_clipEditorDetached = true;
    updateClipEditorVisibility(true);
    updateDetachedPanelUiState();
    m_window.m_detachedClipEditorWindow->show();
    m_window.m_detachedClipEditorWindow->raise();
    m_window.m_detachedClipEditorWindow->requestActivate();
    if (!m_window.m_projectStateChangeInProgress && m_window.hasOpenProject())
    {
        m_window.setProjectDirty(true);
    }
    if (!m_window.m_projectStateChangeInProgress)
    {
        m_window.showStatus(QStringLiteral("Clip editor detached."));
    }
}

void PanelLayoutController::attachClipEditor()
{
    if (!m_window.m_clipEditorDetached || !m_window.m_detachedClipEditorWindow)
    {
        return;
    }

    m_window.m_detachedClipEditorWindowGeometry = saveWindowGeometry(m_window.m_detachedClipEditorWindow);
    m_window.m_detachedClipEditorWindow->hide();
    m_window.m_clipEditorDetached = false;
    updateClipEditorVisibility(m_window.m_showClipEditorAction && m_window.m_showClipEditorAction->isChecked());
    updateDetachedPanelUiState();
    if (m_window.m_clipEditorQuickWidget && m_window.m_clipEditorQuickWidget->isVisible())
    {
        m_window.m_clipEditorQuickWidget->forceActiveFocus();
    }
    if (!m_window.m_projectStateChangeInProgress && m_window.hasOpenProject())
    {
        m_window.setProjectDirty(true);
    }
    if (!m_window.m_projectStateChangeInProgress)
    {
        m_window.showStatus(QStringLiteral("Clip editor attached."));
    }
}

void PanelLayoutController::detachMix()
{
    if (m_window.m_mixDetached || !m_window.m_detachedMixWindow || !m_window.m_showMixAction)
    {
        return;
    }

    if (!m_window.m_showMixAction->isChecked())
    {
        const QSignalBlocker blocker{m_window.m_showMixAction};
        m_window.m_showMixAction->setChecked(true);
    }

    if (!m_window.m_detachedMixWindowGeometry.isEmpty())
    {
        restoreWindowGeometry(m_window.m_detachedMixWindow, m_window.m_detachedMixWindowGeometry);
    }
    else
    {
        const auto panelSize = m_window.m_mixQuickWidget
            ? QSize(
                static_cast<int>(std::lround(m_window.m_mixQuickWidget->width())),
                static_cast<int>(std::lround(m_window.m_mixQuickWidget->height())))
            : QSize{1100, 420};
        m_window.m_detachedMixWindow->resize(panelSize.expandedTo(QSize{780, 280}));
    }

    m_window.m_mixDetached = true;
    updateMixVisibility(true);
    updateDetachedPanelUiState();
    m_window.m_detachedMixWindow->show();
    m_window.m_detachedMixWindow->raise();
    m_window.m_detachedMixWindow->requestActivate();
    if (!m_window.m_projectStateChangeInProgress && m_window.hasOpenProject())
    {
        m_window.setProjectDirty(true);
    }
    if (!m_window.m_projectStateChangeInProgress)
    {
        m_window.showStatus(QStringLiteral("Mixer detached."));
    }
}

void PanelLayoutController::attachMix()
{
    if (!m_window.m_mixDetached || !m_window.m_detachedMixWindow)
    {
        return;
    }

    m_window.m_detachedMixWindowGeometry = saveWindowGeometry(m_window.m_detachedMixWindow);
    m_window.m_detachedMixWindow->hide();
    m_window.m_mixDetached = false;
    updateMixVisibility(m_window.m_showMixAction && m_window.m_showMixAction->isChecked());
    updateDetachedPanelUiState();
    if (m_window.m_mixQuickWidget && m_window.m_mixQuickWidget->isVisible())
    {
        m_window.m_mixQuickWidget->forceActiveFocus();
    }
    if (!m_window.m_projectStateChangeInProgress && m_window.hasOpenProject())
    {
        m_window.setProjectDirty(true);
    }
    if (!m_window.m_projectStateChangeInProgress)
    {
        m_window.showStatus(QStringLiteral("Mixer attached."));
    }
}

void PanelLayoutController::detachAudioPool()
{
    if (m_window.m_audioPoolDetached || !m_window.m_detachedAudioPoolWindow || !m_window.m_audioPoolAction)
    {
        return;
    }

    if (!m_window.m_audioPoolAction->isChecked())
    {
        const QSignalBlocker blocker{m_window.m_audioPoolAction};
        m_window.m_audioPoolAction->setChecked(true);
    }

    if (!m_window.m_detachedAudioPoolWindowGeometry.isEmpty())
    {
        restoreWindowGeometry(m_window.m_detachedAudioPoolWindow, m_window.m_detachedAudioPoolWindowGeometry);
    }
    else
    {
        const auto panelSize = m_window.m_audioPoolQuickWidget
            ? QSize(
                static_cast<int>(std::lround(m_window.m_audioPoolQuickWidget->width())),
                static_cast<int>(std::lround(m_window.m_audioPoolQuickWidget->height())))
            : QSize{420, 640};
        m_window.m_detachedAudioPoolWindow->resize(panelSize.expandedTo(QSize{320, 420}));
    }

    m_window.m_audioPoolDetached = true;
    updateAudioPoolVisibility(true);
    updateDetachedPanelUiState();
    m_window.m_detachedAudioPoolWindow->show();
    m_window.m_detachedAudioPoolWindow->raise();
    m_window.m_detachedAudioPoolWindow->requestActivate();
    if (!m_window.m_projectStateChangeInProgress && m_window.hasOpenProject())
    {
        m_window.setProjectDirty(true);
    }
    if (!m_window.m_projectStateChangeInProgress)
    {
        m_window.showStatus(QStringLiteral("Audio Pool detached."));
    }
}

void PanelLayoutController::attachAudioPool()
{
    if (!m_window.m_audioPoolDetached || !m_window.m_detachedAudioPoolWindow)
    {
        return;
    }

    m_window.m_detachedAudioPoolWindowGeometry = saveWindowGeometry(m_window.m_detachedAudioPoolWindow);
    m_window.m_detachedAudioPoolWindow->hide();
    m_window.m_audioPoolDetached = false;
    updateAudioPoolVisibility(m_window.m_audioPoolAction && m_window.m_audioPoolAction->isChecked());
    updateDetachedPanelUiState();
    if (m_window.m_audioPoolQuickWidget && m_window.m_audioPoolQuickWidget->isVisible())
    {
        m_window.m_audioPoolQuickWidget->forceActiveFocus();
    }
    if (!m_window.m_projectStateChangeInProgress && m_window.hasOpenProject())
    {
        m_window.setProjectDirty(true);
    }
    if (!m_window.m_projectStateChangeInProgress)
    {
        m_window.showStatus(QStringLiteral("Audio Pool attached."));
    }
}

void PanelLayoutController::updateDetachedVideoUiState()
{
    const auto nativePresentationEnabled = shouldUseNativeVideoPresentation();
    if (m_window.m_controller)
    {
        m_window.m_controller->setNativeVideoPresentationEnabled(nativePresentationEnabled);
    }
    if (m_window.m_videoViewportQuickController)
    {
        m_window.m_videoViewportQuickController->setNativePresentationEnabled(nativePresentationEnabled);
    }
    if (m_window.m_detachedVideoViewportQuickController)
    {
        m_window.m_detachedVideoViewportQuickController->setNativePresentationEnabled(false);
    }

    if (m_window.m_detachVideoAction)
    {
        m_window.m_detachVideoAction->setText(
            m_window.m_videoDetached ? QStringLiteral("Attach Video") : QStringLiteral("Detach Video"));
        m_window.m_detachVideoAction->setEnabled(m_window.m_videoViewportQuickWidget != nullptr);
    }
}

void PanelLayoutController::updateDetachedPanelUiState()
{
    updateDetachedVideoUiState();

    if (m_window.m_detachTimelineAction)
    {
        m_window.m_detachTimelineAction->setText(
            m_window.m_timelineDetached ? QStringLiteral("Attach Timeline") : QStringLiteral("Detach Timeline"));
        m_window.m_detachTimelineAction->setEnabled(m_window.m_timelineQuickWidget != nullptr);
    }
    if (m_window.m_detachClipEditorAction)
    {
        m_window.m_detachClipEditorAction->setText(
            m_window.m_clipEditorDetached ? QStringLiteral("Attach Clip Editor") : QStringLiteral("Detach Clip Editor"));
        m_window.m_detachClipEditorAction->setEnabled(m_window.m_clipEditorQuickWidget != nullptr);
    }
    if (m_window.m_detachMixAction)
    {
        m_window.m_detachMixAction->setText(
            m_window.m_mixDetached ? QStringLiteral("Attach Mixer") : QStringLiteral("Detach Mixer"));
        m_window.m_detachMixAction->setEnabled(m_window.m_mixQuickWidget != nullptr);
    }
    if (m_window.m_detachAudioPoolAction)
    {
        m_window.m_detachAudioPoolAction->setText(
            m_window.m_audioPoolDetached ? QStringLiteral("Attach Audio Pool") : QStringLiteral("Detach Audio Pool"));
        m_window.m_detachAudioPoolAction->setEnabled(m_window.m_audioPoolQuickWidget != nullptr);
    }
}

void PanelLayoutController::syncMainVerticalPanelSizes()
{
    if (!m_window.m_shellLayoutController)
    {
        return;
    }
    m_window.m_shellLayoutController->setVideoDetached(m_window.m_videoDetached);
    m_window.m_shellLayoutController->setPreferredSizes(
        m_window.m_audioPoolPreferredWidth,
        m_window.m_timelinePreferredHeight,
        m_window.m_clipEditorPreferredHeight,
        m_window.m_nodeEditorPreferredHeight,
        m_window.m_mixPreferredHeight);
    m_window.m_shellLayoutController->setThumbnailsVisible(
        !m_window.m_showTimelineThumbnailsAction || m_window.m_showTimelineThumbnailsAction->isChecked());
    m_window.m_shellLayoutController->setTimelineVisible(
        m_window.m_showTimelineAction && m_window.m_showTimelineAction->isChecked() && !m_window.m_timelineDetached);
    m_window.m_shellLayoutController->setClipEditorVisible(
        m_window.m_showClipEditorAction && m_window.m_showClipEditorAction->isChecked() && !m_window.m_clipEditorDetached);
    m_window.m_shellLayoutController->setNodeEditorVisible(
        m_window.m_showNodeEditorAction && m_window.m_showNodeEditorAction->isChecked());
    m_window.m_shellLayoutController->setMixVisible(
        m_window.m_showMixAction && m_window.m_showMixAction->isChecked() && !m_window.m_mixDetached);
    m_window.m_shellLayoutController->setAudioPoolVisible(
        m_window.m_audioPoolAction && m_window.m_audioPoolAction->isChecked() && !m_window.m_audioPoolDetached);
    m_window.syncShellPanelGeometry();
}

bool PanelLayoutController::shouldUseNativeVideoPresentation() const
{
    // Keep the attached viewport on the CPU-image Quick path for now.
    // The native attached presentation path has been fragile and currently
    // regressed visible playback, while detached playback remains stable
    // without it.
    return false;
}
