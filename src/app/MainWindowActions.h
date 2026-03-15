#pragma once

class MainWindow;

class MainWindowActions
{
public:
    explicit MainWindowActions(MainWindow& window);

    void buildMenus();
    void updateSelectionState(bool hasSelection);
    void updateTrackAvailabilityState(bool hasTracks);
    void updateEditActionState();

private:
    MainWindow& m_window;
};
