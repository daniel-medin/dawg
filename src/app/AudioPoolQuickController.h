#pragma once

#include <vector>

#include <QAbstractListModel>
#include <QString>

#include "app/AudioPoolService.h"

class MainWindow;

class AudioPoolQuickController final : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(bool showLength READ showLength WRITE setShowLength NOTIFY showLengthChanged)
    Q_PROPERTY(bool showSize READ showSize WRITE setShowSize NOTIFY showSizeChanged)
    Q_PROPERTY(bool hasVideoAudio READ hasVideoAudio NOTIFY videoAudioStateChanged)
    Q_PROPERTY(QString videoAudioLabel READ videoAudioLabel NOTIFY videoAudioStateChanged)
    Q_PROPERTY(QString videoAudioDetail READ videoAudioDetail NOTIFY videoAudioStateChanged)
    Q_PROPERTY(QString videoAudioTooltip READ videoAudioTooltip NOTIFY videoAudioStateChanged)
    Q_PROPERTY(bool videoAudioMuted READ videoAudioMuted NOTIFY videoAudioStateChanged)
    Q_PROPERTY(bool fastPlaybackEnabled READ fastPlaybackEnabled NOTIFY videoAudioStateChanged)

public:
    enum Roles
    {
        KeyRole = Qt::UserRole + 1,
        AssetPathRole,
        DisplayNameRole,
        ConnectedNodeCountRole,
        IsPlayingRole,
        ConnectionSummaryRole,
        DurationTextRole,
        SizeTextRole,
        StatusColorRole,
        ConnectedRole,
    };
    Q_ENUM(Roles)

    explicit AudioPoolQuickController(MainWindow& window, QObject* parent = nullptr);

    [[nodiscard]] int count() const;
    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    void replaceItems(std::vector<AudioPoolItem> items);
    void updatePlaybackState(const std::vector<AudioPoolItem>& items);
    void syncVideoAudioState(
        bool hasVideoAudio,
        const QString& videoAudioLabel,
        const QString& videoAudioDetail,
        const QString& videoAudioTooltip,
        bool videoAudioMuted,
        bool fastPlaybackEnabled);

    [[nodiscard]] bool showLength() const;
    void setShowLength(bool showLength);

    [[nodiscard]] bool showSize() const;
    void setShowSize(bool showSize);

    [[nodiscard]] bool hasVideoAudio() const;
    [[nodiscard]] QString videoAudioLabel() const;
    [[nodiscard]] QString videoAudioDetail() const;
    [[nodiscard]] QString videoAudioTooltip() const;
    [[nodiscard]] bool videoAudioMuted() const;
    [[nodiscard]] bool fastPlaybackEnabled() const;

    Q_INVOKABLE void importAudio();
    Q_INVOKABLE void closePanel();
    Q_INVOKABLE void itemActivated(int index);
    Q_INVOKABLE void itemDoubleActivated(int index);
    Q_INVOKABLE void startPreview(int index);
    Q_INVOKABLE void stopPreview();
    Q_INVOKABLE void deleteAudio(int index);
    Q_INVOKABLE void deleteAudioAndNodes(int index);
    Q_INVOKABLE void toggleVideoAudioMuted();
    Q_INVOKABLE void toggleFastPlayback();

signals:
    void countChanged();
    void showLengthChanged();
    void showSizeChanged();
    void videoAudioStateChanged();

private:
    [[nodiscard]] const AudioPoolItem* itemAt(int index) const;
    void setPreviewItemKey(const QString& key);
    void markProjectDirtyForDisplayChange();

    MainWindow& m_window;
    std::vector<AudioPoolItem> m_items;
    QString m_previewItemKey;
    bool m_showLength = true;
    bool m_showSize = true;
    bool m_hasVideoAudio = false;
    QString m_videoAudioLabel;
    QString m_videoAudioDetail;
    QString m_videoAudioTooltip;
    bool m_videoAudioMuted = true;
    bool m_fastPlaybackEnabled = false;
};
