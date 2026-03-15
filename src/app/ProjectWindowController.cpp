#include "app/ProjectWindowController.h"

#include <algorithm>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QRegularExpression>
#include <QSettings>

#include "app/MainWindow.h"
#include "app/PlayerController.h"

namespace
{
constexpr auto kLastProjectPathSettingsKey = "project/lastProjectPath";
constexpr auto kRecentProjectPathsSettingsKey = "project/recentProjectPaths";
constexpr int kMaxRecentProjectCount = 10;

Qt::CaseSensitivity projectPathCaseSensitivity()
{
#ifdef Q_OS_WIN
    return Qt::CaseInsensitive;
#else
    return Qt::CaseSensitive;
#endif
}

QString normalizeProjectFilePath(const QString& path)
{
    if (path.isEmpty())
    {
        return {};
    }

    return QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath()));
}

bool projectPathMatches(const QString& left, const QString& right)
{
    return QString::compare(left, right, projectPathCaseSensitivity()) == 0;
}

const QStringList& projectVideoExtensions()
{
    static const QStringList kExtensions{
        QStringLiteral("mp4"),
        QStringLiteral("mov"),
        QStringLiteral("mkv"),
        QStringLiteral("avi")
    };
    return kExtensions;
}

const QStringList& projectAudioExtensions()
{
    static const QStringList kExtensions{
        QStringLiteral("wav"),
        QStringLiteral("mp3"),
        QStringLiteral("flac"),
        QStringLiteral("aif"),
        QStringLiteral("aiff"),
        QStringLiteral("m4a"),
        QStringLiteral("aac"),
        QStringLiteral("ogg")
    };
    return kExtensions;
}

QStringList projectMediaFilesInDirectory(const QString& directoryPath, const QStringList& extensions)
{
    const QDir directory(directoryPath);
    if (!directory.exists())
    {
        return {};
    }

    QStringList files;
    const auto entries = directory.entryInfoList(QDir::Files, QDir::Name | QDir::IgnoreCase);
    files.reserve(entries.size());
    for (const auto& entry : entries)
    {
        if (extensions.contains(entry.suffix().toLower()))
        {
            files.push_back(QDir::cleanPath(entry.absoluteFilePath()));
        }
    }
    return files;
}

QString normalizedMediaCopyBaseName(const QFileInfo& fileInfo)
{
    auto baseName = fileInfo.completeBaseName();
    static const QRegularExpression copySuffixPattern(QStringLiteral(R"( \(\d+\)$)"));
    baseName.remove(copySuffixPattern);
    return baseName;
}

std::optional<QString> preferredRecoveredVideoPath(const QStringList& videoFiles)
{
    if (videoFiles.isEmpty())
    {
        return std::nullopt;
    }

    QStringList rankedFiles = videoFiles;
    std::sort(
        rankedFiles.begin(),
        rankedFiles.end(),
        [](const QString& leftPath, const QString& rightPath)
        {
            const QFileInfo leftInfo(leftPath);
            const QFileInfo rightInfo(rightPath);
            const auto leftNormalizedBase = normalizedMediaCopyBaseName(leftInfo);
            const auto rightNormalizedBase = normalizedMediaCopyBaseName(rightInfo);
            const auto leftIsCopiedName =
                QString::compare(leftInfo.completeBaseName(), leftNormalizedBase, Qt::CaseInsensitive) != 0;
            const auto rightIsCopiedName =
                QString::compare(rightInfo.completeBaseName(), rightNormalizedBase, Qt::CaseInsensitive) != 0;
            if (leftIsCopiedName != rightIsCopiedName)
            {
                return !leftIsCopiedName;
            }

            const auto leftNameCompare =
                QString::compare(leftNormalizedBase, rightNormalizedBase, Qt::CaseInsensitive);
            if (leftNameCompare != 0)
            {
                return leftNameCompare < 0;
            }

            if (leftInfo.fileName().size() != rightInfo.fileName().size())
            {
                return leftInfo.fileName().size() < rightInfo.fileName().size();
            }

            return QString::compare(leftInfo.fileName(), rightInfo.fileName(), Qt::CaseInsensitive) < 0;
        });
    return QDir::cleanPath(rankedFiles.front());
}

bool recoverProjectMediaFromFolders(
    dawg::project::ControllerState* state,
    const QString& projectRootPath,
    QString* message)
{
    if (!state)
    {
        return false;
    }

    bool recovered = false;
    if (state->videoPath.isEmpty())
    {
        const auto videoFiles =
            projectMediaFilesInDirectory(QDir(projectRootPath).filePath(QStringLiteral("video")), projectVideoExtensions());
        if (const auto recoveredVideoPath = preferredRecoveredVideoPath(videoFiles); recoveredVideoPath.has_value())
        {
            state->videoPath = *recoveredVideoPath;
            recovered = true;
        }
    }

    if (state->audioPoolAssetPaths.empty())
    {
        const auto audioFiles =
            projectMediaFilesInDirectory(QDir(projectRootPath).filePath(QStringLiteral("audio")), projectAudioExtensions());
        if (!audioFiles.isEmpty())
        {
            state->audioPoolAssetPaths.assign(audioFiles.cbegin(), audioFiles.cend());
            recovered = true;
        }
    }

    if (recovered && message)
    {
        QStringList recoveredParts;
        if (!state->videoPath.isEmpty())
        {
            recoveredParts.push_back(QStringLiteral("video"));
        }
        if (!state->audioPoolAssetPaths.empty())
        {
            recoveredParts.push_back(QStringLiteral("audio pool"));
        }
        *message = QStringLiteral("Recovered %1 from project folders. Save the project to persist the recovered state.")
            .arg(recoveredParts.join(QStringLiteral(" and ")));
    }
    return recovered;
}

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
}

ProjectWindowController::ProjectWindowController(MainWindow& window)
    : m_window(window)
{
}

bool ProjectWindowController::hasOpenProject() const
{
    return !m_window.m_currentProjectFilePath.isEmpty();
}

void ProjectWindowController::clearCurrentProject()
{
    m_window.m_currentProjectFilePath.clear();
    m_window.m_currentProjectRootPath.clear();
    m_window.m_currentProjectName.clear();
    m_window.m_projectDirty = false;
    if (m_window.m_saveProjectAction)
    {
        m_window.m_saveProjectAction->setEnabled(false);
    }
    if (m_window.m_saveProjectAsAction)
    {
        m_window.m_saveProjectAsAction->setEnabled(false);
    }
    updateWindowTitle();
}

void ProjectWindowController::setCurrentProject(const QString& projectFilePath, const QString& projectName)
{
    m_window.m_currentProjectFilePath = normalizeProjectFilePath(projectFilePath);
    m_window.m_currentProjectRootPath = QFileInfo(m_window.m_currentProjectFilePath).absolutePath();
    m_window.m_currentProjectName = dawg::project::sanitizeProjectName(projectName);
    m_window.m_projectDirty = false;
    if (m_window.m_saveProjectAction)
    {
        m_window.m_saveProjectAction->setEnabled(false);
    }
    if (m_window.m_saveProjectAsAction)
    {
        m_window.m_saveProjectAsAction->setEnabled(true);
    }

    QSettings settings;
    settings.setValue(QString::fromLatin1(kLastProjectPathSettingsKey), m_window.m_currentProjectFilePath);
    updateWindowTitle();
}

void ProjectWindowController::setProjectDirty(const bool dirty)
{
    if (!hasOpenProject() || m_window.m_projectDirty == dirty)
    {
        return;
    }

    m_window.m_projectDirty = dirty;
    if (m_window.m_saveProjectAction)
    {
        m_window.m_saveProjectAction->setEnabled(m_window.m_projectDirty);
    }
    updateWindowTitle();
}

void ProjectWindowController::updateWindowTitle()
{
    QStringList parts{QStringLiteral("dawg")};
    if (!m_window.m_currentProjectName.isEmpty())
    {
        parts.push_back(m_window.m_currentProjectName + (m_window.m_projectDirty ? QStringLiteral("*") : QString{}));
    }
    if (!m_window.m_clipName.isEmpty())
    {
        parts.push_back(m_window.m_clipName);
    }
    m_window.setWindowTitle(parts.join(QStringLiteral(" - ")));

    if (m_window.m_detachedVideoWindow)
    {
        QStringList detachedParts{QStringLiteral("Detached Video"), QStringLiteral("dawg")};
        if (!m_window.m_currentProjectName.isEmpty())
        {
            detachedParts.push_back(m_window.m_currentProjectName);
        }
        m_window.m_detachedVideoWindow->setWindowTitle(detachedParts.join(QStringLiteral(" - ")));
        m_window.m_detachedVideoWindow->setWindowIcon(m_window.windowIcon());
    }
}

void ProjectWindowController::restoreLastProjectOnStartup()
{
    QSettings settings;
    const auto lastProjectPath = settings.value(QString::fromLatin1(kLastProjectPathSettingsKey)).toString();
    if (lastProjectPath.isEmpty())
    {
        updateWindowTitle();
        return;
    }

    if (!QFileInfo::exists(lastProjectPath) || !loadProjectFile(lastProjectPath))
    {
        settings.remove(QString::fromLatin1(kLastProjectPathSettingsKey));
        removeRecentProjectPath(lastProjectPath);
        clearCurrentProject();
    }
}

QStringList ProjectWindowController::recentProjectPaths() const
{
    QSettings settings;
    const auto storedPaths = settings.value(QString::fromLatin1(kRecentProjectPathsSettingsKey)).toStringList();
    QStringList normalizedPaths;
    normalizedPaths.reserve(static_cast<qsizetype>(
        std::min(storedPaths.size(), static_cast<qsizetype>(kMaxRecentProjectCount))));
    for (const auto& storedPath : storedPaths)
    {
        const auto normalizedPath = normalizeProjectFilePath(storedPath);
        if (normalizedPath.isEmpty())
        {
            continue;
        }

        const auto duplicateIt = std::find_if(
            normalizedPaths.cbegin(),
            normalizedPaths.cend(),
            [&normalizedPath](const QString& existingPath)
            {
                return projectPathMatches(existingPath, normalizedPath);
            });
        if (duplicateIt != normalizedPaths.cend())
        {
            continue;
        }

        normalizedPaths.push_back(normalizedPath);
        if (normalizedPaths.size() >= kMaxRecentProjectCount)
        {
            break;
        }
    }
    return normalizedPaths;
}

void ProjectWindowController::storeRecentProjectPaths(const QStringList& projectPaths)
{
    QSettings settings;
    if (projectPaths.isEmpty())
    {
        settings.remove(QString::fromLatin1(kRecentProjectPathsSettingsKey));
        return;
    }

    settings.setValue(QString::fromLatin1(kRecentProjectPathsSettingsKey), projectPaths);
}

void ProjectWindowController::addRecentProjectPath(const QString& projectFilePath)
{
    const auto normalizedPath = normalizeProjectFilePath(projectFilePath);
    if (normalizedPath.isEmpty())
    {
        return;
    }

    auto updatedPaths = recentProjectPaths();
    updatedPaths.erase(
        std::remove_if(
            updatedPaths.begin(),
            updatedPaths.end(),
            [&normalizedPath](const QString& existingPath)
            {
                return projectPathMatches(existingPath, normalizedPath);
            }),
        updatedPaths.end());
    updatedPaths.push_front(normalizedPath);
    while (updatedPaths.size() > kMaxRecentProjectCount)
    {
        updatedPaths.removeLast();
    }

    storeRecentProjectPaths(updatedPaths);
    rebuildRecentProjectsMenu();
}

void ProjectWindowController::removeRecentProjectPath(const QString& projectFilePath)
{
    const auto normalizedPath = normalizeProjectFilePath(projectFilePath);
    if (normalizedPath.isEmpty())
    {
        return;
    }

    auto updatedPaths = recentProjectPaths();
    const auto originalSize = updatedPaths.size();
    updatedPaths.erase(
        std::remove_if(
            updatedPaths.begin(),
            updatedPaths.end(),
            [&normalizedPath](const QString& existingPath)
            {
                return projectPathMatches(existingPath, normalizedPath);
            }),
        updatedPaths.end());
    if (updatedPaths.size() == originalSize)
    {
        return;
    }

    storeRecentProjectPaths(updatedPaths);
    rebuildRecentProjectsMenu();
}

void ProjectWindowController::rebuildRecentProjectsMenu()
{
    if (!m_window.m_openRecentMenu)
    {
        return;
    }

    m_window.m_openRecentMenu->clear();
    m_window.m_openRecentMenu->setToolTipsVisible(true);

    const auto storedPaths = recentProjectPaths();
    QStringList existingPaths;
    existingPaths.reserve(storedPaths.size());
    for (const auto& storedPath : storedPaths)
    {
        if (QFileInfo::exists(storedPath))
        {
            existingPaths.push_back(storedPath);
        }
    }

    if (existingPaths != storedPaths)
    {
        storeRecentProjectPaths(existingPaths);
    }

    if (existingPaths.isEmpty())
    {
        auto* placeholderAction = m_window.m_openRecentMenu->addAction(QStringLiteral("No Recent Projects"));
        placeholderAction->setEnabled(false);
        return;
    }

    for (const auto& projectPath : existingPaths)
    {
        const QFileInfo projectInfo(projectPath);
        const auto displayName = projectInfo.completeBaseName().isEmpty()
            ? projectInfo.fileName()
            : projectInfo.completeBaseName();
        const auto parentPath = QDir::toNativeSeparators(projectInfo.absolutePath());
        auto* recentAction =
            m_window.m_openRecentMenu->addAction(QStringLiteral("%1  -  %2").arg(displayName, parentPath));
        recentAction->setToolTip(QDir::toNativeSeparators(projectPath));
        recentAction->setStatusTip(QDir::toNativeSeparators(projectPath));
        QObject::connect(recentAction, &QAction::triggered, &m_window, [this, projectPath]()
        {
            static_cast<void>(openProjectFileWithPrompt(projectPath, QStringLiteral("open another project")));
        });
    }
}

bool ProjectWindowController::promptToSaveIfDirty(const QString& actionLabel)
{
    if (!hasOpenProject() || !m_window.m_projectDirty)
    {
        return true;
    }

    QMessageBox messageBox(&m_window);
    messageBox.setIcon(QMessageBox::Warning);
    messageBox.setWindowTitle(QStringLiteral("Unsaved Changes"));
    const auto projectLabel =
        m_window.m_currentProjectName.isEmpty() ? m_window.m_currentProjectFilePath : m_window.m_currentProjectName;
    messageBox.setText(QStringLiteral("Do you want to save the changes made to \"%1\"?").arg(projectLabel));
    messageBox.setInformativeText(
        QStringLiteral("Your changes will be lost if you don't save them before you %1.").arg(actionLabel));
    messageBox.setStandardButtons(QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    messageBox.setDefaultButton(QMessageBox::Save);
    const auto result = messageBox.exec();

    if (result == QMessageBox::Save)
    {
        return saveProjectToCurrentPath();
    }

    return result == QMessageBox::Discard;
}

bool ProjectWindowController::saveProjectToCurrentPath()
{
    if (!hasOpenProject())
    {
        return false;
    }

    return saveProjectToPath(m_window.m_currentProjectFilePath, m_window.m_currentProjectName);
}

bool ProjectWindowController::saveProjectToPath(const QString& projectFilePath, const QString& projectName)
{
    if (projectFilePath.isEmpty())
    {
        return false;
    }

    auto controllerState = m_window.m_controller->snapshotProjectState();
    const auto projectRootPath = QFileInfo(projectFilePath).absolutePath();
    const QDir projectRoot(projectRootPath);

    const auto makeRelativePath = [&projectRoot](const QString& path, QString* errorMessage) -> std::optional<QString>
    {
        if (path.isEmpty())
        {
            return QString{};
        }

        const auto cleanedPath = QDir::cleanPath(QDir::fromNativeSeparators(path));
        const auto absolutePath = QFileInfo(cleanedPath).isAbsolute()
            ? QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(cleanedPath).absoluteFilePath()))
            : QDir::cleanPath(QDir::fromNativeSeparators(projectRoot.absoluteFilePath(cleanedPath)));
        const auto relativePath = QDir::cleanPath(QDir::fromNativeSeparators(projectRoot.relativeFilePath(absolutePath)));
        const auto escapesProjectRoot =
            relativePath == QStringLiteral("..")
            || relativePath.startsWith(QStringLiteral("../"))
            || QDir::isAbsolutePath(relativePath);
        if (escapesProjectRoot)
        {
            if (errorMessage)
            {
                *errorMessage = QStringLiteral("Project media is outside the project folder:\n%1").arg(absolutePath);
            }
            return std::nullopt;
        }

        return relativePath;
    };

    QString errorMessage;
    if (!controllerState.videoPath.isEmpty())
    {
        const auto relativeVideoPath = makeRelativePath(controllerState.videoPath, &errorMessage);
        if (!relativeVideoPath.has_value())
        {
            QMessageBox::warning(&m_window, QStringLiteral("Save Project"), errorMessage);
            return false;
        }
        controllerState.videoPath = *relativeVideoPath;
    }

    for (auto& assetPath : controllerState.audioPoolAssetPaths)
    {
        const auto relativeAssetPath = makeRelativePath(assetPath, &errorMessage);
        if (!relativeAssetPath.has_value())
        {
            QMessageBox::warning(&m_window, QStringLiteral("Save Project"), errorMessage);
            return false;
        }
        assetPath = *relativeAssetPath;
    }

    for (auto& track : controllerState.trackerState.tracks)
    {
        if (!track.attachedAudio.has_value())
        {
            continue;
        }
        const auto relativeAssetPath = makeRelativePath(track.attachedAudio->assetPath, &errorMessage);
        if (!relativeAssetPath.has_value())
        {
            QMessageBox::warning(&m_window, QStringLiteral("Save Project"), errorMessage);
            return false;
        }
        track.attachedAudio->assetPath = *relativeAssetPath;
    }

    const dawg::project::Document document{
        .name = dawg::project::sanitizeProjectName(projectName),
        .controller = controllerState,
        .ui = m_window.snapshotProjectUiState()
    };

    if (!dawg::project::saveDocument(projectFilePath, document, &errorMessage))
    {
        QMessageBox::warning(&m_window, QStringLiteral("Save Project"), errorMessage);
        return false;
    }

    setCurrentProject(projectFilePath, document.name);
    addRecentProjectPath(projectFilePath);
    setProjectDirty(false);
    m_window.showStatus(QStringLiteral("Saved project %1.").arg(document.name));
    return true;
}

bool ProjectWindowController::saveProjectAsNewCopy()
{
    if (!hasOpenProject())
    {
        return false;
    }

    bool ok = false;
    const auto projectName = QInputDialog::getText(
        &m_window,
        QStringLiteral("Save Project As"),
        QStringLiteral("Project name:"),
        QLineEdit::Normal,
        m_window.m_currentProjectName,
        &ok);
    if (!ok)
    {
        return false;
    }

    const auto sanitizedProjectName = dawg::project::sanitizeProjectName(projectName);
    const auto parentDirectory = m_window.chooseExistingDirectory(QStringLiteral("Choose Destination Folder"));
    if (parentDirectory.isEmpty())
    {
        return false;
    }

    const auto targetRootPath = QDir(parentDirectory).filePath(sanitizedProjectName);
    QDir targetRoot(targetRootPath);
    if (targetRoot.exists() && !targetRoot.entryList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty())
    {
        QMessageBox::warning(
            &m_window,
            QStringLiteral("Save Project As"),
            QStringLiteral("Destination folder already exists and is not empty:\n%1").arg(targetRootPath));
        return false;
    }

    if (!QDir().mkpath(targetRoot.filePath(QStringLiteral("audio")))
        || !QDir().mkpath(targetRoot.filePath(QStringLiteral("video")))
        || !QDir().mkpath(targetRoot.filePath(QStringLiteral("settings"))))
    {
        QMessageBox::warning(
            &m_window,
            QStringLiteral("Save Project As"),
            QStringLiteral("Failed to create destination project folders."));
        return false;
    }

    auto controllerState = m_window.m_controller->snapshotProjectState();
    QHash<QString, QString> copiedAudioPaths;
    QString errorMessage;

    const auto copyIntoTargetProject = [&targetRootPath](
                                           const QString& sourcePath,
                                           const QString& subdirectory,
                                           QString* error) -> std::optional<QString>
    {
        if (sourcePath.isEmpty())
        {
            return QString{};
        }

        const QFileInfo sourceInfo(sourcePath);
        if (!sourceInfo.exists() || !sourceInfo.isFile())
        {
            if (error)
            {
                *error = QStringLiteral("Missing project media: %1").arg(sourcePath);
            }
            return std::nullopt;
        }

        const auto targetDirectoryPath = QDir(targetRootPath).filePath(subdirectory);
        const auto targetPath = uniqueTargetFilePath(targetDirectoryPath, sourceInfo.absoluteFilePath());
        if (!QFile::copy(sourceInfo.absoluteFilePath(), targetPath))
        {
            if (error)
            {
                *error = QStringLiteral("Failed to copy %1 to the new project.").arg(sourceInfo.fileName());
            }
            return std::nullopt;
        }
        return QDir::cleanPath(targetPath);
    };

    if (!controllerState.videoPath.isEmpty())
    {
        const auto copiedVideoPath =
            copyIntoTargetProject(controllerState.videoPath, QStringLiteral("video"), &errorMessage);
        if (!copiedVideoPath.has_value())
        {
            QMessageBox::warning(&m_window, QStringLiteral("Save Project As"), errorMessage);
            return false;
        }
        controllerState.videoPath = *copiedVideoPath;
    }

    for (auto& assetPath : controllerState.audioPoolAssetPaths)
    {
        const auto existingIt = copiedAudioPaths.constFind(assetPath);
        if (existingIt != copiedAudioPaths.cend())
        {
            assetPath = existingIt.value();
            continue;
        }

        const auto copiedAudioPath =
            copyIntoTargetProject(assetPath, QStringLiteral("audio"), &errorMessage);
        if (!copiedAudioPath.has_value())
        {
            QMessageBox::warning(&m_window, QStringLiteral("Save Project As"), errorMessage);
            return false;
        }
        copiedAudioPaths.insert(assetPath, *copiedAudioPath);
        assetPath = *copiedAudioPath;
    }

    for (auto& track : controllerState.trackerState.tracks)
    {
        if (!track.attachedAudio.has_value())
        {
            continue;
        }

        const auto existingIt = copiedAudioPaths.constFind(track.attachedAudio->assetPath);
        if (existingIt != copiedAudioPaths.cend())
        {
            track.attachedAudio->assetPath = existingIt.value();
            continue;
        }

        const auto copiedAudioPath =
            copyIntoTargetProject(track.attachedAudio->assetPath, QStringLiteral("audio"), &errorMessage);
        if (!copiedAudioPath.has_value())
        {
            QMessageBox::warning(&m_window, QStringLiteral("Save Project As"), errorMessage);
            return false;
        }
        copiedAudioPaths.insert(track.attachedAudio->assetPath, *copiedAudioPath);
        track.attachedAudio->assetPath = *copiedAudioPath;
    }

    const auto targetProjectFilePath =
        targetRoot.filePath(dawg::project::projectFileNameForName(sanitizedProjectName));
    const auto currentUiState = m_window.snapshotProjectUiState();
    const auto relativeRoot = QDir(targetRootPath);
    auto relativeControllerState = controllerState;
    relativeControllerState.videoPath = controllerState.videoPath.isEmpty()
        ? QString{}
        : QDir::cleanPath(relativeRoot.relativeFilePath(controllerState.videoPath));
    for (auto& assetPath : relativeControllerState.audioPoolAssetPaths)
    {
        assetPath = QDir::cleanPath(relativeRoot.relativeFilePath(assetPath));
    }
    for (auto& track : relativeControllerState.trackerState.tracks)
    {
        if (track.attachedAudio.has_value())
        {
            track.attachedAudio->assetPath =
                QDir::cleanPath(relativeRoot.relativeFilePath(track.attachedAudio->assetPath));
        }
    }

    const dawg::project::Document document{
        .name = sanitizedProjectName,
        .controller = relativeControllerState,
        .ui = currentUiState
    };
    if (!dawg::project::saveDocument(targetProjectFilePath, document, &errorMessage))
    {
        QMessageBox::warning(&m_window, QStringLiteral("Save Project As"), errorMessage);
        return false;
    }

    m_window.m_projectStateChangeInProgress = true;
    const auto restored = m_window.m_controller->restoreProjectState(controllerState, &errorMessage);
    m_window.m_projectStateChangeInProgress = false;
    if (!restored)
    {
        QMessageBox::warning(&m_window, QStringLiteral("Save Project As"), errorMessage);
        return false;
    }

    setCurrentProject(targetProjectFilePath, sanitizedProjectName);
    addRecentProjectPath(targetProjectFilePath);
    setProjectDirty(false);
    m_window.showStatus(QStringLiteral("Saved project copy as %1.").arg(sanitizedProjectName));
    return true;
}

bool ProjectWindowController::loadProjectFile(const QString& projectFilePath)
{
    QString errorMessage;
    const auto document = dawg::project::loadDocument(projectFilePath, &errorMessage);
    if (!document.has_value())
    {
        QMessageBox::warning(&m_window, QStringLiteral("Open Project"), errorMessage);
        return false;
    }

    auto absoluteControllerState = document->controller;
    const auto projectRootPath = QFileInfo(projectFilePath).absolutePath();
    const QDir projectRoot(projectRootPath);

    const auto makeAbsolutePath = [&projectRoot](const QString& relativePath) -> QString
    {
        if (relativePath.isEmpty())
        {
            return {};
        }
        return QDir::cleanPath(projectRoot.absoluteFilePath(relativePath));
    };

    absoluteControllerState.videoPath = makeAbsolutePath(absoluteControllerState.videoPath);
    for (auto& assetPath : absoluteControllerState.audioPoolAssetPaths)
    {
        assetPath = makeAbsolutePath(assetPath);
    }
    for (auto& track : absoluteControllerState.trackerState.tracks)
    {
        if (track.attachedAudio.has_value())
        {
            track.attachedAudio->assetPath = makeAbsolutePath(track.attachedAudio->assetPath);
        }
    }

    QString recoveryMessage;
    const auto recoveredMediaFromFolders =
        recoverProjectMediaFromFolders(&absoluteControllerState, projectRootPath, &recoveryMessage);

    m_window.m_projectStateChangeInProgress = true;
    const auto restored = m_window.m_controller->restoreProjectState(absoluteControllerState, &errorMessage);
    if (restored)
    {
        m_window.applyProjectUiState(document->ui);
        setCurrentProject(projectFilePath, document->name);
        addRecentProjectPath(projectFilePath);
        setProjectDirty(false);
        if (recoveredMediaFromFolders)
        {
            setProjectDirty(true);
        }
    }
    m_window.m_projectStateChangeInProgress = false;

    if (!restored)
    {
        QMessageBox::warning(&m_window, QStringLiteral("Open Project"), errorMessage);
        return false;
    }

    m_window.showStatus(
        recoveredMediaFromFolders
            ? QStringLiteral("Opened project %1. %2").arg(document->name, recoveryMessage)
            : QStringLiteral("Opened project %1.").arg(document->name));
    return true;
}

bool ProjectWindowController::openProjectFileWithPrompt(const QString& projectFilePath, const QString& actionLabel)
{
    const auto normalizedProjectPath = normalizeProjectFilePath(projectFilePath);
    if (normalizedProjectPath.isEmpty())
    {
        return false;
    }

    if (!promptToSaveIfDirty(actionLabel))
    {
        return false;
    }

    if (hasOpenProject() && !m_window.m_projectDirty && !saveProjectToCurrentPath())
    {
        return false;
    }

    if (!QFileInfo::exists(normalizedProjectPath))
    {
        removeRecentProjectPath(normalizedProjectPath);
        QMessageBox::warning(
            &m_window,
            QStringLiteral("Open Project"),
            QStringLiteral("Project file not found:\n%1").arg(QDir::toNativeSeparators(normalizedProjectPath)));
        return false;
    }

    return loadProjectFile(normalizedProjectPath);
}

bool ProjectWindowController::createProjectAt(const QString& projectName, const QString& parentDirectory)
{
    const auto sanitizedProjectName = dawg::project::sanitizeProjectName(projectName);
    const auto projectRootPath = QDir(parentDirectory).filePath(sanitizedProjectName);
    QDir projectRoot(projectRootPath);
    if (projectRoot.exists() && !projectRoot.entryList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty())
    {
        QMessageBox::warning(
            &m_window,
            QStringLiteral("New Project"),
            QStringLiteral("Project folder already exists and is not empty:\n%1").arg(projectRootPath));
        return false;
    }

    if (!QDir().mkpath(projectRoot.filePath(QStringLiteral("audio")))
        || !QDir().mkpath(projectRoot.filePath(QStringLiteral("video")))
        || !QDir().mkpath(projectRoot.filePath(QStringLiteral("settings"))))
    {
        QMessageBox::warning(
            &m_window,
            QStringLiteral("New Project"),
            QStringLiteral("Failed to create project folders in:\n%1").arg(projectRootPath));
        return false;
    }

    const auto projectFilePath =
        projectRoot.filePath(dawg::project::projectFileNameForName(sanitizedProjectName));
    m_window.m_projectStateChangeInProgress = true;
    m_window.m_controller->resetProjectState();
    m_window.m_projectStateChangeInProgress = false;
    setCurrentProject(projectFilePath, sanitizedProjectName);
    if (!saveProjectToPath(projectFilePath, sanitizedProjectName))
    {
        clearCurrentProject();
        return false;
    }
    return true;
}

void ProjectWindowController::newProject()
{
    if (!promptToSaveIfDirty(QStringLiteral("create a new project")))
    {
        return;
    }

    if (hasOpenProject() && !m_window.m_projectDirty && !saveProjectToCurrentPath())
    {
        return;
    }

    bool ok = false;
    const auto projectName = QInputDialog::getText(
        &m_window,
        QStringLiteral("New Project"),
        QStringLiteral("Project name:"),
        QLineEdit::Normal,
        QStringLiteral("Untitled Project"),
        &ok);
    if (!ok)
    {
        return;
    }

    const auto parentDirectory = m_window.chooseExistingDirectory(QStringLiteral("Choose Project Location"));
    if (parentDirectory.isEmpty())
    {
        return;
    }

    static_cast<void>(createProjectAt(projectName, parentDirectory));
}

void ProjectWindowController::openProject()
{
    if (!promptToSaveIfDirty(QStringLiteral("open another project")))
    {
        return;
    }

    if (hasOpenProject() && !m_window.m_projectDirty && !saveProjectToCurrentPath())
    {
        return;
    }

    const auto projectFilePath = m_window.chooseOpenFileName(
        QStringLiteral("Open Project"),
        {},
        QStringLiteral("DAWG Projects (*%1)").arg(QString::fromLatin1(dawg::project::kProjectFileSuffix)));
    if (projectFilePath.isEmpty())
    {
        return;
    }

    static_cast<void>(openProjectFileWithPrompt(projectFilePath, QStringLiteral("open another project")));
}

void ProjectWindowController::saveProject()
{
    static_cast<void>(saveProjectToCurrentPath());
}

void ProjectWindowController::saveProjectAs()
{
    static_cast<void>(saveProjectAsNewCopy());
}
