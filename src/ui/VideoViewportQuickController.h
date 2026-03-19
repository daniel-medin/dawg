#pragma once

#include <vector>

#include <QImage>
#include <QObject>
#include <QSize>
#include <QString>
#include <QVariantList>

#include "core/tracking/TrackTypes.h"
#include "core/video/VideoFrame.h"

class VideoViewportQuickController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool hasFrame READ hasFrame NOTIFY hasFrameChanged)
    Q_PROPERTY(QString frameSource READ frameSource NOTIFY frameSourceChanged)
    Q_PROPERTY(QVariantList overlays READ overlays NOTIFY overlaysChanged)
    Q_PROPERTY(bool showAllLabels READ showAllLabels NOTIFY showAllLabelsChanged)
    Q_PROPERTY(double displayScaleFactor READ displayScaleFactor NOTIFY displayScaleFactorChanged)
    Q_PROPERTY(int sourceWidth READ sourceWidth NOTIFY sourceGeometryChanged)
    Q_PROPERTY(int sourceHeight READ sourceHeight NOTIFY sourceGeometryChanged)
    Q_PROPERTY(bool nativePresentationActive READ nativePresentationActive NOTIFY nativePresentationActiveChanged)

public:
    explicit VideoViewportQuickController(QObject* parent = nullptr);

    void setPresentedFrame(const QImage& frame, const VideoFrame& videoFrame, const QSize& sourceSize);
    void setFrame(const QImage& frame);
    void setVideoFrame(const VideoFrame& videoFrame);
    void setSourceFrameSize(const QSize& sourceSize);
    void setOverlays(const std::vector<TrackOverlay>& overlays);
    void setShowAllLabels(bool enabled);
    void setDisplayScaleFactor(double scaleFactor);
    void setNativePresentationEnabled(bool enabled);

    [[nodiscard]] bool hasFrame() const;
    [[nodiscard]] QString frameSource() const;
    [[nodiscard]] QVariantList overlays() const;
    [[nodiscard]] bool showAllLabels() const;
    [[nodiscard]] double displayScaleFactor() const;
    [[nodiscard]] int sourceWidth() const;
    [[nodiscard]] int sourceHeight() const;
    [[nodiscard]] bool nativePresentationActive() const;
    [[nodiscard]] const QImage& currentFrame() const;
    [[nodiscard]] const VideoFrame& currentVideoFrame() const;
    [[nodiscard]] int frameRevision() const;
    [[nodiscard]] const std::vector<TrackOverlay>& overlayData() const;

    Q_INVOKABLE QVariantMap frameRect(qreal viewWidth, qreal viewHeight) const;
    Q_INVOKABLE QVariantMap widgetToImagePoint(
        qreal widgetX,
        qreal widgetY,
        qreal viewWidth,
        qreal viewHeight) const;
    Q_INVOKABLE QString trackIdAt(qreal widgetX, qreal widgetY, qreal viewWidth, qreal viewHeight) const;
    Q_INVOKABLE QVariantList tracksInRect(
        qreal rectX,
        qreal rectY,
        qreal rectWidth,
        qreal rectHeight,
        qreal viewWidth,
        qreal viewHeight) const;
    Q_INVOKABLE bool overlayHasAttachedAudio(const QString& trackId) const;
    Q_INVOKABLE bool overlayIsSelected(const QString& trackId) const;

signals:
    void hasFrameChanged();
    void frameSourceChanged();
    void overlaysChanged();
    void showAllLabelsChanged();
    void displayScaleFactorChanged();
    void sourceGeometryChanged();
    void nativePresentationActiveChanged();

private:
    [[nodiscard]] QSize effectiveSourceSize() const;
    [[nodiscard]] QRectF scaledFrameRect(const QSizeF& viewSize) const;
    void updateFrameState(bool hasFrame);
    void updateNativePresentationState();
    void bumpFrameRevision();
    [[nodiscard]] const TrackOverlay* overlayById(const QString& trackId) const;

    QImage m_frame;
    VideoFrame m_videoFrame;
    QSize m_sourceFrameSize;
    std::vector<TrackOverlay> m_overlayData;
    bool m_hasFrame = false;
    bool m_showAllLabels = false;
    double m_displayScaleFactor = 1.0;
    int m_frameRevision = 0;
    QString m_frameSource;
    bool m_nativePresentationEnabled = false;
    bool m_nativePresentationActive = false;
};
