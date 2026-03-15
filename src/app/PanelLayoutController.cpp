#include "app/PanelLayoutController.h"

#include <algorithm>
#include <array>

#include <QSignalBlocker>
#include <QSplitter>
#include <QVBoxLayout>

#include "app/MainWindow.h"
#include "app/PlayerController.h"
#include "ui/ClipEditorView.h"
#include "ui/MixView.h"
#include "ui/NativeVideoViewport.h"
#include "ui/TimelineView.h"
#include "ui/VideoCanvas.h"

PanelLayoutController::PanelLayoutController(MainWindow& window)
    : m_window(window)
{
}

dawg::project::UiState PanelLayoutController::snapshotProjectUiState() const
{
    dawg::project::UiState state;
    state.videoDetached = m_window.m_videoDetached;
    state.detachedVideoWindowGeometry = m_window.m_videoDetached && m_window.m_detachedVideoWindow
                                            ? m_window.m_detachedVideoWindow->saveGeometry()
                                            : m_window.m_detachedVideoWindowGeometry;
    state.timelineVisible = m_window.m_timelinePanel && m_window.m_timelinePanel->isVisible();
    state.clipEditorVisible = m_window.m_clipEditorPanel && m_window.m_clipEditorPanel->isVisible();
    state.mixVisible = m_window.m_mixPanel && m_window.m_mixPanel->isVisible();
    state.audioPoolVisible = m_window.m_audioPoolPanel && m_window.m_audioPoolPanel->isVisible();
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
    if (m_window.m_contentSplitter)
    {
        const auto sizes = m_window.m_contentSplitter->sizes();
        state.contentSplitterSizes.reserve(static_cast<std::size_t>(sizes.size()));
        for (const auto size : sizes)
        {
            state.contentSplitterSizes.push_back(size);
        }
    }
    if (m_window.m_mainVerticalSplitter)
    {
        const auto sizes = m_window.m_mainVerticalSplitter->sizes();
        state.mainVerticalSplitterSizes.reserve(static_cast<std::size_t>(sizes.size()));
        auto persistedSizes = sizes;
        if (m_window.m_videoDetached && persistedSizes.size() == 4)
        {
            persistedSizes[0] = std::max(400, m_window.m_canvasPanel ? m_window.m_canvasPanel->height() : 400);
        }
        for (const auto size : persistedSizes)
        {
            state.mainVerticalSplitterSizes.push_back(size);
        }
    }
    return state;
}

void PanelLayoutController::applyProjectUiState(const dawg::project::UiState& state)
{
    m_window.m_projectStateChangeInProgress = true;
    m_window.m_detachedVideoWindowGeometry = state.detachedVideoWindowGeometry;
    m_window.m_audioPoolPreferredWidth = std::max(240, state.audioPoolPreferredWidth);
    m_window.m_audioPoolShowLength = state.audioPoolShowLength;
    m_window.m_audioPoolShowSize = state.audioPoolShowSize;
    m_window.m_timelinePreferredHeight = std::max(96, state.timelinePreferredHeight);
    m_window.m_clipEditorPreferredHeight = std::max(148, state.clipEditorPreferredHeight);
    m_window.m_mixPreferredHeight = std::max(132, state.mixPreferredHeight);

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

    if (m_window.m_showAllNodeNamesAction)
    {
        const QSignalBlocker blocker{m_window.m_showAllNodeNamesAction};
        m_window.m_showAllNodeNamesAction->setChecked(state.showAllNodeNames);
    }
    m_window.m_canvas->setShowAllLabels(state.showAllNodeNames);
    if (m_window.m_nativeViewport)
    {
        m_window.m_nativeViewport->setShowAllLabels(state.showAllNodeNames);
    }

    if (m_window.m_timelineClickSeeksAction)
    {
        const QSignalBlocker blocker{m_window.m_timelineClickSeeksAction};
        m_window.m_timelineClickSeeksAction->setChecked(state.timelineClickSeeks);
    }
    if (m_window.m_timeline)
    {
        m_window.m_timeline->setSeekOnClickEnabled(state.timelineClickSeeks || !m_window.m_controller->isPlaying());
    }

    updateTimelineVisibility(state.timelineVisible);
    updateClipEditorVisibility(state.clipEditorVisible);
    updateMixVisibility(state.mixVisible);
    updateAudioPoolVisibility(state.audioPoolVisible);
    m_window.refreshAudioPool();

    if (m_window.m_contentSplitter && state.contentSplitterSizes.size() == 2)
    {
        QList<int> sizes;
        for (const auto size : state.contentSplitterSizes)
        {
            sizes.push_back(size);
        }
        m_window.m_contentSplitter->setSizes(sizes);
    }
    if (m_window.m_mainVerticalSplitter && state.mainVerticalSplitterSizes.size() == 4)
    {
        QList<int> sizes;
        for (const auto size : state.mainVerticalSplitterSizes)
        {
            sizes.push_back(size);
        }
        m_window.m_mainVerticalSplitter->setSizes(sizes);
    }
    else
    {
        syncMainVerticalPanelSizes();
    }

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

    m_window.m_projectStateChangeInProgress = false;
}

void PanelLayoutController::updateAudioPoolVisibility(const bool visible)
{
    if (m_window.m_audioPoolPanel)
    {
        if (!visible)
        {
            m_window.m_audioPoolPreferredWidth = std::max(240, m_window.m_audioPoolPanel->width());
        }
        m_window.m_audioPoolPanel->setVisible(visible);
    }

    if (visible && m_window.m_contentSplitter && m_window.m_audioPoolPanel)
    {
        const auto totalWidth = std::max(800, m_window.m_contentSplitter->width());
        const auto poolWidth = std::clamp(m_window.m_audioPoolPreferredWidth, 240, std::max(240, totalWidth / 2));
        m_window.m_contentSplitter->setSizes({std::max(400, totalWidth - poolWidth), poolWidth});
    }

    if (m_window.m_audioPoolAction && m_window.m_audioPoolAction->isChecked() != visible)
    {
        const QSignalBlocker blocker{m_window.m_audioPoolAction};
        m_window.m_audioPoolAction->setChecked(visible);
    }
}

void PanelLayoutController::updateTimelineVisibility(const bool visible)
{
    if (m_window.m_timelinePanel)
    {
        if (!visible)
        {
            m_window.m_timelinePreferredHeight = std::max(96, m_window.m_timelinePanel->height());
        }
        m_window.m_timelinePanel->setVisible(visible);
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
    if (m_window.m_clipEditorPanel)
    {
        if (!visible)
        {
            m_window.m_clipEditorPreferredHeight = std::max(148, m_window.m_clipEditorPanel->height());
        }
        m_window.m_clipEditorPanel->setVisible(visible);
    }

    if (visible)
    {
        m_window.refreshClipEditor();
    }
    else
    {
        m_window.m_clipEditorPreviewTimer.stop();
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
    if (m_window.m_mixPanel)
    {
        if (!visible)
        {
            m_window.m_mixPreferredHeight = std::max(132, m_window.m_mixPanel->height());
        }
        m_window.m_mixPanel->setVisible(visible);
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

    syncMainVerticalPanelSizes();

    if (m_window.m_showMixAction && m_window.m_showMixAction->isChecked() != visible)
    {
        const QSignalBlocker blocker{m_window.m_showMixAction};
        m_window.m_showMixAction->setChecked(visible);
    }
}

void PanelLayoutController::detachVideo()
{
    if (m_window.m_videoDetached || !m_window.m_canvas || !m_window.m_canvasPanel || !m_window.m_detachedVideoWindow)
    {
        return;
    }

    if (auto* panelLayout = qobject_cast<QVBoxLayout*>(m_window.m_canvasPanel->layout()))
    {
        panelLayout->removeWidget(m_window.m_canvas);
    }
    if (auto* detachedLayout = qobject_cast<QVBoxLayout*>(m_window.m_detachedVideoWindow->layout()))
    {
        detachedLayout->addWidget(m_window.m_canvas, 1);
    }

    if (!m_window.m_detachedVideoWindowGeometry.isEmpty())
    {
        m_window.m_detachedVideoWindow->restoreGeometry(m_window.m_detachedVideoWindowGeometry);
    }
    else
    {
        const auto canvasSize = m_window.m_canvasPanel->size().isValid()
            ? m_window.m_canvasPanel->size()
            : QSize{960, 540};
        m_window.m_detachedVideoWindow->resize(canvasSize.expandedTo(QSize{640, 360}));
    }
    m_window.m_canvasPanel->hide();
    m_window.m_videoDetached = true;
    updateDetachedVideoUiState();
    syncMainVerticalPanelSizes();
    m_window.m_detachedVideoWindow->show();
    m_window.m_detachedVideoWindow->raise();
    m_window.m_detachedVideoWindow->activateWindow();
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
    if (!m_window.m_videoDetached || !m_window.m_canvas || !m_window.m_canvasPanel || !m_window.m_detachedVideoWindow)
    {
        return;
    }

    m_window.m_detachedVideoWindowGeometry = m_window.m_detachedVideoWindow->saveGeometry();

    if (auto* detachedLayout = qobject_cast<QVBoxLayout*>(m_window.m_detachedVideoWindow->layout()))
    {
        detachedLayout->removeWidget(m_window.m_canvas);
    }
    if (auto* panelLayout = qobject_cast<QVBoxLayout*>(m_window.m_canvasPanel->layout()))
    {
        panelLayout->addWidget(m_window.m_canvas, 1);
    }

    m_window.m_detachedVideoWindow->hide();
    m_window.m_canvasPanel->show();
    m_window.m_videoDetached = false;
    updateDetachedVideoUiState();
    syncMainVerticalPanelSizes();
    m_window.m_canvas->setFocus(Qt::OtherFocusReason);
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
        m_window.m_detachVideoAction->setEnabled(m_window.m_canvas != nullptr);
    }
}

void PanelLayoutController::syncMainVerticalPanelSizes()
{
    if (!m_window.m_mainVerticalSplitter)
    {
        return;
    }

    const auto totalHeight = std::max(320, m_window.m_mainVerticalSplitter->height());
    const auto timelineVisible = m_window.m_timelinePanel && m_window.m_timelinePanel->isVisible();
    const auto clipEditorVisible = m_window.m_clipEditorPanel && m_window.m_clipEditorPanel->isVisible();
    const auto mixVisible = m_window.m_mixPanel && m_window.m_mixPanel->isVisible();

    struct PanelTarget
    {
        bool visible = false;
        int preferred = 0;
        int minimum = 0;
        int assigned = 0;
    };

    std::array<PanelTarget, 3> panels{{
        PanelTarget{timelineVisible, m_window.m_timelinePreferredHeight, 96, 0},
        PanelTarget{clipEditorVisible, m_window.m_clipEditorPreferredHeight, 148, 0},
        PanelTarget{mixVisible, m_window.m_mixPreferredHeight, 132, 0}
    }};

    const auto videoAttached = m_window.m_canvasPanel && m_window.m_canvasPanel->isVisible() && !m_window.m_videoDetached;
    const auto canvasMinimum = videoAttached ? 220 : 0;
    const auto availableForPanels = std::max(0, totalHeight - canvasMinimum);
    int assignedTotal = 0;
    for (auto& panel : panels)
    {
        if (!panel.visible)
        {
            continue;
        }

        panel.assigned = std::max(panel.minimum, panel.preferred);
        assignedTotal += panel.assigned;
    }

    int overflow = assignedTotal - availableForPanels;
    while (overflow > 0)
    {
        bool reducedAny = false;
        for (auto& panel : panels)
        {
            if (!panel.visible || panel.assigned <= panel.minimum)
            {
                continue;
            }

            --panel.assigned;
            --overflow;
            reducedAny = true;
            if (overflow <= 0)
            {
                break;
            }
        }

        if (!reducedAny)
        {
            break;
        }
    }

    const auto canvasHeight = videoAttached
        ? std::max(200, totalHeight - panels[0].assigned - panels[1].assigned - panels[2].assigned)
        : 0;
    m_window.m_mainVerticalSplitter->setSizes({canvasHeight, panels[0].assigned, panels[1].assigned, panels[2].assigned});
}
