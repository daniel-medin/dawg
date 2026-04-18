#include "app/ShellUiSetupController.h"

#include <algorithm>
#include <cmath>
#include <optional>

#include <QApplication>
#include <QColor>
#include <QDebug>
#include <QIcon>
#include <QImage>
#include <QKeySequence>
#include <QMetaObject>
#include <QPoint>
#include <QQuickImageProvider>
#include <QQuickItem>
#include <QQuickView>
#include <QSGRendererInterface>
#include <QShortcut>
#include <QSignalBlocker>
#include <QUrl>
#include <QQmlContext>

#ifdef Q_OS_WIN
#include <windows.h>
#include <d3d11.h>
#include <d3d11_4.h>
#include <dwmapi.h>
#endif

#include "app/ActionRegistry.h"
#include "app/AudioPoolQuickController.h"
#include "app/ContextMenuController.h"
#include "app/DialogController.h"
#include "app/FilePickerController.h"
#include "app/MainWindow.h"
#include "app/NodeEditorPreviewSession.h"
#include "app/NodeEditorWorkspaceSession.h"
#include "app/PanelLayoutController.h"
#include "app/PlayerController.h"
#include "app/ShellLayoutController.h"
#include "app/ShellOverlayController.h"
#include "app/TransportUiSyncController.h"
#include "app/WindowChromeController.h"
#include "ui/ClipWaveformQuickItem.h"
#include "ui/DebugOverlayWindow.h"
#include "ui/MixQuickController.h"
#include "ui/NativeVideoViewport.h"
#include "ui/NodeEditorQuickController.h"
#include "ui/QuickEngineSupport.h"
#include "ui/ThumbnailStripQuickController.h"
#include "ui/TimelineQuickController.h"
#include "ui/TimelineThumbnailCache.h"
#include "ui/VideoOverlayQuickItem.h"
#include "ui/VideoViewportQuickController.h"
#include "ui/VideoViewportQuickItem.h"
#include <qqml.h>

namespace
{
#ifdef Q_OS_WIN
void enableD3D11MultithreadProtection(ID3D11Device* device)
{
    if (!device)
    {
        return;
    }

    ID3D11DeviceContext* deviceContext = nullptr;
    device->GetImmediateContext(&deviceContext);
    if (!deviceContext)
    {
        return;
    }

    ID3D11Multithread* multithread = nullptr;
    if (SUCCEEDED(deviceContext->QueryInterface(__uuidof(ID3D11Multithread), reinterpret_cast<void**>(&multithread)))
        && multithread)
    {
        multithread->SetMultithreadProtected(TRUE);
        multithread->Release();
    }

    deviceContext->Release();
}

void applyDarkTitleBar(QWindow* window)
{
    if (!window)
    {
        return;
    }

    const auto hwnd = reinterpret_cast<HWND>(window->winId());
    if (!hwnd)
    {
        return;
    }

    const auto library = LoadLibraryW(L"dwmapi.dll");
    if (!library)
    {
        return;
    }

    using DwmSetWindowAttributeFn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
    const auto setWindowAttribute =
        reinterpret_cast<DwmSetWindowAttributeFn>(GetProcAddress(library, "DwmSetWindowAttribute"));
    if (setWindowAttribute)
    {
        constexpr DWORD kUseImmersiveDarkMode = 20;
        const BOOL enabled = TRUE;
        setWindowAttribute(hwnd, kUseImmersiveDarkMode, &enabled, sizeof(enabled));
    }

    FreeLibrary(library);
}
#endif

void clearStuckWaitCursor(QWindow* window)
{
    if (window)
    {
        window->unsetCursor();
    }

    while (qApp && qApp->overrideCursor())
    {
        qApp->restoreOverrideCursor();
    }
}

QUrl mixSceneUrl()
{
    return QUrl(QStringLiteral("qrc:/qml/MixScene.qml"));
}

QUrl timelineSceneUrl()
{
    return QUrl(QStringLiteral("qrc:/qml/TimelineScene.qml"));
}

QUrl audioPoolSceneUrl()
{
    return QUrl(QStringLiteral("qrc:/qml/AudioPoolScene.qml"));
}

QUrl videoViewportSceneUrl()
{
    return QUrl(QStringLiteral("qrc:/qml/VideoViewportScene.qml"));
}

QUrl appShellUrl()
{
    return QUrl(QStringLiteral("qrc:/qml/AppShell.qml"));
}

class TimelineThumbnailProvider final : public QQuickImageProvider
{
public:
    TimelineThumbnailProvider()
        : QQuickImageProvider(QQuickImageProvider::Image)
    {
    }

    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override
    {
        const auto separatorIndex = id.lastIndexOf(QLatin1Char('/'));
        if (separatorIndex <= 0 || separatorIndex >= (id.size() - 1))
        {
            return {};
        }

        bool frameOk = false;
        const auto frameIndex = id.mid(separatorIndex + 1).toInt(&frameOk);
        if (!frameOk)
        {
            return {};
        }

        auto image = timelineThumbnailCache().thumbnail(
            QUrl::fromPercentEncoding(id.left(separatorIndex).toUtf8()),
            frameIndex);
        if (requestedSize.isValid() && !image.isNull())
        {
            image = image.scaled(requestedSize, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        }
        if (size)
        {
            *size = image.size();
        }
        return image;
    }
};

class VideoViewportImageProvider final : public QQuickImageProvider
{
public:
    explicit VideoViewportImageProvider(VideoViewportQuickController& controller)
        : QQuickImageProvider(QQuickImageProvider::Image)
        , m_controller(controller)
    {
    }

    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override
    {
        Q_UNUSED(id);
        Q_UNUSED(requestedSize);

        const auto image = m_controller.currentFrame();
        if (size)
        {
            *size = image.size();
        }
        return image;
    }

private:
    VideoViewportQuickController& m_controller;
};

void ensureQuickTypesRegistered()
{
    static const bool registered = []()
    {
        qmlRegisterType<ClipWaveformQuickItem>("Dawg", 1, 0, "ClipWaveformQuickItem");
        qmlRegisterType<VideoOverlayQuickItem>("Dawg", 1, 0, "VideoOverlayQuickItem");
        qmlRegisterType<VideoViewportQuickItem>("Dawg", 1, 0, "VideoViewportQuickItem");
        return true;
    }();
    Q_UNUSED(registered);
}

float mixGainSliderValueToDb(const int sliderValue)
{
    constexpr int kMixGainPopupMinValue = -1000;
    return sliderValue <= kMixGainPopupMinValue ? -100.0F : static_cast<float>(sliderValue) / 10.0F;
}
}

ShellUiSetupController::ShellUiSetupController(MainWindow& window)
    : m_window(window)
{
}

void ShellUiSetupController::buildUi()
{
    configureWindow();
    createShortcuts();
    createControllers();
    configureNodeEditorSessions();
    connectNodeEditorSignals();
    configureQuickShell();
    bindShellItems();
    connectShellLayoutSignals();
    createDetachedWindows();
    initializeShellLayoutDefaults();
    connectOverlayControllers();
    createDebugOverlay();
    connectSceneGraphInitialization();
    finalizeBuild();
}

void ShellUiSetupController::configureWindow() const
{
    m_window.setWindowTitle(QStringLiteral("dawg"));
    m_window.resize(1400, 900);
    m_window.setMinimumSize(QSize(1180, 760));
    m_window.setFlags(m_window.flags() | Qt::FramelessWindowHint);
    m_window.setColor(QColor(QStringLiteral("#0a0c10")));
    m_window.setResizeMode(QQuickView::SizeRootObjectToView);
    m_window.setIcon(QIcon(QStringLiteral(":/branding/dawg.png")));

    m_window.m_actionRegistry = new ActionRegistry(m_window, &m_window);
    if (m_window.m_mixSoloModeAction)
    {
        const QSignalBlocker blocker(m_window.m_mixSoloModeAction);
        m_window.m_mixSoloModeAction->setChecked(m_window.m_controller->isMixSoloXorMode());
    }
    m_window.m_windowChromeController = new WindowChromeController(m_window, &m_window);
}

void ShellUiSetupController::createShortcuts() const
{
    m_window.m_playPauseShortcut = new QShortcut(QKeySequence(Qt::Key_Space), &m_window);
    m_window.m_startShortcut = new QShortcut(QKeySequence(Qt::Key_Return), &m_window);
    m_window.m_numpadStartShortcut = new QShortcut(QKeySequence(Qt::Key_Enter), &m_window);
    m_window.m_stepBackShortcut = new QShortcut(QKeySequence(Qt::Key_Comma), &m_window);
    m_window.m_stepForwardShortcut = new QShortcut(QKeySequence(Qt::Key_Period), &m_window);
    m_window.m_stepFastForwardShortcut = new QShortcut(QKeySequence(Qt::Key_Minus), &m_window);
    m_window.m_stepFastBackShortcut = new QShortcut(QKeySequence(Qt::Key_M), &m_window);
    m_window.m_insertionFollowsPlaybackShortcut = new QShortcut(QKeySequence(Qt::Key_N), &m_window);
    m_window.m_copyShortcut = new QShortcut(QKeySequence::Copy, &m_window);
    m_window.m_pasteShortcut = new QShortcut(QKeySequence::Paste, &m_window);
    m_window.m_cutShortcut = new QShortcut(QKeySequence::Cut, &m_window);
    m_window.m_undoShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Z), &m_window);
    m_window.m_redoShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Y), &m_window);
    m_window.m_selectAllShortcut = new QShortcut(QKeySequence::SelectAll, &m_window);
    m_window.m_nodeStartShortcut = new QShortcut(QKeySequence(Qt::Key_A), &m_window);
    m_window.m_nodeEndShortcut = new QShortcut(QKeySequence(Qt::Key_S), &m_window);
    m_window.m_selectNextNodeShortcut = new QShortcut(QKeySequence(Qt::Key_Tab), &m_window);
    m_window.m_showTimelineShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+T")), &m_window);
    m_window.m_showMixShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl++")), &m_window);
    m_window.m_trimNodeShortcut = new QShortcut(QKeySequence(Qt::SHIFT | Qt::Key_T), &m_window);
    m_window.m_autoPanShortcut = new QShortcut(QKeySequence(Qt::Key_R), &m_window);
    m_window.m_audioPoolShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+P")), &m_window);
    m_window.m_toggleNodeNameShortcut = new QShortcut(QKeySequence(Qt::Key_E), &m_window);
    m_window.m_deleteShortcut = new QShortcut(QKeySequence(Qt::Key_Backspace), &m_window);
    m_window.m_unselectAllShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), &m_window);

    const auto setApplicationShortcutContext = [](QShortcut* shortcut)
    {
        if (shortcut)
        {
            shortcut->setContext(Qt::ApplicationShortcut);
        }
    };

    setApplicationShortcutContext(m_window.m_playPauseShortcut);
    setApplicationShortcutContext(m_window.m_startShortcut);
    setApplicationShortcutContext(m_window.m_numpadStartShortcut);
    setApplicationShortcutContext(m_window.m_stepBackShortcut);
    setApplicationShortcutContext(m_window.m_stepForwardShortcut);
    setApplicationShortcutContext(m_window.m_stepFastForwardShortcut);
    setApplicationShortcutContext(m_window.m_stepFastBackShortcut);
    setApplicationShortcutContext(m_window.m_insertionFollowsPlaybackShortcut);
    setApplicationShortcutContext(m_window.m_copyShortcut);
    setApplicationShortcutContext(m_window.m_pasteShortcut);
    setApplicationShortcutContext(m_window.m_cutShortcut);
    setApplicationShortcutContext(m_window.m_undoShortcut);
    setApplicationShortcutContext(m_window.m_redoShortcut);
    setApplicationShortcutContext(m_window.m_selectAllShortcut);
    setApplicationShortcutContext(m_window.m_nodeStartShortcut);
    setApplicationShortcutContext(m_window.m_nodeEndShortcut);
    setApplicationShortcutContext(m_window.m_selectNextNodeShortcut);
    setApplicationShortcutContext(m_window.m_showTimelineShortcut);
    setApplicationShortcutContext(m_window.m_showMixShortcut);
    setApplicationShortcutContext(m_window.m_trimNodeShortcut);
    setApplicationShortcutContext(m_window.m_autoPanShortcut);
    setApplicationShortcutContext(m_window.m_audioPoolShortcut);
    setApplicationShortcutContext(m_window.m_toggleNodeNameShortcut);
    setApplicationShortcutContext(m_window.m_deleteShortcut);
    setApplicationShortcutContext(m_window.m_unselectAllShortcut);

    new QShortcut(QKeySequence::New, &m_window, [&window = m_window]()
    {
        if (window.m_newProjectAction)
        {
            window.m_newProjectAction->trigger();
        }
    }, Qt::ApplicationShortcut);
    new QShortcut(QKeySequence::Open, &m_window, [&window = m_window]()
    {
        if (window.m_openProjectAction)
        {
            window.m_openProjectAction->trigger();
        }
    }, Qt::ApplicationShortcut);
    new QShortcut(QKeySequence::Save, &m_window, [&window = m_window]()
    {
        if (window.m_saveProjectAction)
        {
            window.m_saveProjectAction->trigger();
        }
    }, Qt::ApplicationShortcut);
    new QShortcut(QKeySequence::SaveAs, &m_window, [&window = m_window]()
    {
        if (window.m_saveProjectAsAction)
        {
            window.m_saveProjectAsAction->trigger();
        }
    }, Qt::ApplicationShortcut);
    new QShortcut(QKeySequence(QStringLiteral("Ctrl+Q")), &m_window, [&window = m_window]()
    {
        if (window.m_quitAction)
        {
            window.m_quitAction->trigger();
        }
    }, Qt::ApplicationShortcut);
    new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_I), &m_window, [&window = m_window]()
    {
        if (window.m_importSoundAction && window.m_importSoundAction->isEnabled())
        {
            window.m_importSoundAction->trigger();
        }
    }, Qt::ApplicationShortcut);
}

void ShellUiSetupController::createControllers()
{
    m_window.m_shellLayoutController = new ShellLayoutController(&m_window);
    m_window.m_videoViewportQuickController = new VideoViewportQuickController(&m_window);
    m_window.m_detachedVideoViewportQuickController = new VideoViewportQuickController(&m_window);
    m_window.m_detachedVideoViewportQuickController->setNativePresentationEnabled(false);
    m_window.m_timelineQuickController = new TimelineQuickController(&m_window);
    m_window.m_timelineQuickController->setThumbnailsVisible(false);
    m_window.m_thumbnailStripQuickController = new ThumbnailStripQuickController(&m_window);
    ensureQuickTypesRegistered();
    m_window.m_nodeEditorQuickController = new NodeEditorQuickController(&m_window);
    m_window.m_mixQuickController = new MixQuickController(&m_window);
    m_window.m_audioPoolQuickController = new AudioPoolQuickController(m_window, &m_window);
    m_window.m_audioPoolQuickController->setShowLength(m_window.m_audioPoolShowLength);
    m_window.m_audioPoolQuickController->setShowSize(m_window.m_audioPoolShowSize);
    m_window.m_contextMenuController = new ContextMenuController(&m_window);
    m_window.m_dialogController = new DialogController(&m_window);
    m_window.m_filePickerController = new FilePickerController(&m_window);
    m_window.m_shellOverlayController = new ShellOverlayController(&m_window);
}

void ShellUiSetupController::configureNodeEditorSessions() const
{
    m_window.m_transportUiSyncController = std::make_unique<TransportUiSyncController>(
        *m_window.m_controller,
        *m_window.m_nodeEditorQuickController,
        *m_window.m_timelineQuickController,
        *m_window.m_thumbnailStripQuickController,
        [&window = m_window]()
        {
            return window.m_timelineQuickWidget && window.m_timelineQuickWidget->isVisible();
        });
    m_window.m_nodeEditorPreviewSession = std::make_unique<NodeEditorPreviewSession>(
        *m_window.m_controller,
        *m_window.m_nodeEditorQuickController,
        &m_window);
    m_window.m_nodeEditorPreviewSession->setTransportUiSyncController(m_window.m_transportUiSyncController.get());
    m_window.m_nodeEditorPreviewSession->setStatusCallback([&window = m_window](const QString& message)
    {
        window.showStatus(message);
    });

    m_window.m_nodeEditorWorkspaceSession = std::make_unique<NodeEditorWorkspaceSession>(
        *m_window.m_controller,
        *m_window.m_nodeEditorQuickController,
        *m_window.m_nodeEditorPreviewSession);
    m_window.m_nodeEditorWorkspaceSession->setStatusCallback([&window = m_window](const QString& message)
    {
        window.showStatus(message);
    });
    m_window.m_nodeEditorWorkspaceSession->setProjectDirtyCallback([&window = m_window]()
    {
        if (!window.m_projectStateChangeInProgress && window.hasOpenProject())
        {
            window.setProjectDirty(true);
        }
    });
    m_window.m_nodeEditorWorkspaceSession->setRefreshAudioPoolCallback([&window = m_window]()
    {
        window.refreshAudioPool();
    });
    m_window.m_nodeEditorWorkspaceSession->setRefreshMixCallback([&window = m_window]()
    {
        window.refreshMixView();
    });
    m_window.m_nodeEditorWorkspaceSession->setChooseOpenFileCallback(
        [&window = m_window](const QString& title, const QString& directory, const QString& filter)
        {
            return window.chooseOpenFileName(title, directory, filter);
        });
    m_window.m_nodeEditorWorkspaceSession->setChooseSaveFileCallback(
        [&window = m_window](const QString& title, const QString& directory, const QString& fileName, const QString& filter)
        {
            return window.m_filePickerController
                ? window.m_filePickerController->execSaveFile(title, directory, fileName, filter)
                : QString{};
        });
    m_window.m_nodeEditorWorkspaceSession->setCopyMediaIntoProjectCallback(
        [&window = m_window](const QString& sourcePath, const QString& subdirectory, QString* errorMessage)
        {
            return window.copyMediaIntoProject(sourcePath, subdirectory, errorMessage);
        });
    m_window.m_nodeEditorWorkspaceSession->setEnsureProjectForMediaActionCallback(
        [&window = m_window](const QString& actionLabel)
        {
            return window.ensureProjectForMediaAction(actionLabel);
        });
    m_window.m_nodeEditorPreviewSession->setMixViewRefreshCallback([&window = m_window]()
    {
        window.refreshMixView();
    });
    m_window.m_nodeEditorPreviewSession->setMixMeterRefreshCallback([&window = m_window]()
    {
        window.updateMixMeterLevels();
    });
    m_window.m_nodeEditorWorkspaceSession->setDialogController(m_window.m_dialogController);
    m_window.m_nodeEditorWorkspaceSession->setFilePickerController(m_window.m_filePickerController);
}

void ShellUiSetupController::connectNodeEditorSignals() const
{
    QObject::connect(
        m_window.m_nodeEditorQuickController,
        &NodeEditorQuickController::fileActionRequested,
        &m_window,
        [&window = m_window](const QString& actionKey)
        {
            window.requestNodeEditorFileAction(actionKey);
        });
    QObject::connect(
        m_window.m_nodeEditorQuickController,
        &NodeEditorQuickController::audioActionRequested,
        &m_window,
        [&window = m_window](const QString& actionKey)
        {
            window.requestNodeEditorAudioAction(actionKey);
        });
    QObject::connect(
        m_window.m_nodeEditorQuickController,
        &NodeEditorQuickController::editActionRequested,
        &m_window,
        [&window = m_window](const QString& actionKey)
        {
            window.requestNodeEditorEditAction(actionKey);
        });
    QObject::connect(
        m_window.m_nodeEditorQuickController,
        &NodeEditorQuickController::laneMuteRequested,
        &m_window,
        [&window = m_window](const QString& laneId, const bool muted)
        {
            if (!window.m_nodeEditorWorkspaceSession)
            {
                return;
            }
            window.m_nodeEditorWorkspaceSession->setLaneMuted(laneId, muted, window.hasOpenProject());
            window.syncNodeEditorActionAvailability();
        });
    QObject::connect(
        m_window.m_nodeEditorQuickController,
        &NodeEditorQuickController::laneSoloRequested,
        &m_window,
        [&window = m_window](const QString& laneId, const bool soloed)
        {
            if (!window.m_nodeEditorWorkspaceSession)
            {
                return;
            }
            window.m_nodeEditorWorkspaceSession->setLaneSoloed(laneId, soloed, window.hasOpenProject());
            window.syncNodeEditorActionAvailability();
        });
    QObject::connect(
        m_window.m_nodeEditorQuickController,
        &NodeEditorQuickController::clipMoveRequested,
        &m_window,
        [&window = m_window](const QString& laneId, const QString& clipId, const int laneOffsetMs)
        {
            if (!window.m_nodeEditorWorkspaceSession)
            {
                return;
            }
            window.m_nodeEditorWorkspaceSession->moveClip(laneId, clipId, laneOffsetMs, window.hasOpenProject());
            window.syncNodeEditorActionAvailability();
        });
    QObject::connect(
        m_window.m_nodeEditorQuickController,
        &NodeEditorQuickController::clipCopyRequested,
        &m_window,
        [&window = m_window](const QString& laneId, const QString& clipId, const int laneOffsetMs)
        {
            if (!window.m_nodeEditorWorkspaceSession)
            {
                return;
            }
            window.m_nodeEditorWorkspaceSession->copyClipAtOffset(laneId, clipId, laneOffsetMs, window.hasOpenProject());
            window.syncNodeEditorActionAvailability();
        });
    QObject::connect(
        m_window.m_nodeEditorQuickController,
        &NodeEditorQuickController::clipDropRequested,
        &m_window,
        [&window = m_window](
            const QString& sourceLaneId,
            const QString& clipId,
            const QString& targetLaneId,
            const int laneOffsetMs,
            const bool copyClip)
        {
            if (!window.m_nodeEditorWorkspaceSession)
            {
                return;
            }
            window.m_nodeEditorWorkspaceSession->dropClip(
                sourceLaneId,
                clipId,
                targetLaneId,
                laneOffsetMs,
                copyClip,
                window.hasOpenProject());
            window.syncNodeEditorActionAvailability();
        });
    QObject::connect(
        m_window.m_nodeEditorQuickController,
        &NodeEditorQuickController::clipTrimRequested,
        &m_window,
        [&window = m_window](const QString& laneId, const QString& clipId, const int targetMs, const bool trimStart)
        {
            if (!window.m_nodeEditorWorkspaceSession)
            {
                return;
            }
            window.m_nodeEditorWorkspaceSession->trimClip(laneId, clipId, targetMs, trimStart, window.hasOpenProject());
            window.syncNodeEditorActionAvailability();
        });
    QObject::connect(
        m_window.m_nodeEditorQuickController,
        &NodeEditorQuickController::playheadChanged,
        &m_window,
        [&window = m_window](const int playheadMs)
        {
            if (!window.m_controller->isPlaying()
                && (!window.m_nodeEditorPreviewSession
                    || !window.m_nodeEditorPreviewSession->isUpdatingPlayhead()))
            {
                window.setPreferredPlaybackContext(MainWindow::PlaybackContext::NodeEditor);
            }
            if (window.m_nodeEditorPreviewSession)
            {
                window.m_nodeEditorPreviewSession->handlePlayheadChanged(playheadMs);
            }
        });
}

void ShellUiSetupController::configureQuickShell() const
{
    m_window.m_shellLayoutController->setPreferredSizes(
        m_window.m_audioPoolPreferredWidth,
        m_window.m_timelinePreferredHeight,
        m_window.m_nodeEditorPreferredHeight,
        m_window.m_mixPreferredHeight);

    configureQuickEngine(*m_window.engine());
    m_window.engine()->addImageProvider(
        QStringLiteral("videoViewport"),
        new VideoViewportImageProvider(*m_window.m_videoViewportQuickController));
    m_window.engine()->addImageProvider(
        QStringLiteral("timeline-thumbnail"),
        new TimelineThumbnailProvider());
    m_window.rootContext()->setContextProperty(QStringLiteral("actionRegistry"), m_window.m_actionRegistry);
    m_window.rootContext()->setContextProperty(QStringLiteral("windowChrome"), m_window.m_windowChromeController);
    m_window.rootContext()->setContextProperty(QStringLiteral("shellLayoutController"), m_window.m_shellLayoutController);
    m_window.rootContext()->setContextProperty(QStringLiteral("videoViewportController"), m_window.m_videoViewportQuickController);
    m_window.rootContext()->setContextProperty(QStringLiteral("videoViewportBridge"), &m_window);
    m_window.rootContext()->setContextProperty(QStringLiteral("videoViewportAllowNativePresentation"), false);
    m_window.rootContext()->setContextProperty(QStringLiteral("timelineController"), m_window.m_timelineQuickController);
    m_window.rootContext()->setContextProperty(QStringLiteral("thumbnailStripController"), m_window.m_thumbnailStripQuickController);
    m_window.rootContext()->setContextProperty(QStringLiteral("nodeEditorController"), m_window.m_nodeEditorQuickController);
    m_window.rootContext()->setContextProperty(QStringLiteral("mainWindowBridge"), &m_window);
    m_window.rootContext()->setContextProperty(QStringLiteral("mixController"), m_window.m_mixQuickController);
    m_window.rootContext()->setContextProperty(QStringLiteral("audioPoolController"), m_window.m_audioPoolQuickController);
    m_window.rootContext()->setContextProperty(QStringLiteral("contextMenuController"), m_window.m_contextMenuController);
    m_window.rootContext()->setContextProperty(QStringLiteral("dialogController"), m_window.m_dialogController);
    m_window.rootContext()->setContextProperty(QStringLiteral("filePickerController"), m_window.m_filePickerController);
    m_window.rootContext()->setContextProperty(QStringLiteral("shellOverlay"), m_window.m_shellOverlayController);
    m_window.setSource(appShellUrl());
    if (m_window.status() == QQuickView::Error)
    {
        for (const auto& error : m_window.errors())
        {
            qWarning().noquote() << "Quick shell error:" << error.toString();
        }
    }
}

void ShellUiSetupController::bindShellItems() const
{
    m_window.m_shellRootItem = qobject_cast<QQuickItem*>(m_window.rootObject());
    if (m_window.m_nodeEditorWorkspaceSession)
    {
        m_window.m_nodeEditorWorkspaceSession->bindShellRootItem(m_window.m_shellRootItem);
    }
    m_window.m_titleBarItem =
        m_window.m_shellRootItem ? m_window.m_shellRootItem->findChild<QQuickItem*>(QStringLiteral("quickTitleBar")) : nullptr;
    m_window.m_contentAreaItem =
        m_window.m_shellRootItem ? m_window.m_shellRootItem->findChild<QQuickItem*>(QStringLiteral("shellContentArea")) : nullptr;
    m_window.m_videoViewportQuickWidget =
        m_window.m_shellRootItem ? m_window.m_shellRootItem->findChild<QQuickItem*>(QStringLiteral("videoViewportScene")) : nullptr;
    m_window.m_timelineQuickWidget =
        m_window.m_shellRootItem ? m_window.m_shellRootItem->findChild<QQuickItem*>(QStringLiteral("timelineScene")) : nullptr;
    m_window.m_thumbnailStripQuickWidget =
        m_window.m_shellRootItem ? m_window.m_shellRootItem->findChild<QQuickItem*>(QStringLiteral("thumbnailStripScene")) : nullptr;
    m_window.m_nodeEditorQuickWidget =
        m_window.m_shellRootItem ? m_window.m_shellRootItem->findChild<QQuickItem*>(QStringLiteral("nodeEditorScene")) : nullptr;
    m_window.m_mixQuickWidget =
        m_window.m_shellRootItem ? m_window.m_shellRootItem->findChild<QQuickItem*>(QStringLiteral("mixScene")) : nullptr;
    m_window.m_audioPoolQuickWidget =
        m_window.m_shellRootItem ? m_window.m_shellRootItem->findChild<QQuickItem*>(QStringLiteral("audioPoolScene")) : nullptr;
    m_window.m_contextMenuQuickWidget =
        m_window.m_shellRootItem ? m_window.m_shellRootItem->findChild<QQuickItem*>(QStringLiteral("contextMenuOverlay")) : nullptr;
    m_window.m_dialogOverlayQuickWidget =
        m_window.m_shellRootItem ? m_window.m_shellRootItem->findChild<QQuickItem*>(QStringLiteral("dialogOverlay")) : nullptr;
    m_window.m_filePickerQuickWidget =
        m_window.m_shellRootItem ? m_window.m_shellRootItem->findChild<QQuickItem*>(QStringLiteral("filePickerOverlay")) : nullptr;
    m_window.m_shellOverlayQuickWidget =
        m_window.m_shellRootItem ? m_window.m_shellRootItem->findChild<QQuickItem*>(QStringLiteral("shellOverlayScene")) : nullptr;

    const auto installWindowEventFilter = [&window = m_window](QQuickItem* item)
    {
        if (item)
        {
            item->installEventFilter(&window);
        }
    };
    installWindowEventFilter(m_window.m_videoViewportQuickWidget);
    installWindowEventFilter(m_window.m_timelineQuickWidget);
    installWindowEventFilter(m_window.m_thumbnailStripQuickWidget);
    installWindowEventFilter(m_window.m_nodeEditorQuickWidget);

    if (m_window.m_nodeEditorQuickWidget)
    {
        QObject::connect(
            m_window.m_nodeEditorQuickWidget,
            &QQuickItem::implicitHeightChanged,
            &m_window,
            [&window = m_window]()
            {
                if (window.m_panelLayoutController)
                {
                    window.m_panelLayoutController->fitNodeEditorToContent();
                }
            });
    }

    if (m_window.m_titleBarItem)
    {
        m_window.m_windowChromeController->setTitleBarHeight(static_cast<int>(std::lround(m_window.m_titleBarItem->height())));
    }
}

void ShellUiSetupController::connectShellLayoutSignals() const
{
    QObject::connect(
        m_window.m_shellLayoutController,
        &ShellLayoutController::layoutChanged,
        &m_window,
        &MainWindow::syncShellPanelGeometry);
    QObject::connect(
        m_window.m_shellLayoutController,
        &ShellLayoutController::preferredSizesChanged,
        &m_window,
        [&window = m_window](const int audioPoolWidth, const int timelineHeight, const int nodeEditorHeight, const int mixHeight)
        {
            window.m_audioPoolPreferredWidth = std::max(240, audioPoolWidth);
            window.m_timelinePreferredHeight = std::max(window.timelineMinimumHeight(), timelineHeight);
            window.m_nodeEditorPreferredHeight = std::max(148, nodeEditorHeight);
            window.m_mixPreferredHeight = std::max(132, mixHeight);
            if (!window.m_projectStateChangeInProgress && window.hasOpenProject())
            {
                window.setProjectDirty(true);
            }
        });
}

void ShellUiSetupController::createDetachedWindows() const
{
    m_window.m_nativeViewport = new NativeVideoViewport(nullptr);
    m_window.m_nativeViewport->setWindowTitle(QStringLiteral("Native Video Viewport Test"));
    m_window.m_nativeViewport->resize(960, 540);
    m_window.m_nativeViewport->hide();
    m_window.m_nativeViewport->installEventFilter(&m_window);
    m_window.m_nativeViewport->setRenderService(nullptr);
    m_window.m_nativeViewportWindow = m_window.m_nativeViewport;
    QObject::connect(&m_window, &QObject::destroyed, m_window.m_nativeViewportWindow, &QObject::deleteLater);

    m_window.m_detachedVideoWindow = new QQuickView();
    m_window.m_detachedVideoWindow->setTitle(QStringLiteral("Detached Video"));
    m_window.m_detachedVideoWindow->setIcon(m_window.icon());
    m_window.m_detachedVideoWindow->setColor(QColor(QStringLiteral("#0c1016")));
    m_window.m_detachedVideoWindow->setFlags(
        Qt::Window
        | Qt::WindowStaysOnTopHint
        | Qt::WindowTitleHint
        | Qt::WindowSystemMenuHint
        | Qt::WindowMinimizeButtonHint
        | Qt::WindowMaximizeButtonHint
        | Qt::WindowCloseButtonHint);
    m_window.m_detachedVideoWindow->setResizeMode(QQuickView::SizeRootObjectToView);
    configureQuickEngine(*m_window.m_detachedVideoWindow->engine());
    m_window.m_detachedVideoWindow->engine()->addImageProvider(
        QStringLiteral("videoViewport"),
        new VideoViewportImageProvider(*m_window.m_detachedVideoViewportQuickController));
    m_window.m_detachedVideoWindow->rootContext()->setContextProperty(
        QStringLiteral("videoViewportController"),
        m_window.m_detachedVideoViewportQuickController);
    m_window.m_detachedVideoWindow->rootContext()->setContextProperty(
        QStringLiteral("videoViewportBridge"),
        &m_window);
    m_window.m_detachedVideoWindow->rootContext()->setContextProperty(
        QStringLiteral("videoViewportAllowNativePresentation"),
        false);
    m_window.m_detachedVideoWindow->setSource(videoViewportSceneUrl());
#ifdef Q_OS_WIN
    applyDarkTitleBar(m_window.m_detachedVideoWindow);
#endif
    m_window.m_detachedVideoWindow->hide();
    m_window.m_detachedVideoWindow->installEventFilter(&m_window);

    const auto createDetachedPanelWindow = [&window = m_window](const QString& title, const QColor& color) -> QQuickView*
    {
        auto* panelWindow = new QQuickView();
        panelWindow->setTitle(title);
        panelWindow->setIcon(window.icon());
        panelWindow->setColor(color);
        panelWindow->setFlags(
            Qt::Window
            | Qt::WindowTitleHint
            | Qt::WindowSystemMenuHint
            | Qt::WindowMinimizeButtonHint
            | Qt::WindowMaximizeButtonHint
            | Qt::WindowCloseButtonHint);
        panelWindow->setResizeMode(QQuickView::SizeRootObjectToView);
        configureQuickEngine(*panelWindow->engine());
        return panelWindow;
    };

    m_window.m_detachedTimelineWindow = createDetachedPanelWindow(
        QStringLiteral("Detached Timeline"),
        QColor(QStringLiteral("#050608")));
    m_window.m_detachedTimelineWindow->rootContext()->setContextProperty(QStringLiteral("timelineController"), m_window.m_timelineQuickController);
    m_window.m_detachedTimelineWindow->rootContext()->setContextProperty(QStringLiteral("videoViewportBridge"), &m_window);
    m_window.m_detachedTimelineWindow->setSource(timelineSceneUrl());
#ifdef Q_OS_WIN
    applyDarkTitleBar(m_window.m_detachedTimelineWindow);
#endif
    m_window.m_detachedTimelineWindow->hide();
    m_window.m_detachedTimelineWindow->installEventFilter(&m_window);

    m_window.m_detachedMixWindow = createDetachedPanelWindow(
        QStringLiteral("Detached Mixer"),
        QColor(QStringLiteral("#080b10")));
    m_window.m_detachedMixWindow->rootContext()->setContextProperty(QStringLiteral("mixController"), m_window.m_mixQuickController);
    m_window.m_detachedMixWindow->setSource(mixSceneUrl());
#ifdef Q_OS_WIN
    applyDarkTitleBar(m_window.m_detachedMixWindow);
#endif
    m_window.m_detachedMixWindow->hide();
    m_window.m_detachedMixWindow->installEventFilter(&m_window);

    m_window.m_detachedAudioPoolWindow = createDetachedPanelWindow(
        QStringLiteral("Detached Audio Pool"),
        QColor(QStringLiteral("#07090c")));
    m_window.m_detachedAudioPoolWindow->rootContext()->setContextProperty(
        QStringLiteral("audioPoolController"),
        m_window.m_audioPoolQuickController);
    m_window.m_detachedAudioPoolWindow->rootContext()->setContextProperty(QStringLiteral("windowChrome"), m_window.m_windowChromeController);
    m_window.m_detachedAudioPoolWindow->setSource(audioPoolSceneUrl());
#ifdef Q_OS_WIN
    applyDarkTitleBar(m_window.m_detachedAudioPoolWindow);
#endif
    m_window.m_detachedAudioPoolWindow->hide();
    m_window.m_detachedAudioPoolWindow->installEventFilter(&m_window);
}

void ShellUiSetupController::initializeShellLayoutDefaults() const
{
    if (!m_window.m_shellLayoutController)
    {
        return;
    }

    m_window.m_shellLayoutController->setTimelineMinimumHeight(m_window.timelineMinimumHeight());
    m_window.m_shellLayoutController->setThumbnailsVisible(true);
    m_window.m_shellLayoutController->setTimelineVisible(true);
    m_window.m_shellLayoutController->setNodeEditorVisible(false);
    m_window.m_shellLayoutController->setMixVisible(false);
    m_window.m_shellLayoutController->setAudioPoolVisible(false);
    m_window.m_shellLayoutController->setVideoDetached(false);
    m_window.m_shellLayoutController->setPreferredSizes(
        m_window.m_audioPoolPreferredWidth,
        m_window.m_timelinePreferredHeight,
        m_window.m_nodeEditorPreferredHeight,
        m_window.m_mixPreferredHeight);

    m_window.syncShellLayoutViewport();
}

void ShellUiSetupController::connectOverlayControllers() const
{
    QObject::connect(m_window.m_contextMenuController, &ContextMenuController::changed, &m_window, [&window = m_window]()
    {
        window.updateOverlayPositions();
    });
    QObject::connect(m_window.m_contextMenuController, &ContextMenuController::itemTriggered, &m_window, [&window = m_window](const QString& key)
    {
        if (key == QStringLiteral("node.openEditor"))
        {
            if (!window.m_contextMenuTrackId.isNull())
            {
                window.m_controller->selectTrack(window.m_contextMenuTrackId);
            }
            if (window.m_showNodeEditorAction)
            {
                window.m_showNodeEditorAction->setChecked(true);
            }
            return;
        }
        if (key == QStringLiteral("node.rename"))
        {
            const auto text = window.m_dialogController
                ? window.m_dialogController->execTextInput(
                    QStringLiteral("Rename Node"),
                    QStringLiteral("Node name"),
                    window.m_contextMenuNodeLabel)
                : std::optional<QString>{};
            if (text.has_value())
            {
                const auto updatedLabel = text->trimmed();
                if (!updatedLabel.isEmpty() && updatedLabel != window.m_contextMenuNodeLabel && !window.m_contextMenuTrackId.isNull())
                {
                    window.m_controller->renameTrack(window.m_contextMenuTrackId, updatedLabel);
                    if (window.m_nodeEditorWorkspaceSession)
                    {
                        window.m_nodeEditorWorkspaceSession->markTrackUnsaved(window.m_contextMenuTrackId);
                        window.m_nodeEditorWorkspaceSession->refresh(window.hasOpenProject());
                    }
                }
            }
            return;
        }
        if (key == QStringLiteral("node.trim"))
        {
            window.trimSelectedNodeToSound();
            return;
        }
        if (key == QStringLiteral("node.autoPan"))
        {
            window.toggleSelectedNodeAutoPan();
            return;
        }
        if (key == QStringLiteral("loop.delete"))
        {
            window.clearLoopRange();
        }
    });

    QObject::connect(m_window.m_dialogController, &DialogController::changed, &m_window, [&window = m_window]()
    {
        window.updateOverlayPositions();
    });
    QObject::connect(m_window.m_filePickerController, &FilePickerController::changed, &m_window, [&window = m_window]()
    {
        window.updateOverlayPositions();
    });
    QObject::connect(m_window.m_shellOverlayController, &ShellOverlayController::changed, &m_window, [&window = m_window]()
    {
        if (!window.m_shellOverlayController->trackGainPopupVisible())
        {
            window.m_trackGainPopupTrackId = {};
        }
    });
    QObject::connect(
        m_window.m_shellOverlayController,
        &ShellOverlayController::trackGainSliderValueChanged,
        &m_window,
        [&window = m_window](const int sliderValue)
        {
            if (window.m_trackGainPopupTrackId.isNull())
            {
                return;
            }

            const auto gainDb = mixGainSliderValueToDb(sliderValue);
            window.updateTrackMixGainPopupValue(gainDb);
            if (!window.m_controller->setMixLaneGainForTrack(window.m_trackGainPopupTrackId, gainDb))
            {
                return;
            }

            window.refreshMixView();
            if (!window.m_projectStateChangeInProgress && window.hasOpenProject())
            {
                window.setProjectDirty(true);
            }
        });
}

void ShellUiSetupController::createDebugOverlay() const
{
    auto* debugOverlay = new DebugOverlayWindow();
    m_window.m_debugOverlay = debugOverlay;
    QObject::connect(&m_window, &QObject::destroyed, debugOverlay, &QObject::deleteLater);
    m_window.m_debugOverlay->setTransientParent(&m_window);
    m_window.m_debugOverlay->setPosition(m_window.mapToGlobal(QPoint(16, 48)));
    m_window.m_debugOverlay->setVisible(m_window.m_debugVisible);
    QObject::connect(debugOverlay, &DebugOverlayWindow::closeRequested, &m_window, [&window = m_window]()
    {
        window.updateDebugVisibility(false);
        window.showStatus(QStringLiteral("Debug window hidden."));
    });
}

void ShellUiSetupController::connectSceneGraphInitialization() const
{
    QObject::connect(
        &m_window,
        &QQuickWindow::sceneGraphInitialized,
        &m_window,
        [&window = m_window]()
        {
#ifdef Q_OS_WIN
            auto* quickDevice = window.rendererInterface()
                ? static_cast<ID3D11Device*>(window.rendererInterface()->getResource(&window, QSGRendererInterface::DeviceResource))
                : nullptr;
            if (quickDevice)
            {
                enableD3D11MultithreadProtection(quickDevice);
                quickDevice->AddRef();
            }

            QMetaObject::invokeMethod(
                &window,
                [&window, quickDevice]()
                {
                    bool nativePresentationReady = quickDevice != nullptr;
                    clearStuckWaitCursor(&window);
                    window.m_controller->setPreferredD3D11Device(quickDevice);

                    if (window.hasOpenProject() && window.m_controller->hasVideoLoaded() && window.m_controller->videoHardwareAccelerated())
                    {
                        QString errorMessage;
                        const auto controllerState = window.m_controller->snapshotProjectState();
                        window.m_projectStateChangeInProgress = true;
                        const auto restored = window.m_controller->restoreProjectState(controllerState, &errorMessage);
                        window.m_projectStateChangeInProgress = false;
                        if (!restored)
                        {
                            nativePresentationReady = false;
                            if (!errorMessage.isEmpty())
                            {
                                qWarning().noquote()
                                    << "Failed to refresh startup video for native Quick presentation:"
                                    << errorMessage;
                            }
                        }
                    }

                    window.m_nativeVideoPresentationAllowed = nativePresentationReady;
                    window.updateDetachedPanelUiState();
                    if (quickDevice)
                    {
                        quickDevice->Release();
                    }
                    clearStuckWaitCursor(&window);
                    window.updateMixQuickDiagnostics();
                },
                Qt::QueuedConnection);
#else
            QMetaObject::invokeMethod(
                &window,
                [&window]()
                {
                    clearStuckWaitCursor(&window);
                    window.m_nativeVideoPresentationAllowed = false;
                    window.updateDetachedPanelUiState();
                    window.updateMixQuickDiagnostics();
                },
                Qt::QueuedConnection);
#endif
        },
        Qt::DirectConnection);
}

void ShellUiSetupController::finalizeBuild() const
{
    m_window.handleTimelineQuickStatusChanged();
    m_window.handleMixQuickStatusChanged();
    m_window.updateOverlayPositions();
}
