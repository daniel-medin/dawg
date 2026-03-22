#include "app/MediaImportController.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

#include "app/DialogController.h"
#include "app/FilePickerController.h"
#include "app/MainWindow.h"
#include "app/PlayerController.h"

namespace
{
QString uniqueTargetFilePath(const QString& targetDirectoryPath, const QString& sourceFilePath)
{
    const QFileInfo sourceInfo(sourceFilePath);
    const auto completeBaseName = sourceInfo.completeBaseName();
    const auto suffix = sourceInfo.suffix();
    const auto extension = suffix.isEmpty() ? QString{} : QStringLiteral(".") + suffix;
    QDir targetDirectory(targetDirectoryPath);

    auto candidatePath = targetDirectory.filePath(sourceInfo.fileName());
    if (!QFileInfo::exists(candidatePath))
    {
        return candidatePath;
    }

    for (int index = 2;; ++index)
    {
        candidatePath = targetDirectory.filePath(
            QStringLiteral("%1 (%2)%3").arg(completeBaseName).arg(index).arg(extension));
        if (!QFileInfo::exists(candidatePath))
        {
            return candidatePath;
        }
    }
}

bool pathIsInsideRoot(const QString& rootPath, const QString& candidatePath)
{
    const auto cleanedRoot = QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(rootPath).absoluteFilePath()));
    const auto cleanedCandidate =
        QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(candidatePath).absoluteFilePath()));
#ifdef Q_OS_WIN
    const auto compareSensitivity = Qt::CaseInsensitive;
#else
    const auto compareSensitivity = Qt::CaseSensitive;
#endif
    const auto rootPrefix = cleanedRoot.endsWith(QLatin1Char('/')) ? cleanedRoot : (cleanedRoot + QLatin1Char('/'));
    return QString::compare(cleanedCandidate, cleanedRoot, compareSensitivity) == 0
        || cleanedCandidate.startsWith(rootPrefix, compareSensitivity);
}
}

MediaImportController::MediaImportController(MainWindow& window)
    : m_window(window)
{
}

bool MediaImportController::ensureProjectForMediaAction(const QString& actionLabel)
{
    if (m_window.hasOpenProject())
    {
        return true;
    }

    const auto result = m_window.m_dialogController->execMessage(
        QStringLiteral("Project Required"),
        QStringLiteral("Create or open a project before you %1.").arg(actionLabel),
        {},
        {
            DialogController::Button::NewProject,
            DialogController::Button::OpenProject,
            DialogController::Button::Cancel,
        },
        DialogController::Button::NewProject);

    if (result == DialogController::Button::NewProject)
    {
        m_window.newProject();
    }
    else if (result == DialogController::Button::OpenProject)
    {
        m_window.openProject();
    }

    return m_window.hasOpenProject();
}

std::optional<QString> MediaImportController::copyMediaIntoProject(
    const QString& sourcePath,
    const QString& subdirectory,
    QString* errorMessage) const
{
    if (!m_window.hasOpenProject())
    {
        if (errorMessage)
        {
            *errorMessage = QStringLiteral("No project is open.");
        }
        return std::nullopt;
    }

    const QFileInfo sourceInfo(sourcePath);
    if (!sourceInfo.exists() || !sourceInfo.isFile())
    {
        if (errorMessage)
        {
            *errorMessage = QStringLiteral("Media file does not exist: %1").arg(sourcePath);
        }
        return std::nullopt;
    }

    const auto targetDirectoryPath = QDir(m_window.m_currentProjectRootPath).filePath(subdirectory);
    QDir rootDirectory(m_window.m_currentProjectRootPath);
    if (!rootDirectory.mkpath(subdirectory))
    {
        if (errorMessage)
        {
            *errorMessage = QStringLiteral("Failed to create project folder %1.").arg(targetDirectoryPath);
        }
        return std::nullopt;
    }

    if (pathIsInsideRoot(targetDirectoryPath, sourceInfo.absoluteFilePath()))
    {
        return QDir::cleanPath(sourceInfo.absoluteFilePath());
    }

    const auto targetPath = uniqueTargetFilePath(targetDirectoryPath, sourceInfo.absoluteFilePath());
    if (!QFile::copy(sourceInfo.absoluteFilePath(), targetPath))
    {
        if (errorMessage)
        {
            *errorMessage = QStringLiteral("Failed to copy %1 into the project.").arg(sourceInfo.fileName());
        }
        return std::nullopt;
    }

    return QDir::cleanPath(targetPath);
}

QString MediaImportController::chooseOpenFileName(
    const QString& title,
    const QString& directory,
    const QString& filter) const
{
    return m_window.m_filePickerController
        ? m_window.m_filePickerController->execOpenFile(title, directory, filter)
        : QString{};
}

QString MediaImportController::chooseExistingDirectory(const QString& title, const QString& directory) const
{
    const auto initialDirectory = directory.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::DesktopLocation)
        : directory;
    return m_window.m_filePickerController
        ? m_window.m_filePickerController->execOpenDirectory(title, initialDirectory)
        : QString{};
}

void MediaImportController::openVideo()
{
    if (!ensureProjectForMediaAction(QStringLiteral("import a video")))
    {
        return;
    }

    const auto currentProjectState = m_window.m_controller->snapshotProjectState();
    if (m_window.m_controller->hasVideoLoaded()
        || !currentProjectState.audioPoolAssetPaths.empty()
        || !currentProjectState.trackerState.tracks.empty())
    {
        const auto choice = m_window.m_dialogController->execMessage(
            QStringLiteral("Replace Project Video"),
            QStringLiteral("Opening a new video will clear the current nodes and audio pool for this project. Continue?"),
            {},
            {DialogController::Button::Yes, DialogController::Button::No},
            DialogController::Button::No);
        if (choice != DialogController::Button::Yes)
        {
            return;
        }
    }

    const auto filePath = chooseOpenFileName(
        QStringLiteral("Import Video"),
        QStandardPaths::writableLocation(QStandardPaths::MoviesLocation),
        QStringLiteral("Video Files (*.mp4 *.mov *.mkv *.avi);;All Files (*.*)"));
    if (filePath.isEmpty())
    {
        return;
    }

    QString errorMessage;
    const auto copiedFilePath = copyMediaIntoProject(filePath, QStringLiteral("video"), &errorMessage);
    if (!copiedFilePath.has_value())
    {
        m_window.m_dialogController->execMessage(
            QStringLiteral("Import Video"),
            errorMessage,
            {},
            {DialogController::Button::Ok});
        return;
    }

    m_window.m_projectStateChangeInProgress = true;
    const auto opened = m_window.m_controller->openVideo(*copiedFilePath);
    m_window.m_projectStateChangeInProgress = false;
    if (opened)
    {
        m_window.refreshTimeline();
        m_window.requestProjectTimelineThumbnailsGeneration();
        m_window.setProjectDirty(true);
    }
}

void MediaImportController::importSound()
{
    if (!ensureProjectForMediaAction(QStringLiteral("import audio")))
    {
        return;
    }

    if (!m_window.m_controller->hasSelection())
    {
        m_window.showStatus(QStringLiteral("Select a node before importing audio."));
        return;
    }

    const auto filePath = chooseOpenFileName(
        QStringLiteral("Import Audio"),
        QStandardPaths::writableLocation(QStandardPaths::MusicLocation),
        QStringLiteral("Audio Files (*.wav *.mp3 *.flac *.aif *.aiff *.m4a *.aac *.ogg);;All Files (*.*)"));
    if (filePath.isEmpty())
    {
        return;
    }

    QString errorMessage;
    const auto copiedFilePath = copyMediaIntoProject(filePath, QStringLiteral("audio"), &errorMessage);
    if (!copiedFilePath.has_value())
    {
        m_window.m_dialogController->execMessage(
            QStringLiteral("Import Audio"),
            errorMessage,
            {},
            {DialogController::Button::Ok});
        return;
    }

    m_window.m_controller->importSoundForSelectedTrack(*copiedFilePath);
}

void MediaImportController::importAudioToPool()
{
    if (!ensureProjectForMediaAction(QStringLiteral("import audio")))
    {
        return;
    }

    const auto filePath = chooseOpenFileName(
        QStringLiteral("Import Audio"),
        QStandardPaths::writableLocation(QStandardPaths::MusicLocation),
        QStringLiteral("Audio Files (*.wav *.mp3 *.flac *.aif *.aiff *.m4a *.aac *.ogg);;All Files (*.*)"));
    if (filePath.isEmpty())
    {
        return;
    }

    QString errorMessage;
    const auto copiedFilePath = copyMediaIntoProject(filePath, QStringLiteral("audio"), &errorMessage);
    if (!copiedFilePath.has_value())
    {
        m_window.m_dialogController->execMessage(
            QStringLiteral("Import Audio"),
            errorMessage,
            {},
            {DialogController::Button::Ok});
        return;
    }

    if (m_window.m_controller->importAudioToPool(*copiedFilePath))
    {
        m_window.showStatus(
            QStringLiteral("Imported %1 to the audio pool.").arg(QFileInfo(*copiedFilePath).fileName()));
    }
}
