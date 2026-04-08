#include "app/TimelineThumbnailGenerationController.h"

#include <QDir>
#include <QMetaObject>
#include <QPointer>
#include <QThread>

#include "app/MainWindow.h"
#include "app/PlayerController.h"
#include "app/ProjectTimelineThumbnails.h"
#include "app/ShellOverlayController.h"

namespace
{
QString normalizedPath(const QString& path)
{
    return QDir::cleanPath(QDir::fromNativeSeparators(path));
}
}

TimelineThumbnailGenerationController::TimelineThumbnailGenerationController(MainWindow& window)
    : QObject(&window)
    , m_window(window)
{
}

TimelineThumbnailGenerationController::~TimelineThumbnailGenerationController()
{
    shutdown();
}

void TimelineThumbnailGenerationController::requestGeneration()
{
    if (m_window.m_currentProjectRootPath.isEmpty())
    {
        return;
    }

    Request request;
    request.projectRootPath = m_window.m_currentProjectRootPath;
    request.videoPath = m_window.m_controller
        ? (!m_window.m_controller->projectVideoPath().isEmpty()
               ? m_window.m_controller->projectVideoPath()
               : m_window.m_controller->loadedPath())
        : QString{};
    request.totalFrames = m_window.m_controller ? m_window.m_controller->totalFrames() : 0;
    request.fps = m_window.m_controller ? m_window.m_controller->fps() : 0.0;

    m_pendingRequest = request;
    if (m_generationThread)
    {
        return;
    }

    startGeneration(*m_pendingRequest);
    m_pendingRequest.reset();
}

bool TimelineThumbnailGenerationController::isGenerating() const
{
    return m_generationThread != nullptr;
}

void TimelineThumbnailGenerationController::shutdown()
{
    ++m_generationId;
    m_pendingRequest.reset();
    if (m_window.m_shellOverlayController)
    {
        m_window.m_shellOverlayController->hideTopProgress();
    }
}

void TimelineThumbnailGenerationController::startGeneration(const Request& request)
{
    const auto generationId = ++m_generationId;
    if (m_window.m_shellOverlayController)
    {
        m_window.m_shellOverlayController->showTopProgress(0.0);
    }

    QPointer<TimelineThumbnailGenerationController> controller(this);
    auto* thread = QThread::create([controller, generationId, request]()
    {
        QString errorMessage;
        const bool success = dawg::timeline::ensureProjectTimelineThumbnails(
            request.projectRootPath,
            request.videoPath,
            request.totalFrames,
            request.fps,
            [controller, generationId](const double progress)
            {
                if (!controller)
                {
                    return;
                }

                QMetaObject::invokeMethod(
                    controller,
                    [controller, generationId, progress]()
                    {
                        if (!controller
                            || generationId != controller->m_generationId
                            || !controller->m_window.m_shellOverlayController)
                        {
                            return;
                        }

                        controller->m_window.m_shellOverlayController->showTopProgress(progress);
                    },
                    Qt::QueuedConnection);
            },
            &errorMessage);
        if (!controller)
        {
            return;
        }

        QMetaObject::invokeMethod(
            controller,
            [controller, generationId, request, success, errorMessage]()
            {
                if (!controller)
                {
                    return;
                }

                controller->handleGenerationFinished(generationId, request, success, errorMessage);
            },
            Qt::QueuedConnection);
    });

    m_generationThread = thread;
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void TimelineThumbnailGenerationController::handleGenerationFinished(
    const quint64 generationId,
    const Request& request,
    const bool success,
    const QString& errorMessage)
{
    if (generationId != m_generationId)
    {
        return;
    }

    if (m_window.m_shellOverlayController)
    {
        m_window.m_shellOverlayController->hideTopProgress();
    }

    m_generationThread = nullptr;

    const bool requestMatchesCurrentProject =
        normalizedPath(request.projectRootPath) == normalizedPath(m_window.m_currentProjectRootPath)
        && normalizedPath(request.videoPath)
            == normalizedPath(
                m_window.m_controller
                    ? (!m_window.m_controller->projectVideoPath().isEmpty()
                           ? m_window.m_controller->projectVideoPath()
                           : m_window.m_controller->loadedPath())
                    : QString{});

    if (!success)
    {
        if (!errorMessage.isEmpty() && requestMatchesCurrentProject)
        {
            m_window.showStatus(QStringLiteral("Timeline thumbnails unavailable: %1").arg(errorMessage));
        }
    }
    else if (requestMatchesCurrentProject)
    {
        m_window.refreshTimeline();
    }

    if (m_pendingRequest.has_value())
    {
        const auto nextRequest = *m_pendingRequest;
        m_pendingRequest.reset();
        startGeneration(nextRequest);
    }
}
