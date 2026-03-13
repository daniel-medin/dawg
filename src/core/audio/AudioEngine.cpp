#include "core/audio/AudioEngine.h"

#include "core/audio/JuceAudioEngine.h"
#include "core/audio/MciAudioEngine.h"

AudioEngine::AudioEngine(QObject* parent)
    : QObject(parent)
{
}

AudioEngine::~AudioEngine() = default;

std::unique_ptr<AudioEngine> AudioEngine::create(QObject* parent)
{
    auto juceEngine = std::make_unique<JuceAudioEngine>(parent);
    if (juceEngine->isReady())
    {
        return juceEngine;
    }

    return std::make_unique<MciAudioEngine>(parent);
}
