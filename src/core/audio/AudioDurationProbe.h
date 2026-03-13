#pragma once

#include <optional>

#include <QString>

namespace dawg::audio
{
[[nodiscard]] std::optional<int> probeAudioDurationMs(const QString& filePath);
}
