#include "core/audio/JuceAudioEngine.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
#include <unordered_map>
#include <vector>

#include <QFileInfo>

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include "core/audio/AudioDurationProbe.h"

namespace
{
constexpr float kMixerFloorSilenceDb = -100.0F;

struct TrackIdHash
{
    std::size_t operator()(const QUuid& trackId) const noexcept
    {
        return static_cast<std::size_t>(qHash(trackId));
    }
};

juce::File toJuceFile(const QString& filePath)
{
    const auto widePath = filePath.toStdWString();
    return juce::File(juce::String(widePath.c_str()));
}

class PanAudioSource final : public juce::AudioSource
{
public:
    explicit PanAudioSource(juce::AudioSource& inputSource)
        : m_inputSource(inputSource)
    {
    }

    void setPan(const float pan)
    {
        m_pan.store(juce::jlimit(-1.0f, 1.0f, pan));
    }

    void prepareToPlay(const int samplesPerBlockExpected, const double sampleRate) override
    {
        m_inputSource.prepareToPlay(samplesPerBlockExpected, sampleRate);
    }

    void releaseResources() override
    {
        m_inputSource.releaseResources();
    }

    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override
    {
        m_inputSource.getNextAudioBlock(bufferToFill);

        auto* buffer = bufferToFill.buffer;
        if (!buffer || buffer->getNumChannels() < 2)
        {
            return;
        }

        const auto pan = m_pan.load();
        const auto angle = (pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
        const auto leftGain = std::cos(angle);
        const auto rightGain = std::sin(angle);

        buffer->applyGain(0, bufferToFill.startSample, bufferToFill.numSamples, leftGain);
        buffer->applyGain(1, bufferToFill.startSample, bufferToFill.numSamples, rightGain);
    }

private:
    juce::AudioSource& m_inputSource;
    std::atomic<float> m_pan{0.0f};
};

class GainAudioSource final : public juce::AudioSource
{
public:
    explicit GainAudioSource(juce::AudioSource& inputSource)
        : m_inputSource(inputSource)
    {
    }

    void setGainDb(const float gainDb)
    {
        m_gainLinear.store(juce::Decibels::decibelsToGain(gainDb, kMixerFloorSilenceDb));
    }

    void prepareToPlay(const int samplesPerBlockExpected, const double sampleRate) override
    {
        m_inputSource.prepareToPlay(samplesPerBlockExpected, sampleRate);
    }

    void releaseResources() override
    {
        m_inputSource.releaseResources();
    }

    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override
    {
        m_inputSource.getNextAudioBlock(bufferToFill);

        auto* buffer = bufferToFill.buffer;
        if (!buffer)
        {
            return;
        }

        buffer->applyGain(bufferToFill.startSample, bufferToFill.numSamples, m_gainLinear.load());
    }

private:
    juce::AudioSource& m_inputSource;
    std::atomic<float> m_gainLinear{1.0f};
};

class MeterAudioSource final : public juce::AudioSource
{
public:
    explicit MeterAudioSource(juce::AudioSource& inputSource)
        : m_inputSource(inputSource)
    {
    }

    [[nodiscard]] float level() const
    {
        return m_level.load();
    }

    void prepareToPlay(const int samplesPerBlockExpected, const double sampleRate) override
    {
        m_inputSource.prepareToPlay(samplesPerBlockExpected, sampleRate);
    }

    void releaseResources() override
    {
        m_inputSource.releaseResources();
        m_level.store(0.0F);
    }

    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override
    {
        m_inputSource.getNextAudioBlock(bufferToFill);

        const auto* buffer = bufferToFill.buffer;
        if (!buffer)
        {
            m_level.store(0.0F);
            return;
        }

        float peak = 0.0F;
        for (int channel = 0; channel < buffer->getNumChannels(); ++channel)
        {
            const auto* channelData = buffer->getReadPointer(channel, bufferToFill.startSample);
            for (int sampleIndex = 0; sampleIndex < bufferToFill.numSamples; ++sampleIndex)
            {
                peak = std::max(peak, std::abs(channelData[sampleIndex]));
            }
        }

        const auto previous = m_level.load();
        const auto smoothed = peak > previous ? peak : (previous * 0.90F);
        m_level.store(std::clamp(smoothed, 0.0F, 1.0F));
    }

private:
    juce::AudioSource& m_inputSource;
    std::atomic<float> m_level{0.0F};
};
}

struct JuceAudioEngine::Impl
{
    struct TrackPlayback
    {
        juce::AudioTransportSource transport;
        std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
        std::unique_ptr<GainAudioSource> gainSource;
        std::unique_ptr<PanAudioSource> panSource;
        std::unique_ptr<MeterAudioSource> meterSource;
        QString filePath;
    };

    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    juce::AudioDeviceManager deviceManager;
    juce::AudioSourcePlayer audioSourcePlayer;
    juce::MixerAudioSource mixer;
    std::unique_ptr<GainAudioSource> masterGainSource;
    std::unique_ptr<MeterAudioSource> masterMeterSource;
    juce::AudioFormatManager formatManager;
    std::unordered_map<QUuid, std::unique_ptr<TrackPlayback>, TrackIdHash> activeTracks;
    bool ready = false;
};

JuceAudioEngine::JuceAudioEngine(QObject* parent)
    : AudioEngine(parent)
    , m_impl(std::make_unique<Impl>())
{
    m_impl->formatManager.registerBasicFormats();

    const auto initError = m_impl->deviceManager.initialise(0, 2, nullptr, true);
    if (!initError.isEmpty())
    {
        emit statusChanged(QStringLiteral("JUCE audio init failed: %1").arg(QString::fromStdString(initError.toStdString())));
        return;
    }

    m_impl->masterGainSource = std::make_unique<GainAudioSource>(m_impl->mixer);
    m_impl->masterMeterSource = std::make_unique<MeterAudioSource>(*m_impl->masterGainSource);
    m_impl->audioSourcePlayer.setSource(m_impl->masterMeterSource.get());
    m_impl->deviceManager.addAudioCallback(&m_impl->audioSourcePlayer);
    m_impl->ready = true;
}

JuceAudioEngine::~JuceAudioEngine()
{
    stopAll();
    if (m_impl)
    {
        m_impl->deviceManager.removeAudioCallback(&m_impl->audioSourcePlayer);
        m_impl->audioSourcePlayer.setSource(nullptr);
    }
}

bool JuceAudioEngine::isReady() const
{
    return m_impl && m_impl->ready;
}

bool JuceAudioEngine::playTrack(const QUuid& trackId, const QString& filePath, const int offsetMs)
{
    if (!isReady())
    {
        emit statusChanged(QStringLiteral("JUCE audio backend is not ready."));
        return false;
    }

    const auto clampedOffsetMs = std::max(0, offsetMs);
    const auto activeIt = m_impl->activeTracks.find(trackId);
    if (activeIt != m_impl->activeTracks.end() && activeIt->second->filePath == filePath)
    {
        auto& transport = activeIt->second->transport;
        const auto currentOffsetMs = static_cast<int>(std::lround(transport.getCurrentPosition() * 1000.0));
        const auto driftMs = std::abs(currentOffsetMs - clampedOffsetMs);

        // Avoid tearing down and recreating the reader on small sync drift.
        if (driftMs > 120)
        {
            transport.setPosition(static_cast<double>(clampedOffsetMs) / 1000.0);
        }

        if (!transport.isPlaying())
        {
            transport.start();
        }

        if (!transport.hasStreamFinished())
        {
            return true;
        }
    }

    stopTrack(trackId);

    auto reader = std::unique_ptr<juce::AudioFormatReader>(m_impl->formatManager.createReaderFor(toJuceFile(filePath)));
    if (!reader)
    {
        emit statusChanged(
            QStringLiteral("Failed to open %1 for playback.")
                .arg(QFileInfo(filePath).fileName()));
        return false;
    }

    auto playback = std::make_unique<Impl::TrackPlayback>();
    playback->filePath = filePath;
    const auto sourceSampleRate = reader->sampleRate;
    playback->readerSource = std::make_unique<juce::AudioFormatReaderSource>(reader.release(), true);
    playback->transport.setSource(playback->readerSource.get(), 0, nullptr, sourceSampleRate);
    playback->transport.setPosition(static_cast<double>(clampedOffsetMs) / 1000.0);
    playback->gainSource = std::make_unique<GainAudioSource>(playback->transport);
    playback->panSource = std::make_unique<PanAudioSource>(*playback->gainSource);
    playback->meterSource = std::make_unique<MeterAudioSource>(*playback->panSource);

    m_impl->mixer.addInputSource(playback->meterSource.get(), false);
    playback->transport.start();
    m_impl->activeTracks.emplace(trackId, std::move(playback));
    return true;
}

void JuceAudioEngine::setTrackGain(const QUuid& trackId, const float gainDb)
{
    if (!m_impl)
    {
        return;
    }

    const auto activeIt = m_impl->activeTracks.find(trackId);
    if (activeIt == m_impl->activeTracks.end() || !activeIt->second->gainSource)
    {
        return;
    }

    activeIt->second->gainSource->setGainDb(gainDb);
}

void JuceAudioEngine::setTrackPan(const QUuid& trackId, const float pan)
{
    if (!m_impl)
    {
        return;
    }

    const auto activeIt = m_impl->activeTracks.find(trackId);
    if (activeIt == m_impl->activeTracks.end() || !activeIt->second->panSource)
    {
        return;
    }

    activeIt->second->panSource->setPan(pan);
}

void JuceAudioEngine::setMasterGain(const float gainDb)
{
    if (!m_impl || !m_impl->masterGainSource)
    {
        return;
    }

    m_impl->masterGainSource->setGainDb(gainDb);
}

void JuceAudioEngine::stopTrack(const QUuid& trackId)
{
    if (!m_impl)
    {
        return;
    }

    const auto activeIt = m_impl->activeTracks.find(trackId);
    if (activeIt == m_impl->activeTracks.end())
    {
        return;
    }

    auto& playback = *activeIt->second;
    playback.transport.stop();
    if (playback.meterSource)
    {
        m_impl->mixer.removeInputSource(playback.meterSource.get());
    }
    playback.transport.setSource(nullptr);
    m_impl->activeTracks.erase(activeIt);
}

void JuceAudioEngine::stopAll()
{
    if (!m_impl)
    {
        return;
    }

    std::vector<QUuid> trackIds;
    trackIds.reserve(m_impl->activeTracks.size());
    for (auto it = m_impl->activeTracks.cbegin(); it != m_impl->activeTracks.cend(); ++it)
    {
        trackIds.push_back(it->first);
    }

    for (const auto& trackId : trackIds)
    {
        stopTrack(trackId);
    }
}

bool JuceAudioEngine::isTrackPlaying(const QUuid& trackId) const
{
    if (!m_impl)
    {
        return false;
    }

    const auto activeIt = m_impl->activeTracks.find(trackId);
    return activeIt != m_impl->activeTracks.end() && activeIt->second->transport.isPlaying();
}

float JuceAudioEngine::trackLevel(const QUuid& trackId) const
{
    if (!m_impl)
    {
        return 0.0F;
    }

    const auto activeIt = m_impl->activeTracks.find(trackId);
    if (activeIt == m_impl->activeTracks.end() || !activeIt->second->meterSource)
    {
        return 0.0F;
    }

    return activeIt->second->meterSource->level();
}

float JuceAudioEngine::masterLevel() const
{
    if (!m_impl || !m_impl->masterMeterSource)
    {
        return 0.0F;
    }

    return m_impl->masterMeterSource->level();
}

std::optional<int> JuceAudioEngine::durationMs(const QString& filePath) const
{
    if (m_impl)
    {
        auto reader = std::unique_ptr<juce::AudioFormatReader>(m_impl->formatManager.createReaderFor(toJuceFile(filePath)));
        if (reader && reader->sampleRate > 0.0)
        {
            return static_cast<int>(std::lround((static_cast<double>(reader->lengthInSamples) * 1000.0) / reader->sampleRate));
        }
    }

    return dawg::audio::probeAudioDurationMs(filePath);
}
