#include "app/PerformanceLogger.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

namespace
{
QString safeClipName(const QString& clipPath)
{
    const QFileInfo info{clipPath};
    return info.fileName().isEmpty() ? clipPath : info.fileName();
}

QString findRepositoryLogPath(const QString& clipPath)
{
    QFileInfo info{clipPath};
    QDir dir = info.absoluteDir();
    while (dir.exists() && !dir.isRoot())
    {
        if (dir.exists(QStringLiteral(".git")))
        {
            return dir.absoluteFilePath(QStringLiteral(".watch-out.log"));
        }
        if (!dir.cdUp())
        {
            break;
        }
    }

    return {};
}
}

void PerformanceLogger::startSession(
    const QString& clipPath,
    const QString& decoderName,
    const QString& renderName,
    const double fps,
    const int totalFrames)
{
    const auto repositoryLogPath = findRepositoryLogPath(clipPath);
    m_logFilePath = repositoryLogPath.isEmpty()
        ? QDir::current().absoluteFilePath(QStringLiteral(".watch-out.log"))
        : repositoryLogPath;

    logEvent(
        QStringLiteral("session"),
        QStringLiteral("clip=%1 decode=%2 render=%3 fps=%4 totalFrames=%5")
            .arg(safeClipName(clipPath))
            .arg(decoderName)
            .arg(renderName)
            .arg(fps, 0, 'f', 2)
            .arg(totalFrames));
}

void PerformanceLogger::logEvent(const QString& category, const QString& message)
{
    QFile file(logFilePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
    {
        return;
    }

    QTextStream stream(&file);
    stream << QDateTime::currentDateTime().toString(Qt::ISODateWithMs)
           << " [" << category << "] "
           << message
           << '\n';
}

QString PerformanceLogger::logFilePath() const
{
    return m_logFilePath.isEmpty()
        ? QDir::current().absoluteFilePath(QStringLiteral(".watch-out.log"))
        : m_logFilePath;
}
