#include "app/VideoProxyController.h"

#include <algorithm>
#include <optional>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

#include "app/MainWindow.h"
#include "app/PlayerController.h"
#include "app/ShellOverlayController.h"

namespace
{
Qt::CaseSensitivity pathCaseSensitivity()
{
#ifdef Q_OS_WIN
    return Qt::CaseInsensitive;
#else
    return Qt::CaseSensitive;
#endif
}

QString normalizePath(const QString& path)
{
    if (path.isEmpty())
    {
        return {};
    }

    return QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath()));
}

bool pathsMatch(const QString& left, const QString& right)
{
    return QString::compare(normalizePath(left), normalizePath(right), pathCaseSensitivity()) == 0;
}

std::optional<double> timestampSecondsFromProgressValue(const QString& value)
{
    const auto parts = value.trimmed().split(QLatin1Char(':'));
    if (parts.size() != 3)
    {
        return std::nullopt;
    }

    bool hoursOk = false;
    bool minutesOk = false;
    bool secondsOk = false;
    const auto hours = parts[0].toInt(&hoursOk);
    const auto minutes = parts[1].toInt(&minutesOk);
    const auto seconds = parts[2].toDouble(&secondsOk);
    if (!hoursOk || !minutesOk || !secondsOk)
    {
        return std::nullopt;
    }

    return (static_cast<double>(hours) * 3600.0)
        + (static_cast<double>(minutes) * 60.0)
        + seconds;
}
}

VideoProxyController::VideoProxyController(MainWindow& window)
    : QObject(&window)
    , m_window(window)
{
    connect(
        m_window.m_controller,
        &PlayerController::videoLoaded,
        this,
        [this](const QString&, const int, const double)
        {
            syncForCurrentProjectVideo();
        });
    connect(
        m_window.m_controller,
        &PlayerController::playbackStateChanged,
        this,
        [this](const bool playing)
        {
            if (!playing && m_pendingPlaybackSourceRefresh)
            {
                m_pendingPlaybackSourceRefresh = false;
                QString errorMessage;
                if (requestPlaybackSourceRefreshIfNeeded(&errorMessage) == PlaybackRefreshResult::Failed
                    && !errorMessage.isEmpty())
                {
                    m_window.showStatus(QStringLiteral("Failed to switch video source: %1").arg(errorMessage));
                }
            }
        });
}

VideoProxyController::~VideoProxyController()
{
    cancelProxyGeneration();
}

void VideoProxyController::setProxyEnabled(const bool enabled)
{
    m_window.m_controller->setUseProxyVideo(enabled);

    if (!enabled)
    {
        cancelProxyGeneration();
        QString errorMessage;
        const auto refreshResult = requestPlaybackSourceRefreshIfNeeded(&errorMessage);
        if (refreshResult == PlaybackRefreshResult::Failed && !errorMessage.isEmpty())
        {
            m_window.showStatus(QStringLiteral("Failed to switch video source: %1").arg(errorMessage));
            return;
        }

        m_window.showStatus(
            refreshResult == PlaybackRefreshResult::Deferred
                ? QStringLiteral("Original video will be used when playback stops.")
                : QStringLiteral("Using original project video."));
        return;
    }

    if (m_window.m_controller->projectVideoPath().isEmpty())
    {
        m_window.showStatus(QStringLiteral("Proxy video enabled. Import a video to build an MXF proxy."));
        return;
    }

    const auto existingProxyPath = m_window.m_controller->proxyVideoPath();
    const auto hadProxy = !existingProxyPath.isEmpty() && QFileInfo::exists(existingProxyPath);
    syncForCurrentProjectVideo();
    if (hadProxy)
    {
        m_window.showStatus(QStringLiteral("Using proxy video."));
    }
}

void VideoProxyController::syncForCurrentProjectVideo()
{
    const auto sourcePath = m_window.m_controller->projectVideoPath();
    if (sourcePath.isEmpty())
    {
        cancelProxyGeneration();
        m_pendingPlaybackSourceRefresh = false;
        return;
    }

    const auto proxyPath = desiredProxyPath();
    if (!m_window.m_controller->useProxyVideo())
    {
        cancelProxyGeneration();
        return;
    }

    if (!proxyPath.isEmpty() && QFileInfo::exists(proxyPath))
    {
        if (!pathsMatch(m_window.m_controller->proxyVideoPath(), proxyPath))
        {
            m_window.m_controller->setProxyVideoPath(proxyPath);
            if (!m_window.m_projectStateChangeInProgress && m_window.hasOpenProject())
            {
                m_window.setProjectDirty(true);
            }
        }

        QString errorMessage;
        const auto refreshResult = requestPlaybackSourceRefreshIfNeeded(&errorMessage);
        if (refreshResult == PlaybackRefreshResult::Failed && !errorMessage.isEmpty())
        {
            m_window.showStatus(QStringLiteral("Failed to switch video source: %1").arg(errorMessage));
        }
        if (m_process)
        {
            cancelProxyGeneration();
        }
        return;
    }

    if (proxyPath.isEmpty())
    {
        return;
    }

    if (m_process
        && pathsMatch(m_processSourcePath, sourcePath)
        && pathsMatch(m_outputProxyPath, proxyPath))
    {
        return;
    }

    startProxyGeneration(sourcePath, proxyPath);
}

void VideoProxyController::cancelProxyGeneration()
{
    m_pendingPlaybackSourceRefresh = false;
    if (!m_process)
    {
        if (m_window.m_shellOverlayController)
        {
            m_window.m_shellOverlayController->hideVideoProxyProgress();
        }
        return;
    }

    m_cancellingProcess = true;
    m_process->kill();
    m_process->waitForFinished(3000);
    clearGenerationState();
}

void VideoProxyController::startProxyGeneration(const QString& sourcePath, const QString& outputPath)
{
    cancelProxyGeneration();

    const auto ffmpegPath = ffmpegExecutablePath();
    if (ffmpegPath.isEmpty())
    {
        m_window.showStatus(QStringLiteral("Proxy video needs ffmpeg.exe on PATH."));
        return;
    }

    QFileInfo outputInfo(outputPath);
    if (!outputInfo.dir().mkpath(QStringLiteral(".")))
    {
        m_window.showStatus(QStringLiteral("Failed to create the project video folder for the proxy."));
        return;
    }

    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    m_stdoutBuffer.clear();
    m_lastErrorLine.clear();
    m_processSourcePath = sourcePath;
    m_outputProxyPath = outputPath;
    m_tempOutputProxyPath = tempProxyPath(outputPath);
    m_sourceDurationSeconds = (m_window.m_controller->fps() > 0.0)
        ? (static_cast<double>(m_window.m_controller->totalFrames()) / m_window.m_controller->fps())
        : 0.0;
    m_cancellingProcess = false;

    QFile::remove(m_tempOutputProxyPath);

    connect(m_process, &QProcess::readyReadStandardOutput, this, &VideoProxyController::handleProcessStdOut);
    connect(m_process, &QProcess::readyReadStandardError, this, &VideoProxyController::handleProcessStdErr);
    connect(
        m_process,
        qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
        this,
        &VideoProxyController::handleProcessFinished);
    connect(m_process, &QProcess::errorOccurred, this, &VideoProxyController::handleProcessError);

    const QStringList arguments{
        QStringLiteral("-y"),
        QStringLiteral("-hide_banner"),
        QStringLiteral("-loglevel"),
        QStringLiteral("error"),
        QStringLiteral("-progress"),
        QStringLiteral("pipe:1"),
        QStringLiteral("-nostats"),
        QStringLiteral("-i"),
        sourcePath,
        QStringLiteral("-map"),
        QStringLiteral("0:v:0"),
        QStringLiteral("-map"),
        QStringLiteral("0:a?"),
        QStringLiteral("-sn"),
        QStringLiteral("-dn"),
        QStringLiteral("-vf"),
        QStringLiteral("scale=trunc(iw/2)*2:trunc(ih/2)*2"),
        QStringLiteral("-c:v"),
        QStringLiteral("dnxhd"),
        QStringLiteral("-profile:v"),
        QStringLiteral("dnxhr_lb"),
        QStringLiteral("-pix_fmt"),
        QStringLiteral("yuv422p"),
        QStringLiteral("-c:a"),
        QStringLiteral("pcm_s16le"),
        QStringLiteral("-f"),
        QStringLiteral("mxf"),
        m_tempOutputProxyPath
    };

    if (m_window.m_shellOverlayController)
    {
        m_window.m_shellOverlayController->showVideoProxyProgress(0.0);
    }
    m_window.showStatus(QStringLiteral("Generating MXF proxy video..."));
    m_process->start(ffmpegPath, arguments);
}

void VideoProxyController::clearGenerationState()
{
    if (m_process)
    {
        m_process->deleteLater();
        m_process = nullptr;
    }

    if (!m_tempOutputProxyPath.isEmpty())
    {
        QFile::remove(m_tempOutputProxyPath);
    }

    m_stdoutBuffer.clear();
    m_lastErrorLine.clear();
    m_processSourcePath.clear();
    m_outputProxyPath.clear();
    m_tempOutputProxyPath.clear();
    m_sourceDurationSeconds = 0.0;
    m_cancellingProcess = false;
    if (m_window.m_shellOverlayController)
    {
        m_window.m_shellOverlayController->hideVideoProxyProgress();
    }
}

void VideoProxyController::handleProcessStdOut()
{
    if (!m_process)
    {
        return;
    }

    m_stdoutBuffer += m_process->readAllStandardOutput();
    while (true)
    {
        const auto newlineIndex = m_stdoutBuffer.indexOf('\n');
        if (newlineIndex < 0)
        {
            break;
        }

        const auto line = QString::fromUtf8(m_stdoutBuffer.left(newlineIndex)).trimmed();
        m_stdoutBuffer.remove(0, newlineIndex + 1);
        const auto separatorIndex = line.indexOf(QLatin1Char('='));
        if (separatorIndex <= 0)
        {
            continue;
        }

        const auto key = line.left(separatorIndex);
        const auto value = line.mid(separatorIndex + 1);
        if (key == QStringLiteral("out_time"))
        {
            const auto seconds = timestampSecondsFromProgressValue(value);
            if (seconds.has_value() && m_sourceDurationSeconds > 0.0 && m_window.m_shellOverlayController)
            {
                const auto fraction = std::clamp(*seconds / m_sourceDurationSeconds, 0.0, 0.98);
                m_window.m_shellOverlayController->showVideoProxyProgress(fraction);
            }
        }
        else if (key == QStringLiteral("progress")
            && value == QStringLiteral("end")
            && m_window.m_shellOverlayController)
        {
            m_window.m_shellOverlayController->showVideoProxyProgress(1.0);
        }
    }
}

void VideoProxyController::handleProcessStdErr()
{
    if (!m_process)
    {
        return;
    }

    const auto lines = QString::fromUtf8(m_process->readAllStandardError()).split(QLatin1Char('\n'));
    for (const auto& line : lines)
    {
        const auto trimmed = line.trimmed();
        if (!trimmed.isEmpty())
        {
            m_lastErrorLine = trimmed;
        }
    }
}

void VideoProxyController::handleProcessFinished(const int exitCode, const QProcess::ExitStatus exitStatus)
{
    handleProcessStdOut();
    handleProcessStdErr();

    const auto finalProxyPath = m_outputProxyPath;
    const auto tempProxyPathValue = m_tempOutputProxyPath;
    const auto cancelled = m_cancellingProcess;

    if (cancelled)
    {
        clearGenerationState();
        return;
    }

    if (exitStatus != QProcess::NormalExit || exitCode != 0)
    {
        const auto errorMessage = m_lastErrorLine.isEmpty()
            ? QStringLiteral("ffmpeg exited with code %1.").arg(exitCode)
            : m_lastErrorLine;
        clearGenerationState();
        m_window.showStatus(QStringLiteral("Proxy video failed: %1").arg(errorMessage));
        return;
    }

    QFile::remove(finalProxyPath);
    if (!QFile::rename(tempProxyPathValue, finalProxyPath))
    {
        clearGenerationState();
        m_window.showStatus(QStringLiteral("Proxy video finished, but the MXF file could not be finalized."));
        return;
    }

    clearGenerationState();
    m_window.m_controller->setProxyVideoPath(finalProxyPath);
    if (!m_window.m_projectStateChangeInProgress && m_window.hasOpenProject())
    {
        m_window.setProjectDirty(true);
    }

    QString errorMessage;
    const auto refreshResult = requestPlaybackSourceRefreshIfNeeded(&errorMessage);
    if (refreshResult == PlaybackRefreshResult::Failed && !errorMessage.isEmpty())
    {
        m_window.showStatus(QStringLiteral("Proxy video is ready, but switching failed: %1").arg(errorMessage));
        return;
    }

    m_window.showStatus(
        refreshResult == PlaybackRefreshResult::Deferred
            ? QStringLiteral("Proxy video ready. It will be used when playback stops.")
            : QStringLiteral("Proxy video ready."));
}

void VideoProxyController::handleProcessError(const QProcess::ProcessError error)
{
    if (!m_process)
    {
        return;
    }

    if (m_lastErrorLine.isEmpty())
    {
        m_lastErrorLine = m_process->errorString();
    }

    if (error == QProcess::FailedToStart)
    {
        const auto errorMessage = m_lastErrorLine;
        clearGenerationState();
        m_window.showStatus(QStringLiteral("Proxy video failed: %1").arg(errorMessage));
    }
}

VideoProxyController::PlaybackRefreshResult VideoProxyController::requestPlaybackSourceRefreshIfNeeded(
    QString* errorMessage)
{
    if (!m_window.m_controller->hasVideoLoaded())
    {
        return PlaybackRefreshResult::None;
    }

    const auto preferredPath = m_window.m_controller->preferredPlaybackPath();
    if (preferredPath.isEmpty() || pathsMatch(preferredPath, m_window.m_controller->loadedPath()))
    {
        return PlaybackRefreshResult::None;
    }

    if (m_window.m_controller->isPlaying())
    {
        m_pendingPlaybackSourceRefresh = true;
        return PlaybackRefreshResult::Deferred;
    }

    m_window.m_projectStateChangeInProgress = true;
    const auto refreshed = m_window.m_controller->refreshPlaybackSource(errorMessage);
    m_window.m_projectStateChangeInProgress = false;
    if (!refreshed)
    {
        return PlaybackRefreshResult::Failed;
    }

    m_window.refreshTimeline();
    m_window.requestProjectTimelineThumbnailsGeneration();
    return PlaybackRefreshResult::Applied;
}

QString VideoProxyController::desiredProxyPath() const
{
    const auto storedProxyPath = m_window.m_controller->proxyVideoPath();
    if (!storedProxyPath.isEmpty())
    {
        return storedProxyPath;
    }

    const QFileInfo sourceInfo(m_window.m_controller->projectVideoPath());
    if (!sourceInfo.exists())
    {
        return {};
    }

    return sourceInfo.dir().filePath(QStringLiteral("%1.proxy.mxf").arg(sourceInfo.completeBaseName()));
}

QString VideoProxyController::tempProxyPath(const QString& outputPath) const
{
    const QFileInfo outputInfo(outputPath);
    return outputInfo.dir().filePath(
        QStringLiteral("%1.tmp.%2").arg(outputInfo.completeBaseName(), outputInfo.suffix()));
}

QString VideoProxyController::ffmpegExecutablePath() const
{
    return QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
}
