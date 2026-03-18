#include "app/FilePickerController.h"

#include <algorithm>
#include <array>
#include <cstdint>

#include <QDir>
#include <QFileInfoList>
#include <QVariantMap>

namespace
{
QString displaySize(const std::int64_t fileSizeBytes)
{
    if (fileSizeBytes < 0)
    {
        return {};
    }

    constexpr std::array<const char*, 4> units{"B", "KB", "MB", "GB"};
    double size = static_cast<double>(fileSizeBytes);
    std::size_t unitIndex = 0;
    while (size >= 1024.0 && unitIndex + 1 < units.size())
    {
        size /= 1024.0;
        ++unitIndex;
    }

    const auto decimals = unitIndex == 0 ? 0 : 1;
    return QStringLiteral("%1 %2")
        .arg(size, 0, 'f', decimals)
        .arg(QString::fromLatin1(units[unitIndex]));
}
}

FilePickerEntryModel::FilePickerEntryModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int FilePickerEntryModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_entries.size();
}

QVariant FilePickerEntryModel::data(const QModelIndex& index, const int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size())
    {
        return {};
    }

    const auto& entry = m_entries.at(index.row());
    switch (role)
    {
    case NameRole:
        return entry.fileName();
    case PathRole:
        return QDir::cleanPath(entry.absoluteFilePath());
    case DirectoryRole:
        return entry.isDir();
    case SizeTextRole:
        return entry.isDir() ? QStringLiteral("Folder") : displaySize(entry.size());
    default:
        return {};
    }
}

QHash<int, QByteArray> FilePickerEntryModel::roleNames() const
{
    return {
        {NameRole, "name"},
        {PathRole, "path"},
        {DirectoryRole, "directory"},
        {SizeTextRole, "sizeText"},
    };
}

void FilePickerEntryModel::setEntries(QFileInfoList entries)
{
    beginResetModel();
    m_entries = std::move(entries);
    endResetModel();
}

const QFileInfo* FilePickerEntryModel::entryAt(const int index) const
{
    if (index < 0 || index >= m_entries.size())
    {
        return nullptr;
    }

    return &m_entries.at(index);
}

FilePickerController::FilePickerController(QObject* parent)
    : QObject(parent)
    , m_entries(this)
{
    refreshSidebarLocations();
}

bool FilePickerController::visible() const
{
    return m_visible;
}

QString FilePickerController::title() const
{
    return m_title;
}

QString FilePickerController::currentPath() const
{
    return m_currentPath;
}

void FilePickerController::setCurrentPath(const QString& currentPath)
{
    const auto normalized = normalizedExistingDirectory(currentPath);
    if (normalized.isEmpty() || normalized == m_currentPath)
    {
        return;
    }

    m_currentPath = normalized;
    m_selectedPath.clear();
    refreshEntries();
    emit changed();
}

QString FilePickerController::selectedPath() const
{
    if (m_saveMode)
    {
        return buildSavePath();
    }

    return m_selectedPath;
}

QString FilePickerController::fileName() const
{
    return m_fileName;
}

void FilePickerController::setFileName(const QString& fileName)
{
    if (m_fileName == fileName)
    {
        return;
    }

    m_fileName = fileName;
    emit changed();
}

bool FilePickerController::directoryMode() const
{
    return m_directoryMode;
}

bool FilePickerController::saveMode() const
{
    return m_saveMode;
}

QString FilePickerController::actionText() const
{
    if (m_saveMode)
    {
        return QStringLiteral("Save");
    }

    return m_directoryMode ? QStringLiteral("Select Folder") : QStringLiteral("Open");
}

QObject* FilePickerController::entries() const
{
    return const_cast<FilePickerEntryModel*>(&m_entries);
}

QVariantList FilePickerController::sidebarLocations() const
{
    return m_sidebarLocations;
}

QString FilePickerController::execOpenFile(
    const QString& title,
    const QString& directory,
    const QString& filter)
{
    showRequest(title, directory, false, false, {}, filter);
    QEventLoop eventLoop;
    m_eventLoop = &eventLoop;
    eventLoop.exec();
    m_eventLoop = nullptr;
    return m_result;
}

QString FilePickerController::execOpenDirectory(const QString& title, const QString& directory)
{
    showRequest(title, directory, true, false, {}, {});
    QEventLoop eventLoop;
    m_eventLoop = &eventLoop;
    eventLoop.exec();
    m_eventLoop = nullptr;
    return m_result;
}

QString FilePickerController::execSaveFile(
    const QString& title,
    const QString& directory,
    const QString& suggestedName,
    const QString& filter)
{
    showRequest(title, directory, false, true, suggestedName, filter);
    QEventLoop eventLoop;
    m_eventLoop = &eventLoop;
    eventLoop.exec();
    m_eventLoop = nullptr;
    return m_result;
}

void FilePickerController::goUp()
{
    const auto parentPath = QFileInfo(m_currentPath).dir().absolutePath();
    if (parentPath != m_currentPath)
    {
        setCurrentPath(parentPath);
    }
}

void FilePickerController::activateEntry(const int index)
{
    const auto* entry = m_entries.entryAt(index);
    if (!entry)
    {
        return;
    }

    if (entry->isDir())
    {
        setCurrentPath(entry->absoluteFilePath());
        if (m_directoryMode)
        {
            m_selectedPath = QDir::cleanPath(entry->absoluteFilePath());
            emit changed();
        }
        return;
    }

    if (m_directoryMode)
    {
        return;
    }

    if (m_saveMode)
    {
        m_fileName = entry->fileName();
        emit changed();
        return;
    }

    m_selectedPath = QDir::cleanPath(entry->absoluteFilePath());
    emit changed();
    acceptSelection();
}

void FilePickerController::selectEntry(const int index)
{
    const auto* entry = m_entries.entryAt(index);
    if (!entry)
    {
        return;
    }

    if (entry->isDir())
    {
        if (m_directoryMode)
        {
            m_selectedPath = QDir::cleanPath(entry->absoluteFilePath());
            emit changed();
        }
        return;
    }

    if (m_saveMode)
    {
        m_fileName = entry->fileName();
        emit changed();
        return;
    }

    if (!m_directoryMode)
    {
        m_selectedPath = QDir::cleanPath(entry->absoluteFilePath());
        emit changed();
    }
}

void FilePickerController::openSidebarLocation(const QString& path)
{
    setCurrentPath(path);
}

void FilePickerController::acceptSelection()
{
    if (m_saveMode)
    {
        const auto selection = buildSavePath();
        if (!selection.isEmpty())
        {
            finish(selection);
        }
        return;
    }

    if (m_directoryMode)
    {
        const auto selection = m_selectedPath.isEmpty() ? m_currentPath : m_selectedPath;
        finish(selection);
        return;
    }

    if (!m_selectedPath.isEmpty())
    {
        finish(m_selectedPath);
    }
}

void FilePickerController::cancel()
{
    finish({});
}

void FilePickerController::showRequest(
    const QString& title,
    const QString& directory,
    const bool directoryMode,
    const bool saveMode,
    const QString& suggestedName,
    const QString& filter)
{
    m_title = title;
    m_directoryMode = directoryMode;
    m_saveMode = saveMode;
    m_filterPatterns = directoryMode ? QStringList{} : parseFilterPatterns(filter);
    m_defaultSuffix = defaultSuffix(m_filterPatterns);
    m_currentPath = normalizedExistingDirectory(directory);
    if (m_currentPath.isEmpty())
    {
        m_currentPath = normalizedExistingDirectory(QStandardPaths::writableLocation(QStandardPaths::DesktopLocation));
    }
    if (m_currentPath.isEmpty())
    {
        m_currentPath = QDir::homePath();
    }
    m_selectedPath = directoryMode ? m_currentPath : QString{};
    m_fileName = suggestedName;
    refreshEntries();
    m_result.clear();
    m_visible = true;
    emit changed();
}

void FilePickerController::refreshEntries()
{
    QDir directory(m_currentPath);
    QFileInfoList entries;
    if (directory.exists())
    {
        entries = directory.entryInfoList(
            QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot,
            QDir::DirsFirst | QDir::IgnoreCase | QDir::Name);
    }

    QFileInfoList filteredEntries;
    filteredEntries.reserve(entries.size());
    for (const auto& entry : entries)
    {
        if (entry.isDir())
        {
            filteredEntries.push_back(entry);
            continue;
        }

        if (m_directoryMode)
        {
            continue;
        }

        if (m_filterPatterns.isEmpty() || QDir::match(m_filterPatterns, entry.fileName()))
        {
            filteredEntries.push_back(entry);
        }
    }

    m_entries.setEntries(std::move(filteredEntries));
}

void FilePickerController::finish(const QString& selection)
{
    m_result = selection;
    m_visible = false;
    emit changed();
    if (m_eventLoop)
    {
        m_eventLoop->quit();
    }
}

void FilePickerController::refreshSidebarLocations()
{
    m_sidebarLocations.clear();
    auto addLocation = [this](const QString& label, const QString& path)
    {
        if (path.isEmpty())
        {
            return;
        }

        QVariantMap descriptor;
        descriptor.insert(QStringLiteral("label"), label);
        descriptor.insert(QStringLiteral("path"), QDir::cleanPath(path));
        for (const auto& existing : m_sidebarLocations)
        {
            if (existing.toMap().value(QStringLiteral("path")).toString() == descriptor.value(QStringLiteral("path")).toString())
            {
                return;
            }
        }
        m_sidebarLocations.push_back(descriptor);
    };

    addLocation(QStringLiteral("Desktop"), QStandardPaths::writableLocation(QStandardPaths::DesktopLocation));
    addLocation(QStringLiteral("Home"), QStandardPaths::writableLocation(QStandardPaths::HomeLocation));
    addLocation(QStringLiteral("Documents"), QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
    addLocation(QStringLiteral("Movies"), QStandardPaths::writableLocation(QStandardPaths::MoviesLocation));
    addLocation(QStringLiteral("Music"), QStandardPaths::writableLocation(QStandardPaths::MusicLocation));
}

void FilePickerController::clearState()
{
    m_title.clear();
    m_currentPath.clear();
    m_selectedPath.clear();
    m_fileName.clear();
    m_filterPatterns.clear();
    m_defaultSuffix.clear();
    m_saveMode = false;
    m_entries.setEntries({});
}

QString FilePickerController::normalizedExistingDirectory(const QString& path) const
{
    if (path.isEmpty())
    {
        return {};
    }

    QFileInfo info(path);
    if (info.exists() && info.isDir())
    {
        return QDir::cleanPath(info.absoluteFilePath());
    }

    if (info.exists() && info.isFile())
    {
        return QDir::cleanPath(info.absolutePath());
    }

    QDir dir(path);
    if (dir.exists())
    {
        return QDir::cleanPath(dir.absolutePath());
    }

    return {};
}

QString FilePickerController::sizeText(const QFileInfo& info) const
{
    return info.isDir() ? QStringLiteral("Folder") : displaySize(info.size());
}

QStringList FilePickerController::parseFilterPatterns(const QString& filter) const
{
    QStringList patterns;
    const auto sections = filter.split(QStringLiteral(";;"), Qt::SkipEmptyParts);
    if (sections.isEmpty())
    {
        return patterns;
    }

    const auto firstSection = sections.constFirst();
    const auto start = firstSection.indexOf(QLatin1Char('('));
    const auto end = firstSection.indexOf(QLatin1Char(')'), start + 1);
    if (start < 0 || end <= start)
    {
        return patterns;
    }

    const auto inside = firstSection.mid(start + 1, end - start - 1);
    for (const auto& pattern : inside.split(QLatin1Char(' '), Qt::SkipEmptyParts))
    {
        patterns.push_back(pattern.trimmed());
    }
    return patterns;
}

QString FilePickerController::defaultSuffix(const QStringList& patterns) const
{
    if (patterns.isEmpty())
    {
        return {};
    }

    const auto firstPattern = patterns.constFirst().trimmed();
    if (!firstPattern.startsWith(QStringLiteral("*.")))
    {
        return {};
    }

    const auto suffix = firstPattern.mid(2);
    return suffix.contains(QLatin1Char('*')) ? QString{} : suffix;
}

QString FilePickerController::buildSavePath() const
{
    if (!m_saveMode || m_currentPath.isEmpty())
    {
        return {};
    }

    auto fileName = m_fileName.trimmed();
    if (fileName.isEmpty())
    {
        return {};
    }

    if (!m_defaultSuffix.isEmpty() && QFileInfo(fileName).suffix().isEmpty())
    {
        fileName += QStringLiteral(".") + m_defaultSuffix;
    }

    return QDir::cleanPath(QDir(m_currentPath).filePath(fileName));
}
