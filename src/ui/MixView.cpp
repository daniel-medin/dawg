#include "ui/MixView.h"

#include "ui/QuickMixStripWidget.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QSizePolicy>

MixView::MixView(QWidget* parent)
    : QWidget(parent)
    , m_layout(new QHBoxLayout(this))
    , m_emptyLabel(new QLabel(QStringLiteral("No mix lanes yet."), this))
{
    setObjectName(QStringLiteral("mixView"));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_layout->setContentsMargins(12, 10, 12, 10);
    m_layout->setSpacing(10);

    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setStyleSheet(QStringLiteral("color: #8d9aae; font-size: 9pt;"));
    m_layout->addWidget(m_emptyLabel, 1);
}

void MixView::setMixState(const float masterGainDb, const bool masterMuted, const std::vector<MixLaneStrip>& laneStrips)
{
    const auto shouldRebuild = needsRebuild(laneStrips);
    m_masterGainDb = masterGainDb;
    m_masterMuted = masterMuted;
    m_laneStrips = laneStrips;

    if (shouldRebuild)
    {
        rebuildStrips();
        return;
    }

    syncStripStates();
}

void MixView::setMeterLevels(const float masterMeterLevel, const std::vector<MixLaneStrip>& laneStrips)
{
    if (m_masterQuickStrip)
    {
        m_masterQuickStrip->setMeterLevel(masterMeterLevel);
    }

    for (const auto& strip : laneStrips)
    {
        const auto stripIt = m_laneQuickStrips.find(strip.laneIndex);
        if (stripIt == m_laneQuickStrips.end() || !stripIt->second)
        {
            continue;
        }

        stripIt->second->setMeterLevel(strip.meterLevel);
    }
}

QSize MixView::sizeHint() const
{
    return QSize{640, 184};
}

QSize MixView::minimumSizeHint() const
{
    return QSize{320, 132};
}

bool MixView::needsRebuild(const std::vector<MixLaneStrip>& laneStrips) const
{
    if (!m_masterQuickStrip)
    {
        return true;
    }

    if (laneStrips.size() != m_laneStrips.size())
    {
        return true;
    }

    for (std::size_t index = 0; index < laneStrips.size(); ++index)
    {
        const auto& nextStrip = laneStrips[index];
        const auto& currentStrip = m_laneStrips[index];
        if (nextStrip.laneIndex != currentStrip.laneIndex
            || nextStrip.label != currentStrip.label
            || nextStrip.color != currentStrip.color
            || nextStrip.clipCount != currentStrip.clipCount)
        {
            return true;
        }
    }

    return false;
}

void MixView::syncStripStates()
{
    if (m_masterQuickStrip)
    {
        m_masterQuickStrip->setGainDb(m_masterGainDb);
        m_masterQuickStrip->setMuted(m_masterMuted);
    }

    for (const auto& strip : m_laneStrips)
    {
        const auto stripIt = m_laneQuickStrips.find(strip.laneIndex);
        if (stripIt == m_laneQuickStrips.end() || !stripIt->second)
        {
            continue;
        }

        stripIt->second->setGainDb(strip.gainDb);
        stripIt->second->setMuted(strip.muted);
        stripIt->second->setSoloed(strip.soloed);
    }
}

void MixView::rebuildStrips()
{
    m_masterQuickStrip = nullptr;
    m_laneQuickStrips.clear();
    while (auto* item = m_layout->takeAt(0))
    {
        if (auto* widget = item->widget())
        {
            if (widget != m_emptyLabel)
            {
                widget->deleteLater();
            }
        }
        delete item;
    }

    auto* masterStrip = new QuickMixStripWidget(this);
    masterStrip->setMasterStrip(true);
    masterStrip->setTitle(QStringLiteral("Master"));
    masterStrip->setDetail(QStringLiteral("Main Out"));
    masterStrip->setFooter(QStringLiteral("MASTER"));
    masterStrip->setAccentColor(QColor(QStringLiteral("#f0f4f8")));
    masterStrip->setGainDb(m_masterGainDb);
    masterStrip->setMuted(m_masterMuted);
    masterStrip->setSoloEnabled(false);
    masterStrip->setSoloed(false);
    masterStrip->setGainChangedHandler([this](const float gainDb)
    {
        emit masterGainChanged(gainDb);
    });
    masterStrip->setMutedChangedHandler([this](const bool muted)
    {
        emit masterMutedChanged(muted);
    });
    m_masterQuickStrip = masterStrip;
    m_layout->addWidget(masterStrip, 0);

    if (m_laneStrips.empty())
    {
        m_layout->addSpacing(6);
        m_layout->addWidget(m_emptyLabel, 1);
        m_emptyLabel->show();
        return;
    }

    m_emptyLabel->hide();
    for (const auto& strip : m_laneStrips)
    {
        auto* stripWidget = new QuickMixStripWidget(this);
        stripWidget->setMasterStrip(false);
        stripWidget->setTitle(strip.label);
        stripWidget->setDetail(
            strip.clipCount == 1
                ? QStringLiteral("1 clip")
                : QStringLiteral("%1 clips").arg(strip.clipCount));
        stripWidget->setFooter(QStringLiteral("Lane %1").arg(strip.laneIndex + 1));
        stripWidget->setAccentColor(strip.color);
        stripWidget->setGainDb(strip.gainDb);
        stripWidget->setMuted(strip.muted);
        stripWidget->setSoloEnabled(true);
        stripWidget->setSoloed(strip.soloed);
        stripWidget->setGainChangedHandler([this, laneIndex = strip.laneIndex](const float gainDb)
        {
            emit laneGainChanged(laneIndex, gainDb);
        });
        stripWidget->setMutedChangedHandler([this, laneIndex = strip.laneIndex](const bool muted)
        {
            emit laneMutedChanged(laneIndex, muted);
        });
        stripWidget->setSoloChangedHandler([this, laneIndex = strip.laneIndex](const bool soloed)
        {
            emit laneSoloChanged(laneIndex, soloed);
        });
        m_laneQuickStrips[strip.laneIndex] = stripWidget;
        m_layout->addWidget(stripWidget, 0);
    }

    m_layout->addStretch(1);
    syncStripStates();
}
