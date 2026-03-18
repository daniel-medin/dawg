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
    const auto timelineVisible = m_window.m_shellLayoutController
        ? m_window.m_shellLayoutController->timelineVisible()
        : (m_window.m_timelineQuickWidget && m_window.m_timelineQuickWidget->isVisible());
    const auto clipEditorVisible = m_window.m_shellLayoutController
        ? m_window.m_shellLayoutController->clipEditorVisible()
        : (m_window.m_clipEditorQuickWidget && m_window.m_clipEditorQuickWidget->isVisible());
    const auto mixVisible = m_window.m_shellLayoutController
        ? m_window.m_shellLayoutController->mixVisible()
        : (m_window.m_mixQuickWidget && m_window.m_mixQuickWidget->isVisible());
    const auto audioPoolVisible = m_window.m_shellLayoutController
        ? m_window.m_shellLayoutController->audioPoolVisible()
        : (m_window.m_audioPoolQuickWidget && m_window.m_audioPoolQuickWidget->isVisible());

    dawg::project::UiState state;
    state.videoDetached = m_window.m_videoDetached;
    state.detachedVideoWindowGeometry = m_window.m_videoDetached && m_window.m_detachedVideoWindow
                                            ? saveWindowGeometry(m_window.m_detachedVideoWindow)
                                            : m_window.m_detachedVideoWindowGeometry;
    state.timelineVisible = timelineVisible;
    state.clipEditorVisible = clipEditorVisible;
    state.mixVisible = mixVisible;
    state.audioPoolVisible = audioPoolVisible;
    state.audioPoolShowLength = m_window.m_audioPoolShowLength;
    state.audioPoolShowSize = m_window.m_audioPoolShowSize;
    state.showAllNodeNames = m_window.m_showAllNodeNamesAction && m_window.m_showAllNodeNamesAction->isChecked();
    state.timelineClickSeeks = m_window.m_timelineClickSeeksAction && m_window.m_timelineClickSeeksAction->isChecked();
    state.audioPoolPreferredWidth = m_window.m_audioPoolPreferredWidth;
    state.timelinePreferredHeight = m_window.m_timelinePreferredHeight;
    state.clipEditorPreferredHeight = m_window.m_clipEditorPreferredHeight;
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
        << "mixVisible=" << state.mixVisible
        << "audioPoolVisible=" << state.audioPoolVisible;
    m_window.m_projectStateChangeInProgress = true;
    m_window.m_detachedVideoWindowGeometry = state.detachedVideoWindowGeometry;
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

    updateTimelineVisibility(state.timelineVisible);
    updateClipEditorVisibility(state.clipEditorVisible);
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

    m_window.syncShellLayoutViewport();

    qInfo().noquote()
        << "Applied UI state:"
        << "timelineVisible=" << (m_window.m_shellLayoutController && m_window.m_shellLayoutController->timelineVisible())
        << "clipEditorVisible=" << (m_window.m_shellLayoutController && m_window.m_shellLayoutController->clipEditorVisible())
        << "mixVisible=" << (m_window.m_shellLayoutController && m_window.m_shellLayoutController->mixVisible())
        << "audioPoolVisible=" << (m_window.m_shellLayoutController && m_window.m_shellLayoutController->audioPoolVisible())
        << "preferredAudioPoolWidth=" << m_window.m_audioPoolPreferredWidth
        << "preferredTimelineHeight=" << m_window.m_timelinePreferredHeight
        << "preferredClipEditorHeight=" << m_window.m_clipEditorPreferredHeight
        << "preferredMixHeight=" << m_window.m_mixPreferredHeight;

    m_window.m_projectStateChangeInProgress = false;
}

void PanelLayoutController::updateAudioPoolVisibility(const bool visible)
{
    if (m_window.m_audioPoolQuickWidget)
    {
        if (!visible)
        {
            m_window.m_audioPoolPreferredWidth =
                std::max(240, static_cast<int>(std::lround(m_window.m_audioPoolQuickWidget->width())));
        }
        m_window.m_audioPoolQuickWidget->setVisible(visible);
    }

    if (m_window.m_shellLayoutController)
    {
        m_window.m_shellLayoutController->setAudioPoolVisible(visible);
        m_window.m_shellLayoutController->setPreferredSizes(
            m_window.m_audioPoolPreferredWidth,
            m_window.m_timelinePreferredHeight,
            m_window.m_clipEditorPreferredHeight,
            m_window.m_mixPreferredHeight);
    }
    m_window.syncShellPanelGeometry();

    if (m_window.m_audioPoolAction && m_window.m_audioPoolAction->isChecked() != visible)
    {
        const QSignalBlocker blocker{m_window.m_audioPoolAction};
        m_window.m_audioPoolAction->setChecked(visible);
    }
}

void PanelLayoutController::updateTimelineVisibility(const bool visible)
{
    if (m_window.m_timelineQuickWidget)
    {
        if (!visible)
        {
            m_window.m_timelinePreferredHeight = std::max(
                m_window.timelineMinimumHeight(),
                static_cast<int>(std::lround(m_window.m_timelineQuickWidget->height())));
        }
        m_window.m_timelineQuickWidget->setVisible(visible);
    }

    if (m_window.m_shellLayoutController)
    {
        m_window.m_shellLayoutController->setTimelineVisible(visible);
        m_window.m_shellLayoutController->setPreferredSizes(
            m_window.m_audioPoolPreferredWidth,
            m_window.m_timelinePreferredHeight,
            m_window.m_clipEditorPreferredHeight,
            m_window.m_mixPreferredHeight);
    }
    syncMainVerticalPanelSizes();

    if (m_window.m_showTimelineAction && m_window.m_showTimelineAction->isChecked() != visible)
    {
        const QSignalBlocker blocker{m_window.m_showTimelineAction};
        m_window.m_showTimelineAction->setChecked(visible);
    }
}

void PanelLayoutController::updateClipEditorVisibility(const bool visible)
{
    if (m_window.m_clipEditorQuickWidget)
    {
        if (!visible)
        {
            m_window.m_clipEditorPreferredHeight =
                std::max(148, static_cast<int>(std::lround(m_window.m_clipEditorQuickWidget->height())));
        }
        m_window.m_clipEditorQuickWidget->setVisible(visible);
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
        m_window.m_shellLayoutController->setClipEditorVisible(visible);
        m_window.m_shellLayoutController->setPreferredSizes(
            m_window.m_audioPoolPreferredWidth,
            m_window.m_timelinePreferredHeight,
            m_window.m_clipEditorPreferredHeight,
            m_window.m_mixPreferredHeight);
    }
    syncMainVerticalPanelSizes();

    if (m_window.m_showClipEditorAction && m_window.m_showClipEditorAction->isChecked() != visible)
    {
        const QSignalBlocker blocker{m_window.m_showClipEditorAction};
        m_window.m_showClipEditorAction->setChecked(visible);
    }
}

void PanelLayoutController::updateMixVisibility(const bool visible)
{
    if (m_window.m_mixQuickWidget)
    {
        if (!visible)
        {
            m_window.m_mixPreferredHeight =
                std::max(132, static_cast<int>(std::lround(m_window.m_mixQuickWidget->height())));
        }
        m_window.m_mixQuickWidget->setVisible(visible);
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
        m_window.m_shellLayoutController->setMixVisible(visible);
        m_window.m_shellLayoutController->setPreferredSizes(
            m_window.m_audioPoolPreferredWidth,
            m_window.m_timelinePreferredHeight,
            m_window.m_clipEditorPreferredHeight,
            m_window.m_mixPreferredHeight);
    }
    syncMainVerticalPanelSizes();

    if (m_window.m_showMixAction && m_window.m_showMixAction->isChecked() != visible)
    {
        const QSignalBlocker blocker{m_window.m_showMixAction};
        m_window.m_showMixAction->setChecked(visible);
    }
}

void PanelLayoutController::detachVideo()
{
    if (m_window.m_videoDetached || !m_window.m_detachedVideoWindow)
    {
        return;
    }

    if (!m_window.m_detachedVideoWindowGeometry.isEmpty())
    {
        restoreWindowGeometry(m_window.m_detachedVideoWindow, m_window.m_detachedVideoWindowGeometry);
    }
    else
    {
        const auto canvasSize = m_window.m_videoViewportQuickWidget
            ? QSize(
                static_cast<int>(std::lround(m_window.m_videoViewportQuickWidget->width())),
                static_cast<int>(std::lround(m_window.m_videoViewportQuickWidget->height())))
            : QSize{960, 540};
        m_window.m_detachedVideoWindow->resize(canvasSize.expandedTo(QSize{640, 360}));
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

void PanelLayoutController::updateDetachedVideoUiState()
{
    if (m_window.m_detachVideoAction)
    {
        m_window.m_detachVideoAction->setText(
            m_window.m_videoDetached ? QStringLiteral("Attach Video") : QStringLiteral("Detach Video"));
        m_window.m_detachVideoAction->setEnabled(m_window.m_videoViewportQuickWidget != nullptr);
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
        m_window.m_mixPreferredHeight);
    m_window.syncShellPanelGeometry();
}
