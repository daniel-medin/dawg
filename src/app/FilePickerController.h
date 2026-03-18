#pragma once

#include <optional>

#include <QAbstractListModel>
#include <QEventLoop>
#include <QFileInfo>
#include <QStandardPaths>
#include <QVariantList>

class FilePickerEntryModel final : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Roles
    {
        NameRole = Qt::UserRole + 1,
        PathRole,
        DirectoryRole,
        SizeTextRole,
    };
    Q_ENUM(Roles)

    explicit FilePickerEntryModel(QObject* parent = nullptr);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    void setEntries(QFileInfoList entries);
    [[nodiscard]] const QFileInfo* entryAt(int index) const;

private:
    QFileInfoList m_entries;
};

class FilePickerController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool visible READ visible NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QString currentPath READ currentPath WRITE setCurrentPath NOTIFY changed)
    Q_PROPERTY(QString selectedPath READ selectedPath NOTIFY changed)
    Q_PROPERTY(QString fileName READ fileName WRITE setFileName NOTIFY changed)
    Q_PROPERTY(bool directoryMode READ directoryMode NOTIFY changed)
    Q_PROPERTY(bool saveMode READ saveMode NOTIFY changed)
    Q_PROPERTY(QString actionText READ actionText NOTIFY changed)
    Q_PROPERTY(QObject* entries READ entries CONSTANT)
    Q_PROPERTY(QVariantList sidebarLocations READ sidebarLocations NOTIFY changed)

public:
    explicit FilePickerController(QObject* parent = nullptr);

    [[nodiscard]] bool visible() const;
    [[nodiscard]] QString title() const;
    [[nodiscard]] QString currentPath() const;
    void setCurrentPath(const QString& currentPath);
    [[nodiscard]] QString selectedPath() const;
    [[nodiscard]] QString fileName() const;
    void setFileName(const QString& fileName);
    [[nodiscard]] bool directoryMode() const;
    [[nodiscard]] bool saveMode() const;
    [[nodiscard]] QString actionText() const;
    [[nodiscard]] QObject* entries() const;
    [[nodiscard]] QVariantList sidebarLocations() const;

    [[nodiscard]] QString execOpenFile(
        const QString& title,
        const QString& directory,
        const QString& filter);
    [[nodiscard]] QString execOpenDirectory(const QString& title, const QString& directory);
    [[nodiscard]] QString execSaveFile(
        const QString& title,
        const QString& directory,
        const QString& suggestedName,
        const QString& filter);

    Q_INVOKABLE void goUp();
    Q_INVOKABLE void activateEntry(int index);
    Q_INVOKABLE void selectEntry(int index);
    Q_INVOKABLE void openSidebarLocation(const QString& path);
    Q_INVOKABLE void acceptSelection();
    Q_INVOKABLE void cancel();

signals:
    void changed();

private:
    void showRequest(
        const QString& title,
        const QString& directory,
        bool directoryMode,
        bool saveMode,
        const QString& suggestedName,
        const QString& filter);
    void refreshEntries();
    void finish(const QString& selection);
    void refreshSidebarLocations();
    void clearState();
    [[nodiscard]] QString normalizedExistingDirectory(const QString& path) const;
    [[nodiscard]] QString sizeText(const QFileInfo& info) const;
    [[nodiscard]] QStringList parseFilterPatterns(const QString& filter) const;
    [[nodiscard]] QString defaultSuffix(const QStringList& patterns) const;
    [[nodiscard]] QString buildSavePath() const;

    bool m_visible = false;
    QString m_title;
    QString m_currentPath;
    QString m_selectedPath;
    QString m_fileName;
    bool m_directoryMode = false;
    bool m_saveMode = false;
    QString m_defaultSuffix;
    QStringList m_filterPatterns;
    FilePickerEntryModel m_entries;
    QVariantList m_sidebarLocations;
    QEventLoop* m_eventLoop = nullptr;
    QString m_result;
};
