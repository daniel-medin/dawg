#include "core/audio/AudioEngine.h"

#include "core/audio/JuceAudioEngine.h"

AudioEngine::AudioEngine(QObject* parent)
    : QObject(parent)
{
}

AudioEngine::~AudioEngine() = default;

std::unique_ptr<AudioEngine> AudioEngine::create(QObject* parent)
{
    return std::make_unique<JuceAudioEngine>(parent);
}
