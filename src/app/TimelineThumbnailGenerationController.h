#pragma once

#include <optional>

#include <QObject>
#include <QString>

class MainWindow;
class QThread;

class TimelineThumbnailGenerationController final : public QObject
{
    Q_OBJECT

public:
    explicit TimelineThumbnailGenerationController(MainWindow& window);
    ~TimelineThumbnailGenerationController() override;

    void requestGeneration();
    [[nodiscard]] bool isGenerating() const;
    void shutdown();

private:
    struct Request
    {
        QString projectRootPath;
        QString videoPath;
        int totalFrames = 0;
        double fps = 0.0;
    };

    void startGeneration(const Request& request);
    void handleGenerationFinished(quint64 generationId, const Request& request, bool success, const QString& errorMessage);

    MainWindow& m_window;
    QThread* m_generationThread = nullptr;
    std::optional<Request> m_pendingRequest;
    quint64 m_generationId = 0;
};
