#include "app/MixStateStore.h"

#include <algorithm>
#include <cmath>

#include "core/audio/AudioEngine.h"

namespace
{
constexpr float kMinMixGainDb = -100.0F;
constexpr float kMaxMixGainDb = 12.0F;
constexpr float kSilentMixGainDb = -100.0F;
}

void MixStateStore::reset()
{
    m_masterGainDb = 0.0F;
    m_masterMuted = false;
    m_gainByLane.clear();
    m_mutedByLane.clear();
    m_soloByLane.clear();
    m_soloMode = SoloMode::Latch;
}

float MixStateStore::clampGainDb(const float gainDb)
{
    return std::clamp(gainDb, kMinMixGainDb, kMaxMixGainDb);
}

bool MixStateStore::setMasterGainDb(const float gainDb)
{
    const auto clampedGainDb = clampGainDb(gainDb);
    if (std::abs(m_masterGainDb - clampedGainDb) < 0.001F)
    {
        return false;
    }

    m_masterGainDb = clampedGainDb;
    return true;
}

bool MixStateStore::setMasterMuted(const bool muted)
{
    if (m_masterMuted == muted)
    {
        return false;
    }

    m_masterMuted = muted;
    return true;
}

bool MixStateStore::setLaneGainDb(const int laneIndex, const float gainDb)
{
    if (laneIndex < 0)
    {
        return false;
    }

    const auto clampedGainDb = clampGainDb(gainDb);
    const auto existingIt = m_gainByLane.find(laneIndex);
    if (existingIt != m_gainByLane.end() && std::abs(existingIt->second - clampedGainDb) < 0.001F)
    {
        return false;
    }

    if (std::abs(clampedGainDb) < 0.001F)
    {
        m_gainByLane.erase(laneIndex);
    }
    else
    {
        m_gainByLane[laneIndex] = clampedGainDb;
    }

    return true;
}

bool MixStateStore::setLaneMuted(const int laneIndex, const bool muted)
{
    if (laneIndex < 0)
    {
        return false;
    }

    const auto existingIt = m_mutedByLane.find(laneIndex);
    if (existingIt != m_mutedByLane.end() && existingIt->second == muted)
    {
        return false;
    }

    if (muted)
    {
        m_mutedByLane[laneIndex] = true;
    }
    else
    {
        m_mutedByLane.erase(laneIndex);
    }

    return true;
}

bool MixStateStore::setLaneSoloed(const int laneIndex, const bool soloed)
{
    if (laneIndex < 0)
    {
        return false;
    }

    const auto existingIt = m_soloByLane.find(laneIndex);
    if (m_soloMode == SoloMode::Xor && soloed)
    {
        if (m_soloByLane.size() == 1 && existingIt != m_soloByLane.end() && existingIt->second)
        {
            return false;
        }

        m_soloByLane.clear();
        m_soloByLane[laneIndex] = true;
        return true;
    }

    if (existingIt != m_soloByLane.end() && existingIt->second == soloed)
    {
        return false;
    }

    if (soloed)
    {
        m_soloByLane[laneIndex] = true;
    }
    else
    {
        m_soloByLane.erase(laneIndex);
    }

    return true;
}

bool MixStateStore::setSoloMode(const SoloMode soloMode)
{
    if (m_soloMode == soloMode)
    {
        return false;
    }

    m_soloMode = soloMode;
    if (m_soloMode == SoloMode::Xor && m_soloByLane.size() > 1)
    {
        const auto keepLaneIndex = std::min_element(
            m_soloByLane.cbegin(),
            m_soloByLane.cend(),
            [](const auto& left, const auto& right)
            {
                return left.first < right.first;
            })->first;
        m_soloByLane.clear();
        m_soloByLane[keepLaneIndex] = true;
    }
    return true;
}

void MixStateStore::restore(
    const float masterGainDb,
    const bool masterMuted,
    const std::unordered_map<int, float>& gainByLane,
    const std::unordered_map<int, bool>& mutedByLane,
    const std::unordered_map<int, bool>& soloByLane,
    const SoloMode soloMode)
{
    reset();
    m_masterGainDb = clampGainDb(masterGainDb);
    m_masterMuted = masterMuted;
    m_soloMode = soloMode;

    for (const auto& [laneIndex, gainDb] : gainByLane)
    {
        static_cast<void>(setLaneGainDb(laneIndex, gainDb));
    }
    for (const auto& [laneIndex, muted] : mutedByLane)
    {
        static_cast<void>(setLaneMuted(laneIndex, muted));
    }
    for (const auto& [laneIndex, soloed] : soloByLane)
    {
        static_cast<void>(setLaneSoloed(laneIndex, soloed));
    }
}

void MixStateStore::applyMasterGain(AudioEngine& audioEngine) const
{
    audioEngine.setMasterGain(m_masterMuted ? kSilentMixGainDb : m_masterGainDb);
}

float MixStateStore::masterGainDb() const
{
    return m_masterGainDb;
}

bool MixStateStore::masterMuted() const
{
    return m_masterMuted;
}

float MixStateStore::laneGainDb(const int laneIndex) const
{
    const auto gainIt = m_gainByLane.find(laneIndex);
    return gainIt != m_gainByLane.end() ? gainIt->second : 0.0F;
}

bool MixStateStore::isLaneMuted(const int laneIndex) const
{
    const auto mutedIt = m_mutedByLane.find(laneIndex);
    return mutedIt != m_mutedByLane.end() && mutedIt->second;
}

bool MixStateStore::isLaneSoloed(const int laneIndex) const
{
    const auto soloIt = m_soloByLane.find(laneIndex);
    return soloIt != m_soloByLane.end() && soloIt->second;
}

MixStateStore::SoloMode MixStateStore::soloMode() const
{
    return m_soloMode;
}

const std::unordered_map<int, float>& MixStateStore::gainByLane() const
{
    return m_gainByLane;
}

const std::unordered_map<int, bool>& MixStateStore::mutedByLane() const
{
    return m_mutedByLane;
}

const std::unordered_map<int, bool>& MixStateStore::soloByLane() const
{
    return m_soloByLane;
}
