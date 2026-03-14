#pragma once

#include <QString>

class PerformanceLogger
{
public:
    PerformanceLogger() = default;

    void startSession(
        const QString& clipPath,
        const QString& decoderName,
        const QString& renderName,
        double fps,
        int totalFrames);

    void logEvent(const QString& category, const QString& message);

private:
    QString logFilePath() const;

    QString m_logFilePath;
};
