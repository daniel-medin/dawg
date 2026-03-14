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

struct ResolvedTrackPlaybackOptions
{
    int offsetMs = 0;
    int clipStartMs = 0;
    int clipEndMs = 0;
    int clipLengthMs = 0;
    int transportOffsetMs = 0;
    int64_t clipStartSample = 0;
    int64_t clipLengthSamples = 0;
    bool loopEnabled = false;
    bool usesClipSubsection = false;
};

int millisecondsFromSamples(const int64_t samples, const double sampleRate)
{
    if (sampleRate <= 0.0 || samples <= 0)
    {
        return 0;
    }

    return static_cast<int>(std::lround((static_cast<double>(samples) * 1000.0) / sampleRate));
}

int64_t samplesFromMilliseconds(const int milliseconds, const double sampleRate)
{
    if (sampleRate <= 0.0 || milliseconds <= 0)
    {
        return 0;
    }

    return static_cast<int64_t>(std::llround((static_cast<double>(milliseconds) * sampleRate) / 1000.0));
}

ResolvedTrackPlaybackOptions resolveTrackPlaybackOptions(
    const AudioEngine::TrackPlaybackOptions& options,
    const double sampleRate,
    const int64_t sourceLengthSamples)
{
    ResolvedTrackPlaybackOptions resolved;
    resolved.loopEnabled = options.loopEnabled;
    resolved.usesClipSubsection = options.clipStartMs > 0 || options.clipEndMs.has_value();

    const auto sourceDurationMs = millisecondsFromSamples(sourceLengthSamples, sampleRate);
    resolved.offsetMs = std::clamp(options.offsetMs, 0, sourceDurationMs);

    if (sourceDurationMs <= 0 || sampleRate <= 0.0 || sourceLengthSamples <= 0)
    {
        return resolved;
    }

    if (resolved.usesClipSubsection)
    {
        resolved.clipStartMs = std::clamp(options.clipStartMs, 0, sourceDurationMs);
        const auto requestedClipEndMs = options.clipEndMs.value_or(sourceDurationMs);
        resolved.clipEndMs = std::clamp(requestedClipEndMs, resolved.clipStartMs + 1, sourceDurationMs);
    }
    else
    {
        resolved.clipStartMs = 0;
        resolved.clipEndMs = sourceDurationMs;
    }

    resolved.clipLengthMs = std::max(1, resolved.clipEndMs - resolved.clipStartMs);
    const auto relativeOffsetMs = std::max(0, resolved.offsetMs - resolved.clipStartMs);
    resolved.transportOffsetMs = resolved.loopEnabled
        ? (relativeOffsetMs % resolved.clipLengthMs)
        : std::clamp(relativeOffsetMs, 0, resolved.clipLengthMs);

    resolved.clipStartSample = std::clamp(samplesFromMilliseconds(resolved.clipStartMs, sampleRate), int64_t{0}, sourceLengthSamples);
    const auto clipEndSample = std::clamp(
        samplesFromMilliseconds(resolved.clipEndMs, sampleRate),
        resolved.clipStartSample + 1,
        sourceLengthSamples);
    resolved.clipLengthSamples = std::max<int64_t>(1, clipEndSample - resolved.clipStartSample);
    return resolved;
}

bool matchesRequestedPlaybackConfiguration(
    const ResolvedTrackPlaybackOptions& existing,
    const AudioEngine::TrackPlaybackOptions& requested)
{
    const auto requestedUsesClipSubsection = requested.clipStartMs > 0 || requested.clipEndMs.has_value();
    if (existing.usesClipSubsection != requestedUsesClipSubsection)
    {
        return false;
    }

    if (existing.loopEnabled != requested.loopEnabled
        || existing.clipStartMs != std::max(0, requested.clipStartMs))
    {
        return false;
    }

    if (!requestedUsesClipSubsection)
    {
        return existing.clipStartMs == 0;
    }

    return requested.clipEndMs.has_value() && existing.clipEndMs == *requested.clipEndMs;
}

int transportOffsetMsForRequest(
    const ResolvedTrackPlaybackOptions& existing,
    const int requestedOffsetMs)
{
    const auto clampedOffsetMs = std::clamp(requestedOffsetMs, 0, existing.clipEndMs);
    const auto relativeOffsetMs = std::max(0, clampedOffsetMs - existing.clipStartMs);
    if (existing.loopEnabled)
    {
        return relativeOffsetMs % existing.clipLengthMs;
    }

    return std::clamp(relativeOffsetMs, 0, existing.clipLengthMs);
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
        m_sampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
        m_smoothedMeanSquare = 0.0F;
        m_level.store(0.0F);
    }

    void releaseResources() override
    {
        m_inputSource.releaseResources();
        m_smoothedMeanSquare = 0.0F;
        m_level.store(0.0F);
    }

    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override
    {
        m_inputSource.getNextAudioBlock(bufferToFill);

        const auto* buffer = bufferToFill.buffer;
        if (!buffer || bufferToFill.numSamples <= 0 || buffer->getNumChannels() <= 0)
        {
            m_level.store(0.0F);
            return;
        }

        double sumSquares = 0.0;
        for (int channel = 0; channel < buffer->getNumChannels(); ++channel)
        {
            const auto* channelData = buffer->getReadPointer(channel, bufferToFill.startSample);
            for (int sampleIndex = 0; sampleIndex < bufferToFill.numSamples; ++sampleIndex)
            {
                const auto sample = static_cast<double>(channelData[sampleIndex]);
                sumSquares += sample * sample;
            }
        }

        const auto sampleCount = static_cast<double>(buffer->getNumChannels() * bufferToFill.numSamples);
        const auto meanSquare = sampleCount > 0.0 ? static_cast<float>(sumSquares / sampleCount) : 0.0F;
        const auto targetIsRising = meanSquare > m_smoothedMeanSquare;
        const auto smoothingSeconds = targetIsRising ? 0.035 : 0.180;
        const auto coefficient = static_cast<float>(std::exp(
            -static_cast<double>(bufferToFill.numSamples) / (m_sampleRate * smoothingSeconds)));
        m_smoothedMeanSquare = (m_smoothedMeanSquare * coefficient) + (meanSquare * (1.0F - coefficient));
        m_level.store(std::clamp(std::sqrt(m_smoothedMeanSquare), 0.0F, 1.0F));
    }

private:
    juce::AudioSource& m_inputSource;
    std::atomic<float> m_level{0.0F};
    double m_sampleRate = 44100.0;
    float m_smoothedMeanSquare = 0.0F;
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
        ResolvedTrackPlaybackOptions playbackOptions;
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
    TrackPlaybackOptions options;
    options.offsetMs = offsetMs;
    return playTrack(trackId, filePath, options);
}

bool JuceAudioEngine::playTrack(const QUuid& trackId, const QString& filePath, const TrackPlaybackOptions& options)
{
    if (!isReady())
    {
        emit statusChanged(QStringLiteral("JUCE audio backend is not ready."));
        return false;
    }

    const auto activeIt = m_impl->activeTracks.find(trackId);
    if (activeIt != m_impl->activeTracks.end()
        && activeIt->second->filePath == filePath
        && matchesRequestedPlaybackConfiguration(activeIt->second->playbackOptions, options))
    {
        const auto requestedTransportOffsetMs =
            transportOffsetMsForRequest(activeIt->second->playbackOptions, options.offsetMs);
        auto& transport = activeIt->second->transport;
        const auto currentOffsetMs = static_cast<int>(std::lround(transport.getCurrentPosition() * 1000.0));
        const auto driftMs = std::abs(currentOffsetMs - requestedTransportOffsetMs);

        // Avoid tearing down and recreating the reader on small sync drift.
        if (driftMs > 120)
        {
            transport.setPosition(static_cast<double>(requestedTransportOffsetMs) / 1000.0);
        }

        const auto shouldRemainSilent = !activeIt->second->playbackOptions.loopEnabled
            && requestedTransportOffsetMs >= activeIt->second->playbackOptions.clipLengthMs;
        if (shouldRemainSilent)
        {
            transport.stop();
            transport.setPosition(static_cast<double>(activeIt->second->playbackOptions.clipLengthMs) / 1000.0);
            return true;
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

    auto reader = std::unique_ptr<juce::AudioFormatReader>(m_impl->formatManager.createReaderFor(toJuceFile(filePath)));
    if (!reader)
    {
        emit statusChanged(
            QStringLiteral("Failed to open %1 for playback.")
                .arg(QFileInfo(filePath).fileName()));
        return false;
    }

    const auto sourceSampleRate = reader->sampleRate;
    const auto resolvedOptions = resolveTrackPlaybackOptions(options, sourceSampleRate, reader->lengthInSamples);
    if (resolvedOptions.clipLengthSamples <= 0)
    {
        return false;
    }

    stopTrack(trackId);

    const auto shouldRemainSilent = !resolvedOptions.loopEnabled
        && resolvedOptions.transportOffsetMs >= resolvedOptions.clipLengthMs;
    if (shouldRemainSilent)
    {
        return true;
    }

    auto playback = std::make_unique<Impl::TrackPlayback>();
    playback->filePath = filePath;
    playback->playbackOptions = resolvedOptions;
    std::unique_ptr<juce::AudioFormatReader> playbackReader;
    if (resolvedOptions.usesClipSubsection)
    {
        playbackReader = std::make_unique<juce::AudioSubsectionReader>(
            reader.release(),
            resolvedOptions.clipStartSample,
            resolvedOptions.clipLengthSamples,
            true);
    }
    else
    {
        playbackReader = std::move(reader);
    }

    playback->readerSource = std::make_unique<juce::AudioFormatReaderSource>(playbackReader.release(), true);
    playback->readerSource->setLooping(resolvedOptions.loopEnabled);
    playback->transport.setSource(playback->readerSource.get(), 0, nullptr, sourceSampleRate);
    playback->transport.setPosition(static_cast<double>(resolvedOptions.transportOffsetMs) / 1000.0);
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
