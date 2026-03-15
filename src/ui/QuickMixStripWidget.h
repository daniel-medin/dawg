#pragma once

#include <functional>

#include <QColor>
#include <QQuickWidget>
#include <QString>

class QuickMixStripWidget final : public QQuickWidget
{
    Q_OBJECT

public:
    explicit QuickMixStripWidget(QWidget* parent = nullptr);

    [[nodiscard]] static QString graphicsApiText();
    [[nodiscard]] static QString loadStatusText();

    [[nodiscard]] bool isReady() const;
    [[nodiscard]] QString errorString() const;

    void setMasterStrip(bool masterStrip);
    void setTitle(const QString& title);
    void setDetail(const QString& detail);
    void setFooter(const QString& footer);
    void setAccentColor(const QColor& color);
    void setGainDb(float gainDb);
    void setMuted(bool muted);
    void setSoloEnabled(bool enabled);
    void setSoloed(bool soloed);
    void setMeterLevel(float level);
    void setGainChangedHandler(std::function<void(float)> handler);
    void setMutedChangedHandler(std::function<void(bool)> handler);
    void setSoloChangedHandler(std::function<void(bool)> handler);

private slots:
    void handleGainDragged(double gainDb);
    void handleMuteToggled(bool muted);
    void handleSoloToggled(bool soloed);

private:
    static void setGraphicsApiText(const QString& text);
    static void setLoadStatusText(const QString& text);
    void updateGraphicsApiDiagnostics();
    void attachRootSignals();
    void handleStatusChanged(QQuickWidget::Status status);
    void syncProperties();

    QString m_title;
    QString m_detail;
    QString m_footer;
    QColor m_accentColor = QColor(QStringLiteral("#f0f4f8"));
    bool m_masterStrip = true;
    float m_gainDb = 0.0F;
    bool m_muted = false;
    bool m_soloEnabled = false;
    bool m_soloed = false;
    float m_meterLevel = 0.0F;
    std::function<void(float)> m_gainChangedHandler;
    std::function<void(bool)> m_mutedChangedHandler;
    std::function<void(bool)> m_soloChangedHandler;
    QString m_errorString;
    bool m_rootSignalsConnected = false;
};
