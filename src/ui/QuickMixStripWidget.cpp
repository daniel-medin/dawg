#include "ui/QuickMixStripWidget.h"

#include <algorithm>

#include <QDebug>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QStringList>

namespace
{
QUrl mixStripUrl()
{
    return QUrl(QStringLiteral("qrc:/qml/MixStrip.qml"));
}

QString& graphicsApiStorage()
{
    static QString text = QStringLiteral("Unknown");
    return text;
}

QString& loadStatusStorage()
{
    static QString text = QStringLiteral("Not initialized");
    return text;
}

QString graphicsApiToString(const QSGRendererInterface::GraphicsApi api)
{
    switch (api)
    {
    case QSGRendererInterface::Unknown:
        return QStringLiteral("Unknown");
    case QSGRendererInterface::Software:
        return QStringLiteral("Software");
    case QSGRendererInterface::OpenVG:
        return QStringLiteral("OpenVG");
    case QSGRendererInterface::OpenGL:
        return QStringLiteral("OpenGL");
    case QSGRendererInterface::Direct3D11:
        return QStringLiteral("D3D11");
    case QSGRendererInterface::Vulkan:
        return QStringLiteral("Vulkan");
    case QSGRendererInterface::Metal:
        return QStringLiteral("Metal");
    case QSGRendererInterface::Null:
        return QStringLiteral("Null");
    case QSGRendererInterface::Direct3D12:
        return QStringLiteral("D3D12");
    }

    return QStringLiteral("Unknown");
}
}

QuickMixStripWidget::QuickMixStripWidget(QWidget* parent)
    : QQuickWidget(parent)
{
    setResizeMode(QQuickWidget::SizeRootObjectToView);
    setClearColor(Qt::transparent);
    setMinimumWidth(88);
    setMaximumWidth(108);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    setAttribute(Qt::WA_AlwaysStackOnTop, false);
    connect(this, &QQuickWidget::statusChanged, this, &QuickMixStripWidget::handleStatusChanged);
    if (auto* window = quickWindow())
    {
        connect(window, &QQuickWindow::sceneGraphInitialized, this, [this]()
        {
            updateGraphicsApiDiagnostics();
        });
    }
    setSource(mixStripUrl());
    updateGraphicsApiDiagnostics();
    handleStatusChanged(status());
}

QString QuickMixStripWidget::graphicsApiText()
{
    return graphicsApiStorage();
}

QString QuickMixStripWidget::loadStatusText()
{
    return loadStatusStorage();
}

bool QuickMixStripWidget::isReady() const
{
    return status() == QQuickWidget::Ready && rootObject() && m_errorString.isEmpty();
}

QString QuickMixStripWidget::errorString() const
{
    return m_errorString;
}

void QuickMixStripWidget::setGraphicsApiText(const QString& text)
{
    graphicsApiStorage() = text;
}

void QuickMixStripWidget::setLoadStatusText(const QString& text)
{
    loadStatusStorage() = text;
}

void QuickMixStripWidget::setMasterStrip(const bool masterStrip)
{
    if (m_masterStrip == masterStrip)
    {
        return;
    }

    m_masterStrip = masterStrip;
    syncProperties();
}

void QuickMixStripWidget::setTitle(const QString& title)
{
    if (m_title == title)
    {
        return;
    }

    m_title = title;
    syncProperties();
}

void QuickMixStripWidget::setDetail(const QString& detail)
{
    if (m_detail == detail)
    {
        return;
    }

    m_detail = detail;
    syncProperties();
}

void QuickMixStripWidget::setFooter(const QString& footer)
{
    if (m_footer == footer)
    {
        return;
    }

    m_footer = footer;
    syncProperties();
}

void QuickMixStripWidget::setAccentColor(const QColor& color)
{
    if (m_accentColor == color)
    {
        return;
    }

    m_accentColor = color;
    syncProperties();
}

void QuickMixStripWidget::setGainDb(const float gainDb)
{
    if (std::abs(m_gainDb - gainDb) <= 0.001F)
    {
        return;
    }

    m_gainDb = gainDb;
    syncProperties();
}

void QuickMixStripWidget::setMuted(const bool muted)
{
    if (m_muted == muted)
    {
        return;
    }

    m_muted = muted;
    syncProperties();
}

void QuickMixStripWidget::setSoloEnabled(const bool enabled)
{
    if (m_soloEnabled == enabled)
    {
        return;
    }

    m_soloEnabled = enabled;
    syncProperties();
}

void QuickMixStripWidget::setSoloed(const bool soloed)
{
    if (m_soloed == soloed)
    {
        return;
    }

    m_soloed = soloed;
    syncProperties();
}

void QuickMixStripWidget::setMeterLevel(const float level)
{
    const auto clampedLevel = std::clamp(level, 0.0F, 1.0F);
    if (std::abs(m_meterLevel - clampedLevel) <= 0.0001F)
    {
        return;
    }

    m_meterLevel = clampedLevel;
    syncProperties();
}

void QuickMixStripWidget::setGainChangedHandler(std::function<void(float)> handler)
{
    m_gainChangedHandler = std::move(handler);
}

void QuickMixStripWidget::setMutedChangedHandler(std::function<void(bool)> handler)
{
    m_mutedChangedHandler = std::move(handler);
}

void QuickMixStripWidget::setSoloChangedHandler(std::function<void(bool)> handler)
{
    m_soloChangedHandler = std::move(handler);
}

void QuickMixStripWidget::handleGainDragged(const double gainDb)
{
    m_gainDb = static_cast<float>(gainDb);
    if (m_gainChangedHandler)
    {
        m_gainChangedHandler(m_gainDb);
    }
}

void QuickMixStripWidget::handleMuteToggled(const bool muted)
{
    m_muted = muted;
    if (m_mutedChangedHandler)
    {
        m_mutedChangedHandler(muted);
    }
}

void QuickMixStripWidget::handleSoloToggled(const bool soloed)
{
    m_soloed = soloed;
    if (m_soloChangedHandler)
    {
        m_soloChangedHandler(soloed);
    }
}

void QuickMixStripWidget::attachRootSignals()
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
    connect(root, SIGNAL(muteToggled(bool)), this, SLOT(handleMuteToggled(bool)));
    connect(root, SIGNAL(soloToggled(bool)), this, SLOT(handleSoloToggled(bool)));
    m_rootSignalsConnected = true;
}

void QuickMixStripWidget::updateGraphicsApiDiagnostics()
{
    auto* window = quickWindow();
    if (!window || !window->rendererInterface())
    {
        setGraphicsApiText(QStringLiteral("Unavailable"));
        return;
    }

    const auto apiText = graphicsApiToString(window->rendererInterface()->graphicsApi());
    if (graphicsApiText() != apiText)
    {
        setGraphicsApiText(apiText);
        qInfo().noquote() << "Qt Quick graphics API:" << apiText;
    }
}

void QuickMixStripWidget::handleStatusChanged(const QQuickWidget::Status status)
{
    if (status == QQuickWidget::Ready)
    {
        m_errorString.clear();
        setLoadStatusText(QStringLiteral("Ready"));
        attachRootSignals();
        updateGraphicsApiDiagnostics();
        syncProperties();
        return;
    }

    if (status == QQuickWidget::Loading)
    {
        setLoadStatusText(QStringLiteral("Loading"));
        return;
    }

    if (status == QQuickWidget::Null)
    {
        setLoadStatusText(QStringLiteral("Null"));
        return;
    }

    if (status != QQuickWidget::Error)
    {
        return;
    }

    QStringList messages;
    const auto quickErrors = errors();
    messages.reserve(quickErrors.size());
    for (const auto& error : quickErrors)
    {
        messages.push_back(error.toString());
    }

    m_errorString = messages.join(QLatin1Char('\n'));
    if (m_errorString.isEmpty())
    {
        m_errorString = QStringLiteral("Unknown QML load error.");
    }

    setLoadStatusText(QStringLiteral("Error"));
    qWarning().noquote() << "QuickMixStripWidget failed to load qrc:/qml/MixStrip.qml\n" << m_errorString;
}

void QuickMixStripWidget::syncProperties()
{
    if (auto* root = rootObject())
    {
        root->setProperty("masterStrip", m_masterStrip);
        root->setProperty("titleText", m_title);
        root->setProperty("detailText", m_detail);
        root->setProperty("footerText", m_footer);
        root->setProperty("accentColor", m_accentColor);
        root->setProperty("gainDb", m_gainDb);
        root->setProperty("muted", m_muted);
        root->setProperty("soloEnabled", m_soloEnabled);
        root->setProperty("soloed", m_soloed);
        root->setProperty("meterLevel", m_meterLevel);
    }
}
