#include "app/AudioPoolQuickController.h"

#include <array>
#include <cmath>
#include <cstdint>

#include <QColor>

#include "app/MainWindow.h"
#include "app/PlayerController.h"

namespace
{
QString formatAudioPoolDuration(const int durationMs)
{
    if (durationMs <= 0)
    {
        return QStringLiteral("--");
    }

    const auto totalSeconds = std::max(0, static_cast<int>(std::lround(durationMs / 1000.0)));
    const auto hours = totalSeconds / 3600;
    const auto minutes = (totalSeconds / 60) % 60;
    const auto seconds = totalSeconds % 60;
    if (hours > 0)
    {
        return QStringLiteral("%1:%2:%3")
            .arg(hours)
            .arg(minutes, 2, 10, QLatin1Char('0'))
            .arg(seconds, 2, 10, QLatin1Char('0'));
    }

    return QStringLiteral("%1:%2")
        .arg(totalSeconds / 60)
        .arg(seconds, 2, 10, QLatin1Char('0'));
}

QString formatAudioPoolSize(const std::int64_t fileSizeBytes)
{
    if (fileSizeBytes < 0)
    {
        return QStringLiteral("--");
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

QColor statusColorFor(const AudioPoolItem& item)
{
    if (item.isPlaying)
    {
        return QColor(QStringLiteral("#63c987"));
    }

    return item.connectedNodeCount > 0
        ? QColor(QStringLiteral("#d88932"))
        : QColor(QStringLiteral("#cf5f5f"));
}
}

AudioPoolQuickController::AudioPoolQuickController(MainWindow& window, QObject* parent)
    : QAbstractListModel(parent)
    , m_window(window)
{
}

int AudioPoolQuickController::count() const
{
    return static_cast<int>(m_items.size());
}

int AudioPoolQuickController::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_items.size());
}

QVariant AudioPoolQuickController::data(const QModelIndex& index, const int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(m_items.size()))
    {
        return {};
    }

    const auto& item = m_items[static_cast<std::size_t>(index.row())];
    switch (role)
    {
    case KeyRole:
        return item.key;
    case AssetPathRole:
        return item.assetPath;
    case DisplayNameRole:
        return item.displayName;
    case ConnectedNodeCountRole:
        return item.connectedNodeCount;
    case IsPlayingRole:
        return item.isPlaying;
    case ConnectionSummaryRole:
        return item.connectionSummary;
    case DurationTextRole:
        return formatAudioPoolDuration(item.durationMs);
    case SizeTextRole:
        return formatAudioPoolSize(item.fileSizeBytes);
    case StatusColorRole:
        return statusColorFor(item);
    case ConnectedRole:
        return item.connectedNodeCount > 0;
    default:
        return {};
    }
}

QHash<int, QByteArray> AudioPoolQuickController::roleNames() const
{
    return {
        {KeyRole, "key"},
        {AssetPathRole, "assetPath"},
        {DisplayNameRole, "displayName"},
        {ConnectedNodeCountRole, "connectedNodeCount"},
        {IsPlayingRole, "isPlaying"},
        {ConnectionSummaryRole, "connectionSummary"},
        {DurationTextRole, "durationText"},
        {SizeTextRole, "sizeText"},
        {StatusColorRole, "statusColor"},
        {ConnectedRole, "connected"},
    };
}

void AudioPoolQuickController::replaceItems(std::vector<AudioPoolItem> items)
{
    const auto previousCount = static_cast<int>(m_items.size());
    beginResetModel();
    m_items = std::move(items);
    endResetModel();
    if (previousCount != static_cast<int>(m_items.size()))
    {
        emit countChanged();
    }
}

void AudioPoolQuickController::syncVideoAudioState(
    const bool hasVideoAudio,
    const QString& videoAudioLabel,
    const QString& videoAudioTooltip,
    const bool videoAudioMuted,
    const bool fastPlaybackEnabled)
{
    const bool changed =
        m_hasVideoAudio != hasVideoAudio
        || m_videoAudioLabel != videoAudioLabel
        || m_videoAudioTooltip != videoAudioTooltip
        || m_videoAudioMuted != videoAudioMuted
        || m_fastPlaybackEnabled != fastPlaybackEnabled;

    m_hasVideoAudio = hasVideoAudio;
    m_videoAudioLabel = videoAudioLabel;
    m_videoAudioTooltip = videoAudioTooltip;
    m_videoAudioMuted = videoAudioMuted;
    m_fastPlaybackEnabled = fastPlaybackEnabled;

    if (changed)
    {
        emit videoAudioStateChanged();
    }
}

bool AudioPoolQuickController::showLength() const
{
    return m_showLength;
}

void AudioPoolQuickController::setShowLength(const bool showLength)
{
    if (m_showLength == showLength)
    {
        return;
    }

    m_showLength = showLength;
    emit showLengthChanged();
    markProjectDirtyForDisplayChange();
}

bool AudioPoolQuickController::showSize() const
{
    return m_showSize;
}

void AudioPoolQuickController::setShowSize(const bool showSize)
{
    if (m_showSize == showSize)
    {
        return;
    }

    m_showSize = showSize;
    emit showSizeChanged();
    markProjectDirtyForDisplayChange();
}

bool AudioPoolQuickController::hasVideoAudio() const
{
    return m_hasVideoAudio;
}

QString AudioPoolQuickController::videoAudioLabel() const
{
    return m_videoAudioLabel;
}

QString AudioPoolQuickController::videoAudioTooltip() const
{
    return m_videoAudioTooltip;
}

bool AudioPoolQuickController::videoAudioMuted() const
{
    return m_videoAudioMuted;
}

bool AudioPoolQuickController::fastPlaybackEnabled() const
{
    return m_fastPlaybackEnabled;
}

void AudioPoolQuickController::importAudio()
{
    m_window.importAudioToPool();
}

void AudioPoolQuickController::closePanel()
{
    m_window.updateAudioPoolVisibility(false);
}

void AudioPoolQuickController::itemActivated(const int index)
{
    if (const auto* item = itemAt(index); item && !item->trackId.isNull())
    {
        m_window.m_controller->selectTrackAndJumpToStart(item->trackId);
    }
}

void AudioPoolQuickController::itemDoubleActivated(const int index)
{
    if (const auto* item = itemAt(index); item)
    {
        m_window.m_controller->createTrackWithAudioAtCurrentFrame(item->assetPath);
    }
}

void AudioPoolQuickController::startPreview(const int index)
{
    if (const auto* item = itemAt(index); item)
    {
        m_window.m_controller->startAudioPoolPreview(item->assetPath);
    }
}

void AudioPoolQuickController::stopPreview()
{
    m_window.m_controller->stopAudioPoolPreview();
}

void AudioPoolQuickController::deleteAudio(const int index)
{
    if (const auto* item = itemAt(index); item)
    {
        m_window.m_controller->removeAudioFromPool(item->assetPath);
    }
}

void AudioPoolQuickController::deleteAudioAndNodes(const int index)
{
    if (const auto* item = itemAt(index); item)
    {
        m_window.m_controller->removeAudioAndConnectedNodesFromPool(item->assetPath);
    }
}

void AudioPoolQuickController::toggleVideoAudioMuted()
{
    m_window.m_controller->toggleEmbeddedVideoAudioMuted();
}

void AudioPoolQuickController::toggleFastPlayback()
{
    m_window.m_controller->setFastPlaybackEnabled(!m_window.m_controller->isFastPlaybackEnabled());
}

const AudioPoolItem* AudioPoolQuickController::itemAt(const int index) const
{
    if (index < 0 || index >= static_cast<int>(m_items.size()))
    {
        return nullptr;
    }

    return &m_items[static_cast<std::size_t>(index)];
}

void AudioPoolQuickController::markProjectDirtyForDisplayChange()
{
    m_window.m_audioPoolShowLength = m_showLength;
    m_window.m_audioPoolShowSize = m_showSize;
    if (!m_window.m_projectStateChangeInProgress && m_window.hasOpenProject())
    {
        m_window.setProjectDirty(true);
    }
}
