#include "ui/MixQuickController.h"

#include <algorithm>

#include <QVariant>

MixStripObject::MixStripObject(QObject* parent)
    : QObject(parent)
{
}

int MixStripObject::laneIndex() const
{
    return m_laneIndex;
}

void MixStripObject::setLaneIndex(const int laneIndex)
{
    if (m_laneIndex == laneIndex)
    {
        return;
    }

    m_laneIndex = laneIndex;
    emit laneIndexChanged();
}

bool MixStripObject::masterStrip() const
{
    return m_masterStrip;
}

void MixStripObject::setMasterStrip(const bool masterStrip)
{
    if (m_masterStrip == masterStrip)
    {
        return;
    }

    m_masterStrip = masterStrip;
    emit masterStripChanged();
}

QString MixStripObject::titleText() const
{
    return m_titleText;
}

void MixStripObject::setTitleText(const QString& titleText)
{
    if (m_titleText == titleText)
    {
        return;
    }

    m_titleText = titleText;
    emit titleTextChanged();
}

QString MixStripObject::detailText() const
{
    return m_detailText;
}

void MixStripObject::setDetailText(const QString& detailText)
{
    if (m_detailText == detailText)
    {
        return;
    }

    m_detailText = detailText;
    emit detailTextChanged();
}

QString MixStripObject::footerText() const
{
    return m_footerText;
}

void MixStripObject::setFooterText(const QString& footerText)
{
    if (m_footerText == footerText)
    {
        return;
    }

    m_footerText = footerText;
    emit footerTextChanged();
}

QColor MixStripObject::accentColor() const
{
    return m_accentColor;
}

void MixStripObject::setAccentColor(const QColor& accentColor)
{
    if (m_accentColor == accentColor)
    {
        return;
    }

    m_accentColor = accentColor;
    emit accentColorChanged();
}

float MixStripObject::gainDb() const
{
    return m_gainDb;
}

void MixStripObject::setGainDb(const float gainDb)
{
    if (std::abs(m_gainDb - gainDb) <= 0.001F)
    {
        return;
    }

    m_gainDb = gainDb;
    emit gainDbChanged();
}

bool MixStripObject::muted() const
{
    return m_muted;
}

void MixStripObject::setMuted(const bool muted)
{
    if (m_muted == muted)
    {
        return;
    }

    m_muted = muted;
    emit mutedChanged();
}

bool MixStripObject::soloEnabled() const
{
    return m_soloEnabled;
}

void MixStripObject::setSoloEnabled(const bool soloEnabled)
{
    if (m_soloEnabled == soloEnabled)
    {
        return;
    }

    m_soloEnabled = soloEnabled;
    emit soloEnabledChanged();
}

bool MixStripObject::soloed() const
{
    return m_soloed;
}

void MixStripObject::setSoloed(const bool soloed)
{
    if (m_soloed == soloed)
    {
        return;
    }

    m_soloed = soloed;
    emit soloedChanged();
}

bool MixStripObject::useStereoMeter() const
{
    return m_useStereoMeter;
}

void MixStripObject::setUseStereoMeter(const bool useStereoMeter)
{
    if (m_useStereoMeter == useStereoMeter)
    {
        return;
    }

    m_useStereoMeter = useStereoMeter;
    emit useStereoMeterChanged();
}

float MixStripObject::meterLevel() const
{
    return m_meterLevel;
}

void MixStripObject::setMeterLevel(const float meterLevel)
{
    const auto clampedLevel = std::clamp(meterLevel, 0.0F, 1.0F);
    if (std::abs(m_meterLevel - clampedLevel) <= 0.0001F)
    {
        return;
    }

    m_meterLevel = clampedLevel;
    emit meterLevelChanged();
}

float MixStripObject::meterLeftLevel() const
{
    return m_meterLeftLevel;
}

void MixStripObject::setMeterLeftLevel(const float meterLeftLevel)
{
    const auto clampedLevel = std::clamp(meterLeftLevel, 0.0F, 1.0F);
    if (std::abs(m_meterLeftLevel - clampedLevel) <= 0.0001F)
    {
        return;
    }

    m_meterLeftLevel = clampedLevel;
    emit meterLeftLevelChanged();
}

float MixStripObject::meterRightLevel() const
{
    return m_meterRightLevel;
}

void MixStripObject::setMeterRightLevel(const float meterRightLevel)
{
    const auto clampedLevel = std::clamp(meterRightLevel, 0.0F, 1.0F);
    if (std::abs(m_meterRightLevel - clampedLevel) <= 0.0001F)
    {
        return;
    }

    m_meterRightLevel = clampedLevel;
    emit meterRightLevelChanged();
}

MixQuickController::MixQuickController(QObject* parent)
    : QObject(parent)
    , m_masterStrip(new MixStripObject(this))
{
    m_masterStrip->setMasterStrip(true);
    m_masterStrip->setLaneIndex(-1);
    m_masterStrip->setTitleText(QStringLiteral("Master"));
    m_masterStrip->setDetailText(QStringLiteral("Main Out"));
    m_masterStrip->setFooterText(QStringLiteral("MASTER"));
    m_masterStrip->setAccentColor(QColor(QStringLiteral("#f0f4f8")));
    m_masterStrip->setSoloEnabled(false);
    m_masterStrip->setUseStereoMeter(true);
}

QObject* MixQuickController::masterStrip() const
{
    return m_masterStrip;
}

QObjectList MixQuickController::laneStrips() const
{
    return m_laneStrips;
}

bool MixQuickController::playbackActive() const
{
    return m_playbackActive;
}

int MixQuickController::meterResetToken() const
{
    return m_meterResetToken;
}

void MixQuickController::setMasterState(const float gainDb, const bool muted)
{
    m_masterStrip->setGainDb(gainDb);
    m_masterStrip->setMuted(muted);
}

void MixQuickController::setMasterMeterLevels(const float leftLevel, const float rightLevel)
{
    m_masterStrip->setMeterLeftLevel(leftLevel);
    m_masterStrip->setMeterRightLevel(rightLevel);
    m_masterStrip->setMeterLevel(std::max(leftLevel, rightLevel));
}

void MixQuickController::setLaneStrips(const QVariantList& descriptors)
{
    qDeleteAll(m_laneStrips);
    m_laneStrips.clear();
    m_laneStripsByIndex.clear();

    for (const auto& descriptorValue : descriptors)
    {
        const auto descriptor = descriptorValue.toMap();
        auto* strip = new MixStripObject(this);
        strip->setMasterStrip(false);
        strip->setLaneIndex(descriptor.value(QStringLiteral("laneIndex")).toInt());
        strip->setTitleText(descriptor.value(QStringLiteral("titleText")).toString());
        strip->setDetailText(descriptor.value(QStringLiteral("detailText")).toString());
        strip->setFooterText(descriptor.value(QStringLiteral("footerText")).toString());
        strip->setAccentColor(descriptor.value(QStringLiteral("accentColor")).value<QColor>());
        strip->setGainDb(descriptor.value(QStringLiteral("gainDb")).toFloat());
        strip->setMuted(descriptor.value(QStringLiteral("muted")).toBool());
        strip->setSoloEnabled(descriptor.value(QStringLiteral("soloEnabled")).toBool());
        strip->setSoloed(descriptor.value(QStringLiteral("soloed")).toBool());
        strip->setUseStereoMeter(descriptor.value(QStringLiteral("useStereoMeter")).toBool());
        const auto meterLevel = descriptor.value(QStringLiteral("meterLevel")).toFloat();
        strip->setMeterLevel(meterLevel);
        strip->setMeterLeftLevel(descriptor.value(QStringLiteral("meterLeftLevel")).toFloat());
        strip->setMeterRightLevel(descriptor.value(QStringLiteral("meterRightLevel")).toFloat());
        m_laneStrips.push_back(strip);
        m_laneStripsByIndex[strip->laneIndex()] = strip;
    }

    emit laneStripsChanged();
}

void MixQuickController::setLaneState(const int laneIndex, const float gainDb, const bool muted, const bool soloed)
{
    if (auto* strip = laneStrip(laneIndex))
    {
        strip->setGainDb(gainDb);
        strip->setMuted(muted);
        strip->setSoloed(soloed);
    }
}

void MixQuickController::setLaneMeterLevels(const int laneIndex, const float leftLevel, const float rightLevel)
{
    if (auto* strip = laneStrip(laneIndex))
    {
        strip->setMeterLeftLevel(leftLevel);
        strip->setMeterRightLevel(rightLevel);
        strip->setMeterLevel(std::max(leftLevel, rightLevel));
    }
}

void MixQuickController::setPlaybackActive(const bool playbackActive)
{
    if (m_playbackActive == playbackActive)
    {
        return;
    }

    const auto restartingPlayback = playbackActive && !m_playbackActive;
    m_playbackActive = playbackActive;
    emit playbackActiveChanged();

    if (restartingPlayback)
    {
        ++m_meterResetToken;
        emit meterResetTokenChanged();
    }
}

void MixQuickController::setMasterGainDb(const double gainDb)
{
    const auto gain = static_cast<float>(gainDb);
    m_masterStrip->setGainDb(gain);
    emit masterGainChanged(gain);
}

void MixQuickController::setMasterMuted(const bool muted)
{
    m_masterStrip->setMuted(muted);
    emit masterMutedChanged(muted);
}

void MixQuickController::setLaneGainDb(const int laneIndex, const double gainDb)
{
    const auto gain = static_cast<float>(gainDb);
    if (auto* strip = laneStrip(laneIndex))
    {
        strip->setGainDb(gain);
    }
    emit laneGainChanged(laneIndex, gain);
}

void MixQuickController::setLaneMuted(const int laneIndex, const bool muted)
{
    if (auto* strip = laneStrip(laneIndex))
    {
        strip->setMuted(muted);
    }
    emit laneMutedChanged(laneIndex, muted);
}

void MixQuickController::setLaneSoloed(const int laneIndex, const bool soloed)
{
    if (auto* strip = laneStrip(laneIndex))
    {
        strip->setSoloed(soloed);
    }
    emit laneSoloChanged(laneIndex, soloed);
}

MixStripObject* MixQuickController::laneStrip(const int laneIndex) const
{
    const auto it = m_laneStripsByIndex.find(laneIndex);
    return it == m_laneStripsByIndex.end() ? nullptr : it->second;
}
