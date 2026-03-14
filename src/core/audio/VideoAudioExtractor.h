#pragma once

#include <optional>

#include <QString>

namespace dawg::audio
{
[[nodiscard]] std::optional<QString> extractEmbeddedAudioToWave(const QString& videoFilePath);
}
