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
    void updateDetachedVideoUiState();
    void syncMainVerticalPanelSizes();

private:
    MainWindow& m_window;
};
