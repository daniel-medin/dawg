#include "core/audio/JuceAudioEngine.h"

#include <algorithm>
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
}

struct JuceAudioEngine::Impl
{
    struct TrackPlayback
    {
        juce::AudioTransportSource transport;
        std::unique_ptr<juce::AudioFormatReaderSource> readerSource;
        QString filePath;
    };

    juce::ScopedJuceInitialiser_GUI juceInitialiser;
    juce::AudioDeviceManager deviceManager;
    juce::AudioSourcePlayer audioSourcePlayer;
    juce::MixerAudioSource mixer;
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

    m_impl->audioSourcePlayer.setSource(&m_impl->mixer);
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
        const auto currentOffsetMs = static_cast<int>(std::lround(activeIt->second->transport.getCurrentPosition() * 1000.0));
        if (activeIt->second->transport.isPlaying() && std::abs(currentOffsetMs - clampedOffsetMs) <= 40)
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

    m_impl->mixer.addInputSource(&playback->transport, false);
    playback->transport.start();
    m_impl->activeTracks.emplace(trackId, std::move(playback));
    return true;
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
    m_impl->mixer.removeInputSource(&playback.transport);
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
