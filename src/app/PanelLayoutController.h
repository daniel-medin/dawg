#pragma once

#include "app/ProjectDocument.h"

class MainWindow;

class PanelLayoutController
{
public:
    explicit PanelLayoutController(MainWindow& window);

    [[nodiscard]] dawg::project::UiState snapshotProjectUiState() const;
    void applyProjectUiState(const dawg::project::UiState& state);
    void updateAudioPoolVisibility(bool visible);
    void updateTimelineVisibility(bool visible);
    void updateClipEditorVisibility(bool visible);
    void updateMixVisibility(bool visible);
    void detachVideo();
    void attachVideo();
    void detachTimeline();
    void attachTimeline();
    void detachClipEditor();
    void attachClipEditor();
    void detachMix();
    void attachMix();
    void detachAudioPool();
    void attachAudioPool();
    void updateDetachedVideoUiState();
    void updateDetachedPanelUiState();
    void syncMainVerticalPanelSizes();

private:
    [[nodiscard]] bool shouldUseNativeVideoPresentation() const;
    MainWindow& m_window;
};
