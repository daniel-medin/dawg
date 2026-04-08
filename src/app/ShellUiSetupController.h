#pragma once

class MainWindow;

class ShellUiSetupController
{
public:
    explicit ShellUiSetupController(MainWindow& window);

    void buildUi();

private:
    void configureWindow() const;
    void createShortcuts() const;
    void createControllers();
    void configureNodeEditorSessions() const;
    void connectNodeEditorSignals() const;
    void configureQuickShell() const;
    void bindShellItems() const;
    void connectShellLayoutSignals() const;
    void createDetachedWindows() const;
    void initializeShellLayoutDefaults() const;
    void connectOverlayControllers() const;
    void createDebugOverlay() const;
    void connectSceneGraphInitialization() const;
    void finalizeBuild() const;

    MainWindow& m_window;
};
