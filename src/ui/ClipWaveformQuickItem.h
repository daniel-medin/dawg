#pragma once

#include <array>
#include <optional>
#include <vector>

#include <QImage>
#include <QQuickPaintedItem>
#include <QVariantMap>

#include "ui/AudioClipPreviewTypes.h"

class ClipWaveformQuickItem : public QQuickPaintedItem
{
    Q_OBJECT
    Q_PROPERTY(bool clipRangeHandlesVisible READ clipRangeHandlesVisible WRITE setClipRangeHandlesVisible NOTIFY clipRangeHandlesVisibleChanged)
    Q_PROPERTY(bool clipRangeOnly READ clipRangeOnly WRITE setClipRangeOnly NOTIFY clipRangeOnlyChanged)
    Q_PROPERTY(bool playheadVisible READ playheadVisible WRITE setPlayheadVisible NOTIFY playheadVisibleChanged)
    Q_PROPERTY(qreal contentMargin READ contentMargin WRITE setContentMargin NOTIFY contentMarginChanged)
    Q_PROPERTY(qreal verticalZoom READ verticalZoom WRITE setVerticalZoom NOTIFY verticalZoomChanged)
    Q_PROPERTY(bool scrollVisible READ scrollVisible NOTIFY scrollMetricsChanged)
    Q_PROPERTY(int scrollValue READ scrollValue NOTIFY scrollMetricsChanged)
    Q_PROPERTY(int scrollMaximum READ scrollMaximum NOTIFY scrollMetricsChanged)
    Q_PROPERTY(int scrollPageStep READ scrollPageStep NOTIFY scrollMetricsChanged)
    Q_PROPERTY(QVariantMap waveformState READ waveformState WRITE setWaveformState NOTIFY waveformStateChanged)
    Q_PROPERTY(QVariantMap previewWaveformState READ previewWaveformState WRITE setPreviewWaveformState NOTIFY previewWaveformStateChanged)

public:
    explicit ClipWaveformQuickItem(QQuickItem* parent = nullptr);

    void setState(const std::optional<AudioClipPreviewState>& state);
    [[nodiscard]] QVariantMap waveformState() const;
    void setWaveformState(const QVariantMap& state);
    [[nodiscard]] QVariantMap previewWaveformState() const;
    void setPreviewWaveformState(const QVariantMap& state);
    [[nodiscard]] bool clipRangeHandlesVisible() const;
    void setClipRangeHandlesVisible(bool visible);
    [[nodiscard]] bool clipRangeOnly() const;
    void setClipRangeOnly(bool clipRangeOnly);
    [[nodiscard]] bool playheadVisible() const;
    void setPlayheadVisible(bool visible);
    [[nodiscard]] qreal contentMargin() const;
    void setContentMargin(qreal margin);
    [[nodiscard]] qreal verticalZoom() const;
    void setVerticalZoom(qreal zoom);
    [[nodiscard]] bool scrollVisible() const;
    [[nodiscard]] int scrollValue() const;
    [[nodiscard]] int scrollMaximum() const;
    [[nodiscard]] int scrollPageStep() const;

    Q_INVOKABLE void setViewStartMs(int viewStartMs);

    void paint(QPainter* painter) override;

signals:
    void clipRangeChanged(int clipStartMs, int clipEndMs);
    void playheadChanged(int playheadMs);
    void clipRangeHandlesVisibleChanged();
    void clipRangeOnlyChanged();
    void playheadVisibleChanged();
    void contentMarginChanged();
    void verticalZoomChanged();
    void scrollMetricsChanged();
    void waveformStateChanged();
    void previewWaveformStateChanged();

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    struct WaveformChannelPeak
    {
        float positive = 0.0F;
        float negative = 0.0F;
    };

    struct WaveformPeak
    {
        std::array<WaveformChannelPeak, 2> channels;
    };

    enum class DragMode
    {
        None,
        Start,
        End,
        Playhead
    };

    void loadWaveform(const QString& filePath);
    [[nodiscard]] QRectF waveformBounds() const;
    [[nodiscard]] QRectF selectionRect(const QRectF& bounds) const;
    [[nodiscard]] double clipStartX(const QRectF& bounds) const;
    [[nodiscard]] double clipEndX(const QRectF& bounds) const;
    [[nodiscard]] double playheadX(const QRectF& bounds) const;
    [[nodiscard]] double xForMs(const QRectF& bounds, int timeMs) const;
    [[nodiscard]] int msForX(const QRectF& bounds, double x) const;
    [[nodiscard]] WaveformPeak peakRangeBetweenMs(double startMs, double endMs) const;
    [[nodiscard]] int displayRangeStartMs() const;
    [[nodiscard]] int displayRangeEndMs() const;
    [[nodiscard]] int displayRangeDurationMs() const;
    void paintHandle(QPainter& painter, const QRectF& bounds, double x, const QColor& color) const;
    void paintPlayhead(QPainter& painter, const QRectF& bounds, const QColor& color) const;
    void applyDragAt(const QPointF& position);
    void applyPlayheadAt(const QPointF& position);
    [[nodiscard]] double visibleDurationMs() const;
    [[nodiscard]] double maxHorizontalZoom() const;
    void clampViewWindow();
    void updateScrollMetrics();
    void invalidateWaveformCache();
    void ensureWaveformCache();
    void rebuildWaveformCache();

    std::optional<AudioClipPreviewState> m_state;
    QVariantMap m_waveformState;
    QVariantMap m_previewWaveformState;
    bool m_clipRangeHandlesVisible = true;
    bool m_clipRangeOnly = false;
    bool m_previewClipRangeActive = false;
    bool m_playheadVisible = true;
    qreal m_contentMargin = 10.0;
    QString m_loadedAssetPath;
    std::vector<WaveformPeak> m_peaks;
    int m_waveformChannelCount = 0;
    int m_previewClipStartMs = 0;
    int m_previewClipEndMs = 0;
    DragMode m_dragMode = DragMode::None;
    double m_horizontalZoom = 1.0;
    double m_verticalZoom = 1.0;
    double m_viewStartMs = 0.0;
    int m_scrollValue = 0;
    int m_scrollMaximum = 0;
    int m_scrollPageStep = 0;
    bool m_scrollVisible = false;
    QImage m_waveformCache;
    bool m_waveformCacheDirty = true;
};
