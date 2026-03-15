#pragma once

#include <functional>

#include <QQuickWidget>

class QuickClipGainWidget final : public QQuickWidget
{
    Q_OBJECT

public:
    explicit QuickClipGainWidget(QWidget* parent = nullptr);

    void setGainDb(float gainDb);
    void setMeterLevel(float level);
    void setGainChangedHandler(std::function<void(float)> handler);

private slots:
    void handleGainDragged(double gainDb);

private:
    void attachRootSignals();
    void syncGainProperty();
    void syncMeterProperty();
    void syncProperties();

    float m_gainDb = 0.0F;
    float m_meterLevel = 0.0F;
    std::function<void(float)> m_gainChangedHandler;
    bool m_rootSignalsConnected = false;
};
