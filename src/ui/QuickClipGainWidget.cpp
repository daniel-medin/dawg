#include "ui/QuickClipGainWidget.h"

#include <algorithm>
#include <cmath>

#include <QQuickItem>

namespace
{
QUrl clipGainStripUrl()
{
    return QUrl(QStringLiteral("qrc:/qml/ClipGainStrip.qml"));
}

constexpr float kGainStepDb = 0.1F;
constexpr float kMeterStep = 1.0F / 96.0F;

float quantizeStep(const float value, const float step)
{
    return std::round(value / step) * step;
}
}

QuickClipGainWidget::QuickClipGainWidget(QWidget* parent)
    : QQuickWidget(parent)
{
    setResizeMode(QQuickWidget::SizeRootObjectToView);
    setClearColor(Qt::transparent);
    setMinimumWidth(76);
    setMaximumWidth(76);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    setAttribute(Qt::WA_AlwaysStackOnTop, false);
    setSource(clipGainStripUrl());
    attachRootSignals();
    syncProperties();
}

void QuickClipGainWidget::setGainDb(const float gainDb)
{
    const auto quantizedGainDb = quantizeStep(gainDb, kGainStepDb);
    if (std::abs(m_gainDb - quantizedGainDb) <= 0.001F)
    {
        return;
    }

    m_gainDb = quantizedGainDb;
    syncGainProperty();
}

void QuickClipGainWidget::setMeterLevel(const float level)
{
    const auto clampedLevel = std::clamp(level, 0.0F, 1.0F);
    const auto quantizedLevel = std::clamp(quantizeStep(clampedLevel, kMeterStep), 0.0F, 1.0F);
    if (std::abs(m_meterLevel - quantizedLevel) <= 0.0001F)
    {
        return;
    }

    m_meterLevel = quantizedLevel;
    syncMeterProperty();
}

void QuickClipGainWidget::setGainChangedHandler(std::function<void(float)> handler)
{
    m_gainChangedHandler = std::move(handler);
}

void QuickClipGainWidget::handleGainDragged(const double gainDb)
{
    m_gainDb = quantizeStep(static_cast<float>(gainDb), kGainStepDb);
    if (m_gainChangedHandler)
    {
        m_gainChangedHandler(m_gainDb);
    }
}

void QuickClipGainWidget::attachRootSignals()
{
    if (m_rootSignalsConnected)
    {
        return;
    }

    auto* root = rootObject();
    if (!root)
    {
        return;
    }

    connect(root, SIGNAL(gainDragged(double)), this, SLOT(handleGainDragged(double)));
    m_rootSignalsConnected = true;
}

void QuickClipGainWidget::syncProperties()
{
    syncGainProperty();
    syncMeterProperty();
}

void QuickClipGainWidget::syncGainProperty()
{
    if (auto* root = rootObject())
    {
        root->setProperty("gainDb", m_gainDb);
    }
}

void QuickClipGainWidget::syncMeterProperty()
{
    if (auto* root = rootObject())
    {
        root->setProperty("meterLevel", m_meterLevel);
    }
}
