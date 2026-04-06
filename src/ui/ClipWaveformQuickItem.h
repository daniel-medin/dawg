#pragma once

#include <optional>
#include <vector>

#include <QImage>
#include <QQuickPaintedItem>

#include "ui/ClipEditorTypes.h"

class ClipWaveformQuickItem : public QQuickPaintedItem
{
    Q_OBJECT
    Q_PROPERTY(bool clipRangeHandlesVisible READ clipRangeHandlesVisible WRITE setClipRangeHandlesVisible NOTIFY clipRangeHandlesVisibleChanged)
    Q_PROPERTY(bool scrollVisible READ scrollVisible NOTIFY scrollMetricsChanged)
    Q_PROPERTY(int scrollValue READ scrollValue NOTIFY scrollMetricsChanged)
    Q_PROPERTY(int scrollMaximum READ scrollMaximum NOTIFY scrollMetricsChanged)
    Q_PROPERTY(int scrollPageStep READ scrollPageStep NOTIFY scrollMetricsChanged)

public:
    explicit ClipWaveformQuickItem(QQuickItem* parent = nullptr);

    void setState(const std::optional<ClipEditorState>& state);
    [[nodiscard]] bool clipRangeHandlesVisible() const;
    void setClipRangeHandlesVisible(bool visible);
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
    void scrollMetricsChanged();

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    enum class DragMode
    {
        None,
        Start,
        End,
        Playhead
    };

    void loadWaveform(const QString& filePath);
    [[nodiscard]] QRectF selectionRect(const QRectF& bounds) const;
    [[nodiscard]] double clipStartX(const QRectF& bounds) const;
    [[nodiscard]] double clipEndX(const QRectF& bounds) const;
    [[nodiscard]] double playheadX(const QRectF& bounds) const;
    [[nodiscard]] double xForMs(const QRectF& bounds, int timeMs) const;
    [[nodiscard]] int msForX(const QRectF& bounds, double x) const;
    [[nodiscard]] double interpolatedPeakAtMs(double timeMs) const;
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

    std::optional<ClipEditorState> m_state;
    bool m_clipRangeHandlesVisible = true;
    QString m_loadedAssetPath;
    std::vector<float> m_peaks;
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
