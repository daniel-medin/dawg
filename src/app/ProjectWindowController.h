#pragma once

#include <optional>

#include <QStringList>

#include "app/ProjectDocument.h"

class MainWindow;

class ProjectWindowController
{
public:
    explicit ProjectWindowController(MainWindow& window);

    [[nodiscard]] bool hasOpenProject() const;
    void clearCurrentProject();
    void setCurrentProject(const QString& projectFilePath, const QString& projectName);
    void setProjectDirty(bool dirty);
    void updateWindowTitle();
    void restoreLastProjectOnStartup();

    [[nodiscard]] QStringList recentProjectPaths() const;
    void storeRecentProjectPaths(const QStringList& projectPaths);
    void addRecentProjectPath(const QString& projectFilePath);
    void removeRecentProjectPath(const QString& projectFilePath);
    void rebuildRecentProjectsMenu();

    [[nodiscard]] bool promptToSaveIfDirty(const QString& actionLabel);
    [[nodiscard]] bool saveProjectToCurrentPath();
    [[nodiscard]] bool saveProjectToPath(const QString& projectFilePath, const QString& projectName);
    [[nodiscard]] bool saveProjectAsNewCopy();
    [[nodiscard]] bool loadProjectFile(const QString& projectFilePath);
    [[nodiscard]] bool openProjectFileWithPrompt(const QString& projectFilePath, const QString& actionLabel);
    [[nodiscard]] bool createProjectAt(const QString& projectName, const QString& parentDirectory);

    void newProject();
    void openProject();
    void saveProject();
    void saveProjectAs();

private:
    MainWindow& m_window;
};
