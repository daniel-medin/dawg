#pragma once

#include <unordered_map>

class AudioEngine;

class MixStateStore
{
public:
    void reset();

    [[nodiscard]] static float clampGainDb(float gainDb);

    [[nodiscard]] bool setMasterGainDb(float gainDb);
    [[nodiscard]] bool setMasterMuted(bool muted);
    [[nodiscard]] bool setLaneGainDb(int laneIndex, float gainDb);
    [[nodiscard]] bool setLaneMuted(int laneIndex, bool muted);
    [[nodiscard]] bool setLaneSoloed(int laneIndex, bool soloed);

    void restore(
        float masterGainDb,
        bool masterMuted,
        const std::unordered_map<int, float>& gainByLane,
        const std::unordered_map<int, bool>& mutedByLane,
        const std::unordered_map<int, bool>& soloByLane);

    void applyMasterGain(AudioEngine& audioEngine) const;

    [[nodiscard]] float masterGainDb() const;
    [[nodiscard]] bool masterMuted() const;
    [[nodiscard]] float laneGainDb(int laneIndex) const;
    [[nodiscard]] bool isLaneMuted(int laneIndex) const;
    [[nodiscard]] bool isLaneSoloed(int laneIndex) const;

    [[nodiscard]] const std::unordered_map<int, float>& gainByLane() const;
    [[nodiscard]] const std::unordered_map<int, bool>& mutedByLane() const;
    [[nodiscard]] const std::unordered_map<int, bool>& soloByLane() const;

private:
    float m_masterGainDb = 0.0F;
    bool m_masterMuted = false;
    std::unordered_map<int, float> m_gainByLane;
    std::unordered_map<int, bool> m_mutedByLane;
    std::unordered_map<int, bool> m_soloByLane;
};
