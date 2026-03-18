#pragma once

#include <optional>

#include <QString>
class MainWindow;

class MediaImportController
{
public:
    explicit MediaImportController(MainWindow& window);

    [[nodiscard]] bool ensureProjectForMediaAction(const QString& actionLabel);
    [[nodiscard]] std::optional<QString> copyMediaIntoProject(
        const QString& sourcePath,
        const QString& subdirectory,
        QString* errorMessage = nullptr) const;
    [[nodiscard]] QString chooseOpenFileName(
        const QString& title,
        const QString& directory,
        const QString& filter) const;
    [[nodiscard]] QString chooseExistingDirectory(
        const QString& title,
        const QString& directory = {}) const;

    void openVideo();
    void importSound();
    void importAudioToPool();

private:
    MainWindow& m_window;
};
