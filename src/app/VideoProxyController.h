#pragma once

#include <QObject>

#include <QByteArray>
#include <QProcess>
#include <QString>

class MainWindow;

class VideoProxyController final : public QObject
{
    Q_OBJECT

public:
    explicit VideoProxyController(MainWindow& window);
    ~VideoProxyController() override;

    void setProxyEnabled(bool enabled);
    void syncForCurrentProjectVideo();
    void cancelProxyGeneration();

private:
    enum class PlaybackRefreshResult
    {
        None,
        Deferred,
        Applied,
        Failed
    };

    void startProxyGeneration(const QString& sourcePath, const QString& outputPath);
    void clearGenerationState();
    void handleProcessStdOut();
    void handleProcessStdErr();
    void handleProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void handleProcessError(QProcess::ProcessError error);
    [[nodiscard]] PlaybackRefreshResult requestPlaybackSourceRefreshIfNeeded(QString* errorMessage = nullptr);
    [[nodiscard]] QString desiredProxyPath() const;
    [[nodiscard]] QString tempProxyPath(const QString& outputPath) const;
    [[nodiscard]] QString ffmpegExecutablePath() const;

    MainWindow& m_window;
    QProcess* m_process = nullptr;
    QByteArray m_stdoutBuffer;
    QString m_lastErrorLine;
    QString m_processSourcePath;
    QString m_outputProxyPath;
    QString m_tempOutputProxyPath;
    double m_sourceDurationSeconds = 0.0;
    bool m_pendingPlaybackSourceRefresh = false;
    bool m_cancellingProcess = false;
};
