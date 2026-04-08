#include "app/MainWindow.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iterator>

#include <QApplication>
#include <QCursor>
#include <QDataStream>
#include <QDebug>
#include <QDir>
#include <QDrag>
#include <QEnterEvent>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QPixmap>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QSGRendererInterface>
#include <QScrollArea>
#include <QScreen>
#include <QScopedValueRollback>
#include <QSettings>
#include <QShortcut>
#include <QSignalBlocker>
#include <QToolTip>
#include <QResizeEvent>
#include <QWidget>
#include <QUrl>

#ifdef Q_OS_WIN
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <d3d11.h>
#include <d3d11_4.h>
#include <dxgi1_4.h>
#include <psapi.h>
#endif

#include "app/PlayerController.h"
#include "app/ActionRegistry.h"
#include "app/AudioPoolQuickController.h"
#include "app/ContextMenuController.h"
#include "app/DialogController.h"
#include "app/FilePickerController.h"
#include "app/MainWindowActions.h"
#include "app/ProjectWindowController.h"
#include "app/ShellUiSetupController.h"
#include "app/ShellLayoutController.h"
#include "app/ShellOverlayController.h"
#include "app/PanelLayoutController.h"
#include "app/DebugUiController.h"
#include "app/MediaImportController.h"
#include "app/NodeEditorPreviewSession.h"
#include "app/NodeEditorWorkspaceSession.h"
#include "app/ProjectDocument.h"
#include "app/TimelineThumbnailGenerationController.h"
#include "app/TransportUiSyncController.h"
#include "app/VideoProxyController.h"
#include "ui/MixQuickController.h"
#include "ui/NativeVideoViewport.h"
#include "ui/NodeEditorQuickController.h"
#include "ui/ThumbnailStripQuickController.h"
#include "ui/TimelineQuickController.h"
#include "ui/VideoViewportQuickController.h"

namespace
{
void setActionCheckedSilently(QAction* action, const bool checked)
{
    if (!action || action->isChecked() == checked)
    {
        return;
    }

    const QSignalBlocker blocker{action};
    action->setChecked(checked);
}

Qt::CaseSensitivity pathCaseSensitivity()
{
#ifdef Q_OS_WIN
    return Qt::CaseInsensitive;
#else
    return Qt::CaseSensitive;
#endif
}

QString normalizedAbsolutePath(const QString& path)
{
    if (path.isEmpty())
    {
        return {};
    }

    return QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath()));
}

bool pathsMatch(const QString& left, const QString& right)
{
    return QString::compare(normalizedAbsolutePath(left), normalizedAbsolutePath(right), pathCaseSensitivity()) == 0;
}

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

static QString audioPathFromMimeData(const QMimeData* mimeData)
{
    if (!mimeData)
    {
        return {};
    }

    if (mimeData->hasFormat("application/x-dawg-audio-path"))
    {
        return QString::fromUtf8(mimeData->data("application/x-dawg-audio-path"));
    }

    if (mimeData->hasUrls())
    {
        const auto urls = mimeData->urls();
        if (!urls.isEmpty() && urls.front().isLocalFile())
        {
            return urls.front().toLocalFile();
        }
    }

    if (mimeData->hasText())
    {
        return mimeData->text().trimmed();
    }

    return {};
}

bool itemContainsPoint(QQuickItem* item, const QPointF& scenePoint)
{
    if (!item || !item->isVisible())
    {
        return false;
    }

    return item->mapRectToScene(QRectF(0.0, 0.0, item->width(), item->height())).contains(scenePoint);
}

bool itemHasActiveFocus(QQuickWindow* window, QQuickItem* item)
{
    if (!window || !item)
    {
        return false;
    }

    for (auto* focused = window->activeFocusItem(); focused; focused = focused->parentItem())
    {
        if (focused == item)
        {
            return true;
        }
    }

    return false;
}

QString graphicsApiToString(const QSGRendererInterface::GraphicsApi api)
{
    switch (api)
    {
    case QSGRendererInterface::Unknown:
        return QStringLiteral("Unknown");
    case QSGRendererInterface::Software:
        return QStringLiteral("Software");
    case QSGRendererInterface::OpenVG:
        return QStringLiteral("OpenVG");
    case QSGRendererInterface::OpenGL:
        return QStringLiteral("OpenGL");
    case QSGRendererInterface::Direct3D11:
        return QStringLiteral("D3D11");
    case QSGRendererInterface::Vulkan:
        return QStringLiteral("Vulkan");
    case QSGRendererInterface::Metal:
        return QStringLiteral("Metal");
    case QSGRendererInterface::Null:
        return QStringLiteral("Null");
    case QSGRendererInterface::Direct3D12:
        return QStringLiteral("D3D12");
    }

    return QStringLiteral("Unknown");
}

constexpr auto kLastProjectPathSettingsKey = "project/lastProjectPath";
constexpr auto kRecentProjectPathsSettingsKey = "project/recentProjectPaths";
constexpr int kMaxRecentProjectCount = 10;
constexpr int kMixGainPopupMinValue = -1000;
constexpr int kMixGainPopupMaxValue = 120;
constexpr qint64 kAudioPoolPlaybackRefreshIntervalMs = 100;

int mixGainDbToSliderValue(const float gainDb)
{
    if (gainDb <= -99.9F)
    {
        return kMixGainPopupMinValue;
    }

    return static_cast<int>(std::lround(std::clamp(gainDb, -100.0F, 12.0F) * 10.0F));
}

float mixGainSliderValueToDb(const int sliderValue)
{
    return sliderValue <= kMixGainPopupMinValue ? -100.0F : static_cast<float>(sliderValue) / 10.0F;
}

Qt::CaseSensitivity projectPathCaseSensitivity()
{
#ifdef Q_OS_WIN
    return Qt::CaseInsensitive;
#else
    return Qt::CaseSensitive;
#endif
}

QString normalizeProjectFilePath(const QString& path)
{
    if (path.isEmpty())
    {
        return {};
    }

    return QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(path).absoluteFilePath()));
}

bool projectPathMatches(const QString& left, const QString& right)
{
    return QString::compare(left, right, projectPathCaseSensitivity()) == 0;
}

const QStringList& projectVideoExtensions()
{
    static const QStringList kExtensions{
        QStringLiteral("mp4"),
        QStringLiteral("mov"),
        QStringLiteral("mkv"),
        QStringLiteral("avi")
    };
    return kExtensions;
}

const QStringList& projectAudioExtensions()
{
    static const QStringList kExtensions{
        QStringLiteral("wav"),
        QStringLiteral("mp3"),
        QStringLiteral("flac"),
        QStringLiteral("aif"),
        QStringLiteral("aiff"),
        QStringLiteral("m4a"),
        QStringLiteral("aac"),
        QStringLiteral("ogg")
    };
    return kExtensions;
}

QStringList projectMediaFilesInDirectory(const QString& directoryPath, const QStringList& extensions)
{
    const QDir directory(directoryPath);
    if (!directory.exists())
    {
        return {};
    }

    QStringList files;
    const auto entries = directory.entryInfoList(QDir::Files, QDir::Name | QDir::IgnoreCase);
    files.reserve(entries.size());
    for (const auto& entry : entries)
    {
        if (extensions.contains(entry.suffix().toLower()))
        {
            files.push_back(QDir::cleanPath(entry.absoluteFilePath()));
        }
    }
    return files;
}

QString normalizedMediaCopyBaseName(const QFileInfo& fileInfo)
{
    auto baseName = fileInfo.completeBaseName();
    static const QRegularExpression copySuffixPattern(QStringLiteral(R"( \(\d+\)$)"));
    baseName.remove(copySuffixPattern);
    return baseName;
}

std::optional<QString> preferredRecoveredVideoPath(const QStringList& videoFiles)
{
    if (videoFiles.isEmpty())
    {
        return std::nullopt;
    }

    QStringList rankedFiles = videoFiles;
    std::sort(
        rankedFiles.begin(),
        rankedFiles.end(),
        [](const QString& leftPath, const QString& rightPath)
        {
            const QFileInfo leftInfo(leftPath);
            const QFileInfo rightInfo(rightPath);
            const auto leftNormalizedBase = normalizedMediaCopyBaseName(leftInfo);
            const auto rightNormalizedBase = normalizedMediaCopyBaseName(rightInfo);
            const auto leftIsCopiedName =
                QString::compare(leftInfo.completeBaseName(), leftNormalizedBase, Qt::CaseInsensitive) != 0;
            const auto rightIsCopiedName =
                QString::compare(rightInfo.completeBaseName(), rightNormalizedBase, Qt::CaseInsensitive) != 0;
            if (leftIsCopiedName != rightIsCopiedName)
            {
                return !leftIsCopiedName;
            }

            const auto leftNameCompare = QString::compare(leftNormalizedBase, rightNormalizedBase, Qt::CaseInsensitive);
            if (leftNameCompare != 0)
            {
                return leftNameCompare < 0;
            }

            if (leftInfo.fileName().size() != rightInfo.fileName().size())
            {
                return leftInfo.fileName().size() < rightInfo.fileName().size();
            }

            return QString::compare(leftInfo.fileName(), rightInfo.fileName(), Qt::CaseInsensitive) < 0;
        });
    return QDir::cleanPath(rankedFiles.front());
}

bool recoverProjectMediaFromFolders(
    dawg::project::ControllerState* state,
    const QString& projectRootPath,
    QString* message)
{
    if (!state)
    {
        return false;
    }

    bool recovered = false;
    if (state->videoPath.isEmpty())
    {
        const auto videoFiles = projectMediaFilesInDirectory(
            QDir(projectRootPath).filePath(QStringLiteral("video")),
            projectVideoExtensions());
        if (const auto recoveredVideoPath = preferredRecoveredVideoPath(videoFiles); recoveredVideoPath.has_value())
        {
            state->videoPath = *recoveredVideoPath;
            recovered = true;
        }
    }

    if (state->audioPoolAssetPaths.empty())
    {
        const auto audioFiles = projectMediaFilesInDirectory(
            QDir(projectRootPath).filePath(QStringLiteral("audio")),
            projectAudioExtensions());
        if (!audioFiles.isEmpty())
        {
            state->audioPoolAssetPaths.assign(audioFiles.cbegin(), audioFiles.cend());
            recovered = true;
        }
    }

    if (recovered && message)
    {
        QStringList recoveredParts;
        if (!state->videoPath.isEmpty())
        {
            recoveredParts.push_back(QStringLiteral("video"));
        }
        if (!state->audioPoolAssetPaths.empty())
        {
            recoveredParts.push_back(QStringLiteral("audio pool"));
        }
        *message = QStringLiteral("Recovered %1 from project folders. Save the project to persist the recovered state.")
            .arg(recoveredParts.join(QStringLiteral(" and ")));
    }
    return recovered;
}

QString currentMemoryUsageText()
{
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS_EX counters{};
    if (GetProcessMemoryInfo(
            GetCurrentProcess(),
            reinterpret_cast<PPROCESS_MEMORY_COUNTERS>(&counters),
            sizeof(counters)))
    {
        const auto workingSetMb = static_cast<double>(counters.WorkingSetSize) / (1024.0 * 1024.0);
        return QStringLiteral("Memory %1 MB").arg(workingSetMb, 0, 'f', 1);
    }
#endif

    return QStringLiteral("Memory --");
}

QString currentVideoMemoryUsageText()
{
#ifdef Q_OS_WIN
    IDXGIFactory4* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) || !factory)
    {
        return QStringLiteral("VRAM --");
    }

    quint64 totalUsageBytes = 0;
    for (UINT adapterIndex = 0;; ++adapterIndex)
    {
        IDXGIAdapter1* adapter = nullptr;
        if (factory->EnumAdapters1(adapterIndex, &adapter) == DXGI_ERROR_NOT_FOUND)
        {
            break;
        }

        IDXGIAdapter3* adapter3 = nullptr;
        if (SUCCEEDED(adapter->QueryInterface(IID_PPV_ARGS(&adapter3))) && adapter3)
        {
            DXGI_QUERY_VIDEO_MEMORY_INFO localMemoryInfo{};
            if (SUCCEEDED(adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &localMemoryInfo)))
            {
                totalUsageBytes += localMemoryInfo.CurrentUsage;
            }
            adapter3->Release();
        }

        adapter->Release();
    }

    factory->Release();

    if (totalUsageBytes > 0)
    {
        const auto totalUsageMb = static_cast<double>(totalUsageBytes) / (1024.0 * 1024.0);
        return QStringLiteral("VRAM %1 MB").arg(totalUsageMb, 0, 'f', 1);
    }
#endif

    return QStringLiteral("VRAM --");
}

QString currentProcessorUsageText()
{
#ifdef Q_OS_WIN
    FILETIME creationTime{};
    FILETIME exitTime{};
    FILETIME kernelTime{};
    FILETIME userTime{};
    FILETIME systemTime{};
    if (!GetProcessTimes(GetCurrentProcess(), &creationTime, &exitTime, &kernelTime, &userTime))
    {
        return QStringLiteral("CPU --");
    }

    GetSystemTimeAsFileTime(&systemTime);

    ULARGE_INTEGER kernel{};
    kernel.LowPart = kernelTime.dwLowDateTime;
    kernel.HighPart = kernelTime.dwHighDateTime;

    ULARGE_INTEGER user{};
    user.LowPart = userTime.dwLowDateTime;
    user.HighPart = userTime.dwHighDateTime;

    ULARGE_INTEGER now{};
    now.LowPart = systemTime.dwLowDateTime;
    now.HighPart = systemTime.dwHighDateTime;

    static quint64 previousProcessTicks = 0;
    static quint64 previousWallTicks = 0;
    static const unsigned int processorCount = []() -> unsigned int
    {
        SYSTEM_INFO systemInfo{};
        GetSystemInfo(&systemInfo);
        return std::max<unsigned int>(1u, static_cast<unsigned int>(systemInfo.dwNumberOfProcessors));
    }();

    const auto processTicks = kernel.QuadPart + user.QuadPart;
    const auto wallTicks = now.QuadPart;
    if (previousWallTicks == 0 || wallTicks <= previousWallTicks || processTicks < previousProcessTicks)
    {
        previousProcessTicks = processTicks;
        previousWallTicks = wallTicks;
        return QStringLiteral("CPU --");
    }

    const auto processDelta = static_cast<double>(processTicks - previousProcessTicks);
    const auto wallDelta = static_cast<double>(wallTicks - previousWallTicks);
    previousProcessTicks = processTicks;
    previousWallTicks = wallTicks;

    if (wallDelta <= 0.0)
    {
        return QStringLiteral("CPU --");
    }

    const auto cpuPercent = std::clamp((processDelta / wallDelta) * (100.0 / processorCount), 0.0, 999.0);
    return QStringLiteral("CPU %1%").arg(cpuPercent, 0, 'f', 1);
#endif

    return QStringLiteral("CPU --");
}

QCursor audioPoolPreviewCursor()
{
    static const QCursor cursor = []()
    {
        QPixmap pixmap(20, 20);
        pixmap.fill(Qt::transparent);

        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);

        QPainterPath triangle;
        triangle.moveTo(6.0, 4.0);
        triangle.lineTo(16.0, 10.0);
        triangle.lineTo(6.0, 16.0);
        triangle.closeSubpath();

        painter.fillPath(triangle, Qt::white);
        return QCursor(pixmap, 10, 10);
    }();
    return cursor;
}

bool pathIsInsideRoot(const QString& rootPath, const QString& candidatePath)
{
    const auto cleanedRoot = QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(rootPath).absoluteFilePath()));
    const auto cleanedCandidate = QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(candidatePath).absoluteFilePath()));
#ifdef Q_OS_WIN
    const auto compareSensitivity = Qt::CaseInsensitive;
#else
    const auto compareSensitivity = Qt::CaseSensitive;
#endif
    const auto rootPrefix = cleanedRoot.endsWith(QLatin1Char('/'))
        ? cleanedRoot
        : (cleanedRoot + QLatin1Char('/'));
    return QString::compare(cleanedCandidate, cleanedRoot, compareSensitivity) == 0
        || cleanedCandidate.startsWith(rootPrefix, compareSensitivity);
}

QString mixGainDisplayText(const float gainDb)
{
    if (gainDb <= -99.9F)
    {
        return QStringLiteral("-inf");
    }

    return QStringLiteral("%1 dB").arg(gainDb, 0, 'f', 1);
}

QPoint clampContextMenuPosition(
    const QQuickItem* contentAreaItem,
    const QPointF& requestedPosition,
    const QVariantList& items,
    const bool hasTitle)
{
    if (!contentAreaItem)
    {
        return QPoint{
            static_cast<int>(std::lround(requestedPosition.x())),
            static_cast<int>(std::lround(requestedPosition.y()))};
    }

    constexpr int menuWidth = 220;
    constexpr int edgeMargin = 8;
    constexpr int itemHeight = 30;
    constexpr int separatorHeight = 10;
    constexpr int sectionSpacing = 4;
    constexpr int verticalChrome = 16;
    constexpr int titleHeight = 26;

    int menuHeight = verticalChrome;
    if (hasTitle)
    {
        menuHeight += titleHeight + sectionSpacing;
    }

    for (int index = 0; index < items.size(); ++index)
    {
        const auto item = items.at(index).toMap();
        menuHeight += item.value(QStringLiteral("separator")).toBool() ? separatorHeight : itemHeight;
        if (index + 1 < items.size())
        {
            menuHeight += sectionSpacing;
        }
    }

    const auto contentWidth = static_cast<int>(std::lround(contentAreaItem->width()));
    const auto contentHeight = static_cast<int>(std::lround(contentAreaItem->height()));
    const auto requestedX = static_cast<int>(std::lround(requestedPosition.x()));
    const auto requestedY = static_cast<int>(std::lround(requestedPosition.y()));

    const auto clampedX = std::clamp(
        requestedX,
        edgeMargin,
        std::max(edgeMargin, contentWidth - menuWidth - edgeMargin));

    auto clampedY = requestedY;
    if (requestedY + menuHeight > contentHeight - edgeMargin)
    {
        clampedY = requestedY - menuHeight;
    }
    clampedY = std::clamp(
        clampedY,
        edgeMargin,
        std::max(edgeMargin, contentHeight - menuHeight - edgeMargin));

    return QPoint{clampedX, clampedY};
}

int timelinePreferredHeight(const std::vector<TimelineTrackSpan>& trackSpans)
{
    Q_UNUSED(trackSpans);
    constexpr int baseHeight = 148;
    return baseHeight;
}

}

MainWindow::MainWindow(QWindow* parent)
    : QQuickView(parent)
    , m_controller(new PlayerController(this))
{
    m_shellUiSetupController = std::make_unique<ShellUiSetupController>(*this);
    m_timelineThumbnailGenerationController = std::make_unique<TimelineThumbnailGenerationController>(*this);
    buildUi();
    m_actionsController = std::make_unique<MainWindowActions>(*this);
    m_projectWindowController = std::make_unique<ProjectWindowController>(*this);
    m_panelLayoutController = std::make_unique<PanelLayoutController>(*this);
    m_debugUiController = std::make_unique<DebugUiController>(*this);
    m_mediaImportController = std::make_unique<MediaImportController>(*this);
    m_videoProxyController = std::make_unique<VideoProxyController>(*this);
    clearStuckWaitCursor(this);
    buildMenus();
    if (m_actionRegistry)
    {
        m_actionRegistry->rebuild();
    }
    rebuildRecentProjectsMenu();
    updateDetachedPanelUiState();
    qApp->installEventFilter(this);
    m_clearAllShortcutTimer.setSingleShot(true);
    m_clearAllShortcutTimer.setInterval(1500);
    m_memoryUsageTimer.setInterval(1000);
    m_mixMeterTimer.setInterval(33);
    connect(&m_clearAllShortcutTimer, &QTimer::timeout, this, &MainWindow::clearPendingClearAllShortcut);
    connect(&m_memoryUsageTimer, &QTimer::timeout, this, &MainWindow::updateMemoryUsage);
    connect(&m_mixMeterTimer, &QTimer::timeout, this, &MainWindow::updateMixMeterLevels);
    m_statusToastTimer.setSingleShot(true);
    m_statusToastTimer.setInterval(2800);
    connect(&m_statusToastTimer, &QTimer::timeout, this, [this]()
    {
        if (m_shellOverlayController)
        {
            m_shellOverlayController->hideStatus();
        }
    });
    m_canvasTipsTimer.setSingleShot(true);
    m_canvasTipsTimer.setInterval(6000);
    connect(&m_canvasTipsTimer, &QTimer::timeout, this, &MainWindow::hideCanvasTipsOverlay);
    m_nodeNudgeTimer.setSingleShot(false);
    m_nodeNudgeTimer.setInterval(220);
    connect(&m_nodeNudgeTimer, &QTimer::timeout, this, &MainWindow::applyHeldNodeNudge);

    connect(m_newProjectAction, &QAction::triggered, this, &MainWindow::newProject);
    connect(m_openProjectAction, &QAction::triggered, this, &MainWindow::openProject);
    connect(m_saveProjectAction, &QAction::triggered, this, &MainWindow::saveProject);
    connect(m_saveProjectAsAction, &QAction::triggered, this, &MainWindow::saveProjectAs);
    connect(m_openAction, &QAction::triggered, this, &MainWindow::openVideo);
    connect(m_quitAction, &QAction::triggered, this, [this]()
    {
        close();
    });
    connect(m_importSoundAction, &QAction::triggered, this, &MainWindow::importAudioToPool);
    connect(m_detachVideoAction, &QAction::triggered, this, [this]()
    {
        if (m_videoDetached)
        {
            attachVideo();
        }
        else
        {
            detachVideo();
        }
    });
    connect(m_detachTimelineAction, &QAction::triggered, this, [this]()
    {
        if (m_timelineDetached)
        {
            attachTimeline();
        }
        else
        {
            detachTimeline();
        }
    });
    connect(m_detachMixAction, &QAction::triggered, this, [this]()
    {
        if (m_mixDetached)
        {
            attachMix();
        }
        else
        {
            detachMix();
        }
    });
    connect(m_detachAudioPoolAction, &QAction::triggered, this, [this]()
    {
        if (m_audioPoolDetached)
        {
            attachAudioPool();
        }
        else
        {
            detachAudioPool();
        }
    });
    connect(m_showTimelineAction, &QAction::toggled, this, [this](const bool visible)
    {
        updateTimelineVisibility(visible);
        if (!m_projectStateChangeInProgress && hasOpenProject())
        {
            setProjectDirty(true);
        }
        showStatus(visible ? QStringLiteral("Timeline shown.") : QStringLiteral("Timeline hidden."));
    });
    connect(m_showNodeEditorAction, &QAction::toggled, this, [this](const bool visible)
    {
        if (visible)
        {
            if (!m_controller->hasSelection())
            {
                setActionCheckedSilently(m_showNodeEditorAction, false);
                return;
            }

            if (m_nodeEditorWorkspaceSession)
            {
                m_nodeEditorWorkspaceSession->refresh(hasOpenProject());
            }
        }

        updateNodeEditorVisibility(visible);
        if (!m_projectStateChangeInProgress && hasOpenProject())
        {
            setProjectDirty(true);
        }
        showStatus(visible ? QStringLiteral("Node Editor shown.") : QStringLiteral("Node Editor hidden."));
    });
    connect(m_showMixAction, &QAction::toggled, this, [this](const bool visible)
    {
        updateMixVisibility(visible);
        if (!m_projectStateChangeInProgress && hasOpenProject())
        {
            setProjectDirty(true);
        }
        showStatus(visible ? QStringLiteral("Mix window shown.") : QStringLiteral("Mix window hidden."));
    });
    connect(m_showTimelineThumbnailsAction, &QAction::toggled, this, [this](const bool visible)
    {
        setTimelineThumbnailsVisible(visible);
        m_showTimelineThumbnailsAction->setText(
            visible ? QStringLiteral("Hide Thumbnails") : QStringLiteral("Show Thumbnails"));
        if (!m_projectStateChangeInProgress && hasOpenProject())
        {
            setProjectDirty(true);
        }
        showStatus(visible ? QStringLiteral("Thumbnails shown.") : QStringLiteral("Thumbnails hidden."));
    });
    connect(m_timelineClickSeeksAction, &QAction::toggled, this, [this](const bool enabled)
    {
        setTimelineSeekOnClickEnabled(enabled || !m_controller->isPlaying());
        if (!m_projectStateChangeInProgress && hasOpenProject())
        {
            setProjectDirty(true);
        }
        showStatus(
            enabled
                ? QStringLiteral("Timeline click seek enabled.")
                : QStringLiteral("Timeline click seek disabled. Use play or scrub to move."));
    });
    connect(m_mixSoloModeAction, &QAction::toggled, this, [this](const bool xorMode)
    {
        m_controller->setMixSoloXorMode(xorMode);
        if (!m_projectStateChangeInProgress && hasOpenProject())
        {
            setProjectDirty(true);
        }
        showStatus(
            xorMode
                ? QStringLiteral("Mixer solo mode set to X-OR.")
                : QStringLiteral("Mixer solo mode set to latch."));
    });
    connect(m_audioPoolAction, &QAction::toggled, this, [this](const bool visible)
    {
        updateAudioPoolVisibility(visible);
        if (!m_projectStateChangeInProgress && hasOpenProject())
        {
            setProjectDirty(true);
        }
        showStatus(visible ? QStringLiteral("Audio Pool shown.") : QStringLiteral("Audio Pool hidden."));
    });
    connect(m_showNativeViewportAction, &QAction::toggled, this, [this](const bool visible)
    {
        updateNativeViewportVisibility(visible);
        showStatus(
            visible
                ? QStringLiteral("Native video viewport test shown.")
                : QStringLiteral("Native video viewport test hidden."));
    });
    connect(
        m_motionTrackingAction,
        &QAction::toggled,
        m_controller,
        &PlayerController::setMotionTrackingEnabled);
    connect(
        m_insertionFollowsPlaybackAction,
        &QAction::toggled,
        m_controller,
        &PlayerController::setInsertionFollowsPlayback);
    connect(m_controller, &PlayerController::mixSoloModeChanged, this, [this](const bool xorMode)
    {
        if (m_mixSoloModeAction)
        {
            const QSignalBlocker blocker(m_mixSoloModeAction);
            m_mixSoloModeAction->setChecked(xorMode);
        }
        refreshMixView();
        refreshTimeline();
    });
    connect(m_goToStartAction, &QAction::triggered, m_controller, &PlayerController::goToStart);
    connect(m_playAction, &QAction::triggered, this, &MainWindow::playFromPreferredContext);
    connect(m_stepForwardAction, &QAction::triggered, m_controller, &PlayerController::stepForward);
    connect(m_stepBackAction, &QAction::triggered, m_controller, &PlayerController::stepBackward);
    connect(m_stepFastForwardAction, &QAction::triggered, m_controller, &PlayerController::stepFastForward);
    connect(m_stepFastBackAction, &QAction::triggered, m_controller, &PlayerController::stepFastBackward);
    connect(m_copyAction, &QAction::triggered, this, [this]()
    {
        if (nodeEditorHasFocus())
        {
            requestNodeEditorEditAction(QStringLiteral("copyClip"));
            return;
        }
        copySelectedNode();
    });
    connect(m_pasteAction, &QAction::triggered, this, [this]()
    {
        if (nodeEditorHasFocus())
        {
            requestNodeEditorEditAction(QStringLiteral("pasteClip"));
            return;
        }
        pasteNode();
    });
    connect(m_cutAction, &QAction::triggered, this, [this]()
    {
        if (nodeEditorHasFocus())
        {
            requestNodeEditorEditAction(QStringLiteral("cutClip"));
            return;
        }
        cutSelectedNode();
    });
    connect(m_undoAction, &QAction::triggered, this, &MainWindow::undoNodeEdit);
    connect(m_redoAction, &QAction::triggered, this, &MainWindow::redoNodeEdit);
    connect(m_selectNextNodeAction, &QAction::triggered, this, &MainWindow::selectNextVisibleNode);
    connect(m_moveNodeUpAction, &QAction::triggered, this, &MainWindow::moveSelectedNodeUp);
    connect(m_moveNodeDownAction, &QAction::triggered, this, &MainWindow::moveSelectedNodeDown);
    connect(m_moveNodeLeftAction, &QAction::triggered, this, &MainWindow::moveSelectedNodeLeft);
    connect(m_moveNodeRightAction, &QAction::triggered, this, &MainWindow::moveSelectedNodeRight);
    connect(m_selectAllAction, &QAction::triggered, m_controller, &PlayerController::selectAllVisibleTracks);
    connect(m_unselectAllAction, &QAction::triggered, m_controller, &PlayerController::clearSelection);
    connect(
        m_setNodeStartAction,
        &QAction::triggered,
        this,
        &MainWindow::handleNodeStartShortcut);
    connect(
        m_setNodeEndAction,
        &QAction::triggered,
        this,
        &MainWindow::handleNodeEndShortcut);
    connect(m_setLoopStartAction, &QAction::triggered, this, &MainWindow::handleLoopStartShortcut);
    connect(m_setLoopEndAction, &QAction::triggered, this, &MainWindow::handleLoopEndShortcut);
    connect(m_clearLoopRangeAction, &QAction::triggered, this, &MainWindow::clearLoopRange);
    connect(m_trimNodeAction, &QAction::triggered, this, &MainWindow::trimSelectedNodeToSound);
    connect(m_autoPanAction, &QAction::triggered, this, &MainWindow::toggleSelectedNodeAutoPan);
    connect(m_toggleNodeNameAction, &QAction::triggered, m_controller, &PlayerController::toggleSelectedTrackLabels);
    connect(m_showAllNodeNamesAction, &QAction::toggled, this, [this](const bool enabled)
    {
        m_videoViewportQuickController->setShowAllLabels(enabled);
        if (m_nativeViewport)
        {
            m_nativeViewport->setShowAllLabels(enabled);
        }
        if (!m_projectStateChangeInProgress && hasOpenProject())
        {
            setProjectDirty(true);
        }
        showStatus(
            enabled
                ? QStringLiteral("Node names always visible.")
                : QStringLiteral("Node names only show when relevant."));
    });
    connect(m_useProxyVideoAction, &QAction::toggled, this, [this](const bool enabled)
    {
        if (m_videoProxyController)
        {
            m_videoProxyController->setProxyEnabled(enabled);
        }
        if (!m_projectStateChangeInProgress && hasOpenProject())
        {
            setProjectDirty(true);
        }
    });
    connect(m_deleteNodeAction, &QAction::triggered, m_controller, &PlayerController::deleteSelectedTrack);
    connect(m_deleteEmptyNodesAction, &QAction::triggered, this, &MainWindow::deleteAllEmptyNodes);
    connect(m_clearAllAction, &QAction::triggered, m_controller, &PlayerController::clearAllTracks);
    connect(m_timelineQuickController, &TimelineQuickController::frameRequested, this, [this](const int frameIndex)
    {
        setPreferredPlaybackContext(PlaybackContext::Project);
        m_controller->seekToFrame(frameIndex);
    });
    connect(m_thumbnailStripQuickController, &ThumbnailStripQuickController::frameRequested, this, [this](const int frameIndex)
    {
        setPreferredPlaybackContext(PlaybackContext::Project);
        m_controller->seekToFrame(frameIndex);
    });
    connect(
        m_timelineQuickController,
        &TimelineQuickController::addLoopRangeRequested,
        m_controller,
        &PlayerController::addLoopRange);
    connect(
        m_timelineQuickController,
        &TimelineQuickController::loopStartFrameRequested,
        m_controller,
        &PlayerController::setLoopStartFrame);
    connect(
        m_timelineQuickController,
        &TimelineQuickController::loopEndFrameRequested,
        m_controller,
        &PlayerController::setLoopEndFrame);
    connect(
        m_timelineQuickController,
        &TimelineQuickController::deleteLoopRangeRequested,
        m_controller,
        &PlayerController::removeLoopRange);
    connect(
        m_timelineQuickController,
        &TimelineQuickController::trackSelected,
        m_controller,
        &PlayerController::selectTrack);
    connect(m_timelineQuickController, &TimelineQuickController::trackActivated, this, [this](const QUuid& trackId)
    {
        m_controller->selectTrack(trackId);
        if (m_showNodeEditorAction)
        {
            m_showNodeEditorAction->setChecked(true);
        }
    });
    connect(
        m_timelineQuickController,
        &TimelineQuickController::trackStartFrameRequested,
        m_controller,
        &PlayerController::setTrackStartFrame);
    connect(
        m_timelineQuickController,
        &TimelineQuickController::trackEndFrameRequested,
        m_controller,
        &PlayerController::setTrackEndFrame);
    connect(
        m_timelineQuickController,
        &TimelineQuickController::trackSpanMoveRequested,
        m_controller,
        &PlayerController::moveTrackFrameSpan);
    connect(
        m_timelineQuickController,
        &TimelineQuickController::trackContextMenuRequested,
        this,
        [this](const QUuid& trackId, const QPoint& globalPosition)
    {
        showNodeContextMenu(trackId, globalPosition, false);
    });
    connect(
        m_timelineQuickController,
        &TimelineQuickController::trackGainAdjustRequested,
        this,
        &MainWindow::adjustTrackMixGainFromWheel);
    connect(
        m_timelineQuickController,
        &TimelineQuickController::trackGainPopupRequested,
        this,
        &MainWindow::showTrackMixGainPopup);
    connect(m_timelineQuickController, &TimelineQuickController::loopContextMenuRequested, this, [this](const QPoint& globalPosition)
    {
        showLoopContextMenu(globalPosition);
    });
    connect(m_timelineQuickController, &TimelineQuickController::visualsChanged, this, [this]()
    {
        syncThumbnailStripViewWindow();
        if (m_clearLoopRangeAction)
        {
            m_clearLoopRangeAction->setEnabled(timelineHasSelectedLoopRange());
        }
    });
    if (m_showTimelineThumbnailsAction)
    {
        setTimelineThumbnailsVisible(m_showTimelineThumbnailsAction->isChecked());
    }
    connect(m_toggleDebugAction, &QAction::toggled, this, [this](const bool enabled)
    {
        updateDebugVisibility(enabled);
        showStatus(enabled ? QStringLiteral("Debug info shown.") : QStringLiteral("Debug info hidden."));
    });
    connect(m_playPauseShortcut, &QShortcut::activated, this, &MainWindow::playFromPreferredContext);
    connect(m_startShortcut, &QShortcut::activated, this, [this]()
    {
        if (nodeEditorHasFocus())
        {
            if (m_nodeEditorWorkspaceSession)
            {
                m_nodeEditorWorkspaceSession->resetPlayheadToStart();
            }
            return;
        }
        m_controller->goToStart();
    });
    connect(m_numpadStartShortcut, &QShortcut::activated, this, [this]()
    {
        if (nodeEditorHasFocus())
        {
            if (m_nodeEditorWorkspaceSession)
            {
                m_nodeEditorWorkspaceSession->resetPlayheadToStart();
            }
            return;
        }
        m_controller->goToStart();
    });
    connect(m_stepBackShortcut, &QShortcut::activated, m_controller, &PlayerController::stepBackward);
    connect(m_stepForwardShortcut, &QShortcut::activated, m_controller, &PlayerController::stepForward);
    connect(m_stepFastForwardShortcut, &QShortcut::activated, m_controller, &PlayerController::stepFastForward);
    connect(m_stepFastBackShortcut, &QShortcut::activated, m_controller, &PlayerController::stepFastBackward);
    connect(m_copyShortcut, &QShortcut::activated, this, [this]()
    {
        if (nodeEditorHasFocus())
        {
            requestNodeEditorEditAction(QStringLiteral("copyClip"));
            return;
        }
        copySelectedNode();
    });
    connect(m_pasteShortcut, &QShortcut::activated, this, [this]()
    {
        if (nodeEditorHasFocus())
        {
            requestNodeEditorEditAction(QStringLiteral("pasteClip"));
            return;
        }
        pasteNode();
    });
    connect(m_cutShortcut, &QShortcut::activated, this, [this]()
    {
        if (nodeEditorHasFocus())
        {
            requestNodeEditorEditAction(QStringLiteral("cutClip"));
            return;
        }
        cutSelectedNode();
    });
    connect(m_undoShortcut, &QShortcut::activated, this, &MainWindow::undoNodeEdit);
    connect(m_redoShortcut, &QShortcut::activated, this, &MainWindow::redoNodeEdit);
    connect(m_selectNextNodeShortcut, &QShortcut::activated, this, &MainWindow::selectNextVisibleNode);
    connect(
        m_insertionFollowsPlaybackShortcut,
        &QShortcut::activated,
        m_insertionFollowsPlaybackAction,
        &QAction::trigger);
    connect(m_selectAllShortcut, &QShortcut::activated, m_controller, &PlayerController::selectAllVisibleTracks);
    connect(
        m_nodeStartShortcut,
        &QShortcut::activated,
        this,
        &MainWindow::handleNodeStartShortcut);
    connect(
        m_nodeEndShortcut,
        &QShortcut::activated,
        this,
        &MainWindow::handleNodeEndShortcut);
    connect(m_showTimelineShortcut, &QShortcut::activated, m_showTimelineAction, &QAction::trigger);
    connect(m_trimNodeShortcut, &QShortcut::activated, this, &MainWindow::trimSelectedNodeToSound);
    connect(m_autoPanShortcut, &QShortcut::activated, this, &MainWindow::toggleSelectedNodeAutoPan);
    connect(m_audioPoolShortcut, &QShortcut::activated, m_audioPoolAction, &QAction::trigger);
    connect(m_toggleNodeNameShortcut, &QShortcut::activated, m_controller, &PlayerController::toggleSelectedTrackLabels);
    connect(m_deleteShortcut, &QShortcut::activated, this, &MainWindow::deleteFromFocusedPanel);
    connect(m_unselectAllShortcut, &QShortcut::activated, m_controller, &PlayerController::clearSelection);
    connect(m_controller, &PlayerController::frameReady, this, &MainWindow::updateFrame);
    connect(m_controller, &PlayerController::overlaysChanged, this, &MainWindow::refreshOverlays);
    connect(
        m_controller,
        &PlayerController::insertionFollowsPlaybackChanged,
        this,
        &MainWindow::updateInsertionFollowsPlaybackState);
    connect(m_controller, &PlayerController::playbackStateChanged, this, &MainWindow::updatePlaybackState);
    connect(
        m_controller,
        &PlayerController::motionTrackingChanged,
        this,
        &MainWindow::updateMotionTrackingState);
    connect(m_controller, &PlayerController::loopRangeChanged, this, &MainWindow::refreshTimeline);
    connect(m_controller, &PlayerController::selectionChanged, this, &MainWindow::updateSelectionState);
    connect(m_controller, &PlayerController::trackAvailabilityChanged, this, &MainWindow::updateTrackAvailabilityState);
    connect(m_controller, &PlayerController::editStateChanged, this, &MainWindow::updateEditActionState);
    connect(m_controller, &PlayerController::audioPoolChanged, this, &MainWindow::refreshAudioPool);
    connect(m_controller, &PlayerController::audioPoolPlaybackStateChanged, this, &MainWindow::updateAudioPoolPlaybackIndicators);
    connect(m_controller, &PlayerController::videoLoaded, this, &MainWindow::handleVideoLoaded);
    connect(m_controller, &PlayerController::videoAudioStateChanged, this, &MainWindow::updateVideoAudioRow);
    connect(m_controller, &PlayerController::statusChanged, this, &MainWindow::showStatus);
    connect(m_controller, &PlayerController::editStateChanged, this, [this]()
    {
        if (!m_projectStateChangeInProgress && hasOpenProject())
        {
            setProjectDirty(true);
        }
    });
    connect(m_controller, &PlayerController::audioPoolChanged, this, [this]()
    {
        if (!m_projectStateChangeInProgress && hasOpenProject())
        {
            setProjectDirty(true);
        }
    });
    connect(m_controller, &PlayerController::loopRangeChanged, this, [this]()
    {
        if (!m_projectStateChangeInProgress && hasOpenProject())
        {
            setProjectDirty(true);
        }
    });
    connect(m_controller, &PlayerController::videoAudioStateChanged, this, [this]()
    {
        if (!m_projectStateChangeInProgress && hasOpenProject())
        {
            setProjectDirty(true);
        }
    });
    connect(m_controller, &PlayerController::motionTrackingChanged, this, [this]()
    {
        if (!m_projectStateChangeInProgress && hasOpenProject())
        {
            setProjectDirty(true);
        }
    });
    connect(m_controller, &PlayerController::insertionFollowsPlaybackChanged, this, [this]()
    {
        if (!m_projectStateChangeInProgress && hasOpenProject())
        {
            setProjectDirty(true);
        }
    });
    connect(m_showMixShortcut, &QShortcut::activated, m_showMixAction, &QAction::trigger);
    connect(m_mixQuickController, &MixQuickController::masterGainChanged, m_controller, &PlayerController::setMasterMixGainDb);
    connect(m_mixQuickController, &MixQuickController::masterMutedChanged, m_controller, &PlayerController::setMasterMixMuted);
    connect(m_mixQuickController, &MixQuickController::laneGainChanged, m_controller, &PlayerController::setMixLaneGainDb);
    connect(m_mixQuickController, &MixQuickController::laneMutedChanged, m_controller, &PlayerController::setMixLaneMuted);
    connect(m_mixQuickController, &MixQuickController::laneSoloChanged, m_controller, &PlayerController::setMixLaneSoloed);
    connect(m_mixQuickController, &MixQuickController::masterGainChanged, this, [this](float)
    {
        if (!m_projectStateChangeInProgress && hasOpenProject())
        {
            setProjectDirty(true);
        }
    });
    connect(m_mixQuickController, &MixQuickController::masterMutedChanged, this, [this](bool)
    {
        if (!m_projectStateChangeInProgress && hasOpenProject())
        {
            setProjectDirty(true);
        }
    });
    connect(m_mixQuickController, &MixQuickController::laneGainChanged, this, [this](int, float)
    {
        if (!m_projectStateChangeInProgress && hasOpenProject())
        {
            setProjectDirty(true);
        }
    });
    connect(m_mixQuickController, &MixQuickController::laneMutedChanged, this, [this](int, bool)
    {
        if (!m_projectStateChangeInProgress && hasOpenProject())
        {
            setProjectDirty(true);
        }
    });
    connect(m_mixQuickController, &MixQuickController::laneSoloChanged, this, [this](int, bool)
    {
        refreshMixView();
        refreshTimeline();
        if (!m_projectStateChangeInProgress && hasOpenProject())
        {
            setProjectDirty(true);
        }
    });

    updatePlaybackState(false);
    updateInsertionFollowsPlaybackState(m_controller->isInsertionFollowsPlayback());
    if (m_showAllNodeNamesAction && m_showAllNodeNamesAction->isChecked())
    {
        m_videoViewportQuickController->setShowAllLabels(true);
        if (m_nativeViewport)
        {
            m_nativeViewport->setShowAllLabels(true);
        }
    }
    updateSelectionState(m_controller->hasSelection());
    updateTrackAvailabilityState(m_controller->hasTracks());
    updateEditActionState();
    updateMemoryUsage();
    updateDebugVisibility(m_debugVisible);
    updateDebugText();
    refreshAudioPool();
    updateVideoAudioRow();
    m_memoryUsageTimer.start();
    clearCurrentProject();
    QTimer::singleShot(0, this, [this]()
    {
        if (!hasOpenProject())
        {
            showStatus(QStringLiteral("Create or open a project to start adding nodes."));
        }
    });
}

MainWindow::~MainWindow() = default;

void MainWindow::setWindowTitle(const QString& title)
{
    setTitle(title);
}

QString MainWindow::windowTitle() const
{
    return title();
}

QString MainWindow::currentProjectTitle() const
{
    if (m_currentProjectName.isEmpty())
    {
        return QString{};
    }

    return m_currentProjectName + (m_projectDirty ? QStringLiteral("*") : QString{});
}

void MainWindow::setWindowIcon(const QIcon& windowIcon)
{
    setIcon(windowIcon);
}

QIcon MainWindow::windowIcon() const
{
    return icon();
}

bool MainWindow::isMaximized() const
{
    return visibility() == QWindow::Maximized;
}

QByteArray MainWindow::saveGeometry() const
{
    QByteArray state;
    QDataStream stream(&state, QIODevice::WriteOnly);
    stream << geometry();
    return state;
}

bool MainWindow::restoreGeometry(const QByteArray& geometryState)
{
    if (geometryState.isEmpty())
    {
        return false;
    }

    QRect restoredGeometry;
    QDataStream stream(geometryState);
    stream >> restoredGeometry;
    if (!restoredGeometry.isValid())
    {
        return false;
    }

    setGeometry(restoredGeometry);
    return true;
}

bool MainWindow::openProjectFilePath(const QString& projectFilePath)
{
    if (projectFilePath.isEmpty())
    {
        return false;
    }

    return m_projectWindowController->loadProjectFile(projectFilePath);
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (event)
    {
        const auto interactionEvent =
            event->type() == QEvent::MouseButtonPress
            || event->type() == QEvent::TouchBegin
            || event->type() == QEvent::FocusIn;
        if (interactionEvent)
        {
            if (watched == m_nodeEditorQuickWidget)
            {
                setPreferredPlaybackContext(PlaybackContext::NodeEditor);
            }
            else if (watched == m_timelineQuickWidget
                || watched == m_thumbnailStripQuickWidget
                || watched == m_videoViewportQuickWidget)
            {
                setPreferredPlaybackContext(PlaybackContext::Project);
            }
        }
    }

    if (watched == m_detachedVideoWindow && event && event->type() == QEvent::Close && m_videoDetached && !m_shuttingDown)
    {
        attachVideo();
        event->ignore();
        return true;
    }
    if (watched == m_detachedTimelineWindow && event && event->type() == QEvent::Close && m_timelineDetached && !m_shuttingDown)
    {
        attachTimeline();
        event->ignore();
        return true;
    }
    if (watched == m_detachedMixWindow && event && event->type() == QEvent::Close && m_mixDetached && !m_shuttingDown)
    {
        attachMix();
        event->ignore();
        return true;
    }
    if (watched == m_detachedAudioPoolWindow && event && event->type() == QEvent::Close && m_audioPoolDetached && !m_shuttingDown)
    {
        attachAudioPool();
        event->ignore();
        return true;
    }

    const auto nodeEditorFocused = [this]() -> bool
    {
        return nodeEditorHasFocus();
    };

    if (watched == m_nativeViewportWindow
        && (event->type() == QEvent::Hide || event->type() == QEvent::Close))
    {
        if (m_showNativeViewportAction && m_showNativeViewportAction->isChecked())
        {
            const QSignalBlocker blocker{m_showNativeViewportAction};
            m_showNativeViewportAction->setChecked(false);
        }
    }

    if (event->type() == QEvent::ShortcutOverride)
    {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (!keyEvent->isAutoRepeat()
            && keyEvent->key() == Qt::Key_Space
            && keyEvent->modifiers() == Qt::NoModifier
            && nodeEditorFocused())
        {
            event->accept();
            return true;
        }
        if (!keyEvent->isAutoRepeat()
            && (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter)
            && keyEvent->modifiers() == Qt::NoModifier
            && nodeEditorFocused())
        {
            event->accept();
            return true;
        }
        if (!keyEvent->isAutoRepeat()
            && keyEvent->key() == Qt::Key_Backspace
            && keyEvent->modifiers() == Qt::NoModifier
            && nodeEditorFocused())
        {
            event->accept();
            return true;
        }
        if (!keyEvent->isAutoRepeat()
            && (keyEvent->key() == Qt::Key_A || keyEvent->key() == Qt::Key_S)
            && keyEvent->modifiers() == Qt::NoModifier
            && nodeEditorFocused())
        {
            event->accept();
            return true;
        }
        if (!keyEvent->isAutoRepeat()
            && (keyEvent->key() == Qt::Key_C || keyEvent->key() == Qt::Key_X || keyEvent->key() == Qt::Key_V)
            && keyEvent->modifiers() == Qt::ControlModifier
            && nodeEditorFocused())
        {
            event->accept();
            return true;
        }
        if (!keyEvent->isAutoRepeat()
            && keyEvent->key() == Qt::Key_Tab
            && keyEvent->modifiers() == Qt::NoModifier
            && timelineHasFocus())
        {
            event->accept();
            return true;
        }
    }

    if (event->type() == QEvent::KeyRelease)
    {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (!keyEvent->isAutoRepeat()
            && keyEvent->key() == Qt::Key_Control
            && m_shellOverlayController
            && m_shellOverlayController->trackGainPopupVisible())
        {
            hideTrackMixGainPopup();
        }
    }

    if (event->type() == QEvent::KeyPress)
    {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (!keyEvent->isAutoRepeat()
            && keyEvent->key() == Qt::Key_Tab
            && keyEvent->modifiers() == Qt::NoModifier
            && timelineHasFocus())
        {
            selectNextVisibleNode();
            return true;
        }
        if (!keyEvent->isAutoRepeat()
            && keyEvent->key() == Qt::Key_Space
            && keyEvent->modifiers() == Qt::NoModifier
            && nodeEditorFocused())
        {
            if (m_nodeEditorPreviewSession)
            {
                m_nodeEditorPreviewSession->toggle();
            }
            return true;
        }
        if (!keyEvent->isAutoRepeat()
            && (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter)
            && keyEvent->modifiers() == Qt::NoModifier
            && nodeEditorFocused())
        {
            if (m_nodeEditorWorkspaceSession)
            {
                m_nodeEditorWorkspaceSession->resetPlayheadToStart();
            }
            return true;
        }
        if (!keyEvent->isAutoRepeat()
            && keyEvent->key() == Qt::Key_Backspace
            && keyEvent->modifiers() == Qt::NoModifier
            && nodeEditorFocused())
        {
            deleteFromFocusedPanel();
            return true;
        }
        if (!keyEvent->isAutoRepeat()
            && (keyEvent->key() == Qt::Key_A || keyEvent->key() == Qt::Key_S)
            && keyEvent->modifiers() == Qt::NoModifier
            && nodeEditorFocused())
        {
            if (m_nodeEditorWorkspaceSession)
            {
                m_nodeEditorWorkspaceSession->trimSelectedClipToPlayhead(keyEvent->key() == Qt::Key_A, hasOpenProject());
                syncNodeEditorActionAvailability();
            }
            return true;
        }
        if (!keyEvent->isAutoRepeat()
            && (keyEvent->key() == Qt::Key_C
                || keyEvent->key() == Qt::Key_X
                || keyEvent->key() == Qt::Key_V
                || keyEvent->key() == Qt::Key_E)
            && keyEvent->modifiers() == Qt::ControlModifier
            && nodeEditorFocused())
        {
            if (keyEvent->key() == Qt::Key_C)
            {
                requestNodeEditorEditAction(QStringLiteral("copyClip"));
            }
            else if (keyEvent->key() == Qt::Key_X)
            {
                requestNodeEditorEditAction(QStringLiteral("cutClip"));
            }
            else if (keyEvent->key() == Qt::Key_E)
            {
                requestNodeEditorEditAction(QStringLiteral("splitClip"));
            }
            else
            {
                requestNodeEditorEditAction(QStringLiteral("pasteClip"));
            }
            return true;
        }
    }

    if (!shouldIgnoreNodeMovementShortcuts())
    {
        if (event->type() == QEvent::KeyPress)
        {
            auto* keyEvent = static_cast<QKeyEvent*>(event);
            if (!keyEvent->isAutoRepeat() && keyEvent->modifiers() == Qt::NoModifier)
            {
                switch (keyEvent->key())
                {
                case Qt::Key_Up:
                case Qt::Key_Left:
                case Qt::Key_Down:
                case Qt::Key_Right:
                    beginHeldNodeNudge(keyEvent->key());
                    return true;
                default:
                    break;
                }
            }
        }
        else if (event->type() == QEvent::KeyRelease)
        {
            auto* keyEvent = static_cast<QKeyEvent*>(event);
            if (!keyEvent->isAutoRepeat() && keyEvent->modifiers() == Qt::NoModifier)
            {
                switch (keyEvent->key())
                {
                case Qt::Key_Up:
                case Qt::Key_Left:
                case Qt::Key_Down:
                case Qt::Key_Right:
                    endHeldNodeNudge(keyEvent->key());
                    return true;
                default:
                    break;
                }
            }
        }
    }

    if (event->type() == QEvent::KeyPress)
    {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (!keyEvent->isAutoRepeat())
        {
            const auto modifiers = keyEvent->modifiers();
            const auto key = keyEvent->key();

            if (key == Qt::Key_A && modifiers == (Qt::ControlModifier | Qt::ShiftModifier))
            {
                armClearAllShortcut();
                return true;
            }

            if (key == Qt::Key_Backspace && m_clearAllShortcutArmed)
            {
                clearPendingClearAllShortcut();
                if (m_controller->hasTracks())
                {
                    m_controller->clearAllTracks();
                }
                return true;
            }

            if (key == Qt::Key_Backspace
                && timelineHasSelectedLoopRange()
                && !m_controller->loopRanges().empty())
            {
                clearLoopRange();
                return true;
            }

            if (m_clearAllShortcutArmed
                && key != Qt::Key_Control
                && key != Qt::Key_Shift
                && key != Qt::Key_Alt
                && key != Qt::Key_Meta)
            {
                clearPendingClearAllShortcut();
            }
        }
    }

    if (watched == m_videoViewportQuickWidget && event)
    {
        switch (event->type())
        {
        case QEvent::DragEnter:
        {
            auto* dragEvent = static_cast<QDragEnterEvent*>(event);
            if (m_videoViewportQuickController->hasFrame() && !audioPathFromMimeData(dragEvent->mimeData()).isEmpty())
            {
                dragEvent->setDropAction(Qt::CopyAction);
                dragEvent->acceptProposedAction();
                return true;
            }
            break;
        }
        case QEvent::DragMove:
        {
            auto* dragEvent = static_cast<QDragMoveEvent*>(event);
            if (m_videoViewportQuickController->hasFrame() && !audioPathFromMimeData(dragEvent->mimeData()).isEmpty())
            {
                dragEvent->setDropAction(Qt::CopyAction);
                dragEvent->acceptProposedAction();
                return true;
            }
            break;
        }
        case QEvent::Drop:
        {
            auto* dropEvent = static_cast<QDropEvent*>(event);
            if (!m_videoViewportQuickController->hasFrame())
            {
                break;
            }

            const auto assetPath = audioPathFromMimeData(dropEvent->mimeData());
            if (assetPath.isEmpty())
            {
                break;
            }

            const auto imagePoint = m_videoViewportQuickController->widgetToImagePoint(
                dropEvent->position().x(),
                dropEvent->position().y(),
                m_videoViewportQuickWidget->width(),
                m_videoViewportQuickWidget->height());
            requestAudioDropped(
                assetPath,
                imagePoint.value(QStringLiteral("x")).toDouble(),
                imagePoint.value(QStringLiteral("y")).toDouble());
            dropEvent->setDropAction(Qt::CopyAction);
            dropEvent->acceptProposedAction();
            return true;
        }
        default:
            break;
        }
    }

    return QQuickView::eventFilter(watched, event);
}

void MainWindow::resizeEvent(QResizeEvent* event)
{
    QQuickView::resizeEvent(event);
    syncShellLayoutViewport();
    updateOverlayPositions();
}

#ifdef Q_OS_WIN
bool MainWindow::nativeEvent(const QByteArray& eventType, void* message, qintptr* result)
{
    const auto* msg = static_cast<MSG*>(message);
    if (!msg || !result)
    {
        return false;
    }

    if (msg->message == WM_NCHITTEST)
    {
        if (isMaximized())
        {
            return false;
        }

        const auto borderWidth =
            GetSystemMetrics(SM_CXSIZEFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER);
        RECT windowRect{};
        GetWindowRect(HWND(winId()), &windowRect);

        const auto x = GET_X_LPARAM(msg->lParam);
        const auto y = GET_Y_LPARAM(msg->lParam);
        const bool onLeft = x >= windowRect.left && x < windowRect.left + borderWidth;
        const bool onRight = x <= windowRect.right && x > windowRect.right - borderWidth;
        const bool onTop = y >= windowRect.top && y < windowRect.top + borderWidth;
        const bool onBottom = y <= windowRect.bottom && y > windowRect.bottom - borderWidth;

        if (onTop && onLeft)
        {
            *result = HTTOPLEFT;
            return true;
        }
        if (onTop && onRight)
        {
            *result = HTTOPRIGHT;
            return true;
        }
        if (onBottom && onLeft)
        {
            *result = HTBOTTOMLEFT;
            return true;
        }
        if (onBottom && onRight)
        {
            *result = HTBOTTOMRIGHT;
            return true;
        }
        if (onTop)
        {
            *result = HTTOP;
            return true;
        }
        if (onBottom)
        {
            *result = HTBOTTOM;
            return true;
        }
        if (onLeft)
        {
            *result = HTLEFT;
            return true;
        }
        if (onRight)
        {
            *result = HTRIGHT;
            return true;
        }
    }

    return QQuickView::nativeEvent(eventType, message, result);
}
#endif

void MainWindow::closeEvent(QCloseEvent* event)
{
    qInfo().noquote()
        << "MainWindow::closeEvent begin:"
        << "accepted=" << event->isAccepted()
        << "visible=" << isVisible()
        << "geometry=" << geometry()
        << "projectDirty=" << m_projectDirty;
    if (!promptToSaveIfDirty(QStringLiteral("close the app")))
    {
        qInfo().noquote() << "MainWindow::closeEvent ignored by dirty-project prompt.";
        event->ignore();
        return;
    }

    if (hasOpenProject() && !m_projectDirty && !saveProjectToCurrentPath())
    {
        qInfo().noquote() << "MainWindow::closeEvent ignored because project save failed.";
        event->ignore();
        return;
    }

    m_shuttingDown = true;
    if (m_nodeEditorPreviewSession)
    {
        m_nodeEditorPreviewSession->stop();
    }
    if (m_nodeEditorWorkspaceSession)
    {
        m_nodeEditorWorkspaceSession->bindShellRootItem(nullptr);
    }
    if (m_timelineThumbnailGenerationController)
    {
        m_timelineThumbnailGenerationController->shutdown();
    }
    m_shellRootItem = nullptr;
    setSource({});
    QQuickView::closeEvent(event);
    qInfo().noquote()
        << "MainWindow::closeEvent after base:"
        << "accepted=" << event->isAccepted()
        << "visible=" << isVisible();
    if (event->isAccepted())
    {
        return;
    }

    m_shuttingDown = false;
}

bool MainWindow::hasOpenProject() const
{
    return m_projectWindowController->hasOpenProject();
}

void MainWindow::clearCurrentProject()
{
    m_projectWindowController->clearCurrentProject();
}

void MainWindow::setCurrentProject(const QString& projectFilePath, const QString& projectName)
{
    m_projectWindowController->setCurrentProject(projectFilePath, projectName);
}

QStringList MainWindow::recentProjectPaths() const
{
    return m_projectWindowController->recentProjectPaths();
}

void MainWindow::storeRecentProjectPaths(const QStringList& projectPaths)
{
    m_projectWindowController->storeRecentProjectPaths(projectPaths);
}

void MainWindow::addRecentProjectPath(const QString& projectFilePath)
{
    m_projectWindowController->addRecentProjectPath(projectFilePath);
}

void MainWindow::removeRecentProjectPath(const QString& projectFilePath)
{
    m_projectWindowController->removeRecentProjectPath(projectFilePath);
}

void MainWindow::rebuildRecentProjectsMenu()
{
    m_projectWindowController->rebuildRecentProjectsMenu();
}

void MainWindow::setProjectDirty(const bool dirty)
{
    m_projectWindowController->setProjectDirty(dirty);
}

void MainWindow::updateWindowTitle()
{
    m_projectWindowController->updateWindowTitle();
}

bool MainWindow::promptToSaveIfDirty(const QString& actionLabel)
{
    return m_projectWindowController->promptToSaveIfDirty(actionLabel);
}

bool MainWindow::ensureProjectForMediaAction(const QString& actionLabel)
{
    return m_mediaImportController->ensureProjectForMediaAction(actionLabel);
}

QString MainWindow::chooseOpenFileName(
    const QString& title,
    const QString& directory,
    const QString& filter) const
{
    return m_mediaImportController->chooseOpenFileName(title, directory, filter);
}

QString MainWindow::chooseExistingDirectory(const QString& title, const QString& directory) const
{
    return m_mediaImportController->chooseExistingDirectory(title, directory);
}

std::optional<QString> MainWindow::copyMediaIntoProject(
    const QString& sourcePath,
    const QString& subdirectory,
    QString* errorMessage) const
{
    return m_mediaImportController->copyMediaIntoProject(sourcePath, subdirectory, errorMessage);
}

dawg::project::UiState MainWindow::snapshotProjectUiState() const
{
    return m_panelLayoutController->snapshotProjectUiState();
}

void MainWindow::applyProjectUiState(const dawg::project::UiState& state)
{
    m_panelLayoutController->applyProjectUiState(state);
}

bool MainWindow::saveProjectToPath(const QString& projectFilePath, const QString& projectName)
{
    return m_projectWindowController->saveProjectToPath(projectFilePath, projectName);
}

bool MainWindow::saveProjectToCurrentPath()
{
    return m_projectWindowController->saveProjectToCurrentPath();
}

bool MainWindow::createProjectAt(const QString& projectName, const QString& parentDirectory)
{
    return m_projectWindowController->createProjectAt(projectName, parentDirectory);
}

bool MainWindow::openProjectFileWithPrompt(const QString& projectFilePath, const QString& actionLabel)
{
    return m_projectWindowController->openProjectFileWithPrompt(projectFilePath, actionLabel);
}

bool MainWindow::loadProjectFile(const QString& projectFilePath)
{
    return m_projectWindowController->loadProjectFile(projectFilePath);
}

bool MainWindow::saveProjectAsNewCopy()
{
    return m_projectWindowController->saveProjectAsNewCopy();
}

void MainWindow::restoreLastProjectOnStartup()
{
    m_projectWindowController->restoreLastProjectOnStartup();
}

void MainWindow::newProject()
{
    m_projectWindowController->newProject();
}

void MainWindow::openProject()
{
    m_projectWindowController->openProject();
}

void MainWindow::saveProject()
{
    m_projectWindowController->saveProject();
}

void MainWindow::saveProjectAs()
{
    m_projectWindowController->saveProjectAs();
}

void MainWindow::requestImportVideo()
{
    openVideo();
}

void MainWindow::requestSeedPoint(const double imageX, const double imageY)
{
    m_controller->seedTrack(QPointF{imageX, imageY});
}

void MainWindow::requestSelectedTrackMoved(const double imageX, const double imageY)
{
    m_controller->moveSelectedTrack(QPointF{imageX, imageY});
}

void MainWindow::requestTrackSelected(const QString& trackId)
{
    const QUuid uuid(trackId);
    if (!uuid.isNull())
    {
        m_controller->selectTrack(uuid);
    }
}

void MainWindow::requestTrackActivated(const QString& trackId)
{
    const QUuid uuid(trackId);
    if (!uuid.isNull())
    {
        m_controller->selectTrack(uuid);
        if (m_showNodeEditorAction)
        {
            m_showNodeEditorAction->setChecked(true);
        }
    }
}

void MainWindow::requestTracksSelected(const QVariantList& trackIds)
{
    QList<QUuid> uuids;
    uuids.reserve(trackIds.size());
    for (const auto& trackId : trackIds)
    {
        const QUuid uuid(trackId.toString());
        if (!uuid.isNull())
        {
            uuids.push_back(uuid);
        }
    }

    if (uuids.isEmpty())
    {
        m_controller->clearSelection();
        return;
    }

    m_controller->selectTracks(uuids);
}

void MainWindow::requestNodeEditorFileAction(const QString& actionKey)
{
    if (m_nodeEditorWorkspaceSession)
    {
        m_nodeEditorWorkspaceSession->handleFileAction(actionKey, hasOpenProject(), m_currentProjectRootPath);
        syncNodeEditorActionAvailability();
    }
}

void MainWindow::requestNodeEditorAudioAction(const QString& actionKey)
{
    if (m_nodeEditorWorkspaceSession)
    {
        m_nodeEditorWorkspaceSession->handleAudioAction(actionKey, hasOpenProject(), m_currentProjectRootPath);
        syncNodeEditorActionAvailability();
    }
}

void MainWindow::requestNodeEditorEditAction(const QString& actionKey)
{
    if (m_nodeEditorWorkspaceSession)
    {
        m_nodeEditorWorkspaceSession->handleEditAction(actionKey, hasOpenProject());
        syncNodeEditorActionAvailability();
    }
}

void MainWindow::requestTrackGainPopup(const QString& trackId, const double localX, const double localY)
{
    const QUuid uuid(trackId);
    if (!uuid.isNull())
    {
        const QPoint globalPosition = mapQuickLocalToGlobal(localX, localY);
        showTrackMixGainPopup(uuid, globalPosition);
    }
}

void MainWindow::requestTrackGainAdjust(
    const QString& trackId,
    const int wheelDelta,
    const double localX,
    const double localY)
{
    const QUuid uuid(trackId);
    if (!uuid.isNull())
    {
        const QPoint globalPosition = mapQuickLocalToGlobal(localX, localY);
        adjustTrackMixGainFromWheel(uuid, wheelDelta, globalPosition);
    }
}

void MainWindow::requestTrackContextMenu(const QString& trackId, const double localX, const double localY)
{
    const QUuid uuid(trackId);
    if (!uuid.isNull())
    {
        const QPoint globalPosition = mapQuickLocalToGlobal(localX, localY);
        showNodeContextMenu(uuid, globalPosition, true);
    }
}

void MainWindow::requestAudioDropped(const QString& assetPath, const double imageX, const double imageY)
{
    m_controller->createTrackWithAudioAtCurrentFrame(assetPath, QPointF{imageX, imageY});
}

void MainWindow::requestSelectNextNode()
{
    selectNextVisibleNode();
}

void MainWindow::requestSelectNextVisibleNode()
{
    m_controller->selectNextVisibleTrack();
}

QPoint MainWindow::mapQuickLocalToGlobal(const double localX, const double localY) const
{
    if (!m_videoViewportQuickWidget)
    {
        return {};
    }

    const auto scenePoint = m_videoViewportQuickWidget->mapToScene(QPointF(localX, localY));
    return mapToGlobal(scenePoint.toPoint());
}

void MainWindow::openVideo()
{
    m_mediaImportController->openVideo();
}

void MainWindow::importAudioToPool()
{
    m_mediaImportController->importAudioToPool();
}

void MainWindow::handleLoopStartShortcut()
{
    if (!m_timelineQuickController)
    {
        return;
    }

    if (const auto targetFrame = timelineLoopTargetFrame(); targetFrame.has_value()
        && m_timelineQuickController->selectedLoopIndex() >= 0)
    {
        m_controller->setLoopStartFrame(m_timelineQuickController->selectedLoopIndex(), *targetFrame);
        m_pendingLoopShortcutStartFrame.reset();
        m_pendingLoopShortcutEndFrame.reset();
        m_timelineQuickController->setPendingLoopDraftFrame(std::nullopt);
        return;
    }

    if (const auto targetFrame = timelineLoopTargetFrame(); targetFrame.has_value())
    {
        m_pendingLoopShortcutStartFrame = *targetFrame;
        m_timelineQuickController->setPendingLoopDraftFrame(*targetFrame);
        if (m_pendingLoopShortcutEndFrame.has_value())
        {
            createLoopRangeFromShortcutFrames(*m_pendingLoopShortcutStartFrame, *m_pendingLoopShortcutEndFrame);
        }
    }
}

void MainWindow::handleLoopEndShortcut()
{
    if (!m_timelineQuickController)
    {
        return;
    }

    if (const auto targetFrame = timelineLoopTargetFrame(); targetFrame.has_value()
        && m_timelineQuickController->selectedLoopIndex() >= 0)
    {
        m_controller->setLoopEndFrame(m_timelineQuickController->selectedLoopIndex(), *targetFrame);
        m_pendingLoopShortcutStartFrame.reset();
        m_pendingLoopShortcutEndFrame.reset();
        m_timelineQuickController->setPendingLoopDraftFrame(std::nullopt);
        return;
    }

    if (const auto targetFrame = timelineLoopTargetFrame(); targetFrame.has_value())
    {
        m_pendingLoopShortcutEndFrame = *targetFrame;
        if (m_pendingLoopShortcutStartFrame.has_value())
        {
            createLoopRangeFromShortcutFrames(*m_pendingLoopShortcutStartFrame, *m_pendingLoopShortcutEndFrame);
        }
        else
        {
            m_timelineQuickController->setPendingLoopDraftFrame(std::nullopt);
        }
    }
}

void MainWindow::clearLoopRange()
{
    if (!m_timelineQuickController)
    {
        return;
    }

    const auto selectedLoopIndex = m_timelineQuickController->selectedLoopIndex();
    if (selectedLoopIndex >= 0)
    {
        m_controller->removeLoopRange(selectedLoopIndex);
    }
}

void MainWindow::handleNodeStartShortcut()
{
    if (nodeEditorHasFocus())
    {
        if (m_nodeEditorWorkspaceSession)
        {
            m_nodeEditorWorkspaceSession->trimSelectedClipToPlayhead(true, hasOpenProject());
            syncNodeEditorActionAvailability();
        }
        return;
    }

    if (timelineHasFocus() || timelineLoopShortcutFrame().has_value())
    {
        handleLoopStartShortcut();
        return;
    }

    if (shouldApplyNodeShortcutToAll())
    {
        m_controller->setAllTracksStartToCurrentFrame();
        return;
    }

    m_controller->setSelectedTrackStartToCurrentFrame();
}

void MainWindow::handleNodeEndShortcut()
{
    if (nodeEditorHasFocus())
    {
        if (m_nodeEditorWorkspaceSession)
        {
            m_nodeEditorWorkspaceSession->trimSelectedClipToPlayhead(false, hasOpenProject());
            syncNodeEditorActionAvailability();
        }
        return;
    }

    if (timelineHasFocus() || timelineLoopShortcutFrame().has_value())
    {
        handleLoopEndShortcut();
        return;
    }

    if (shouldApplyNodeShortcutToAll())
    {
        m_controller->setAllTracksEndToCurrentFrame();
        return;
    }

    m_controller->setSelectedTrackEndToCurrentFrame();
}

void MainWindow::trimSelectedNodeToSound()
{
    m_controller->trimSelectedTracksToAttachedSound();
}

void MainWindow::toggleSelectedNodeAutoPan()
{
    m_controller->toggleSelectedTrackAutoPan();
    if (m_autoPanAction)
    {
        const QSignalBlocker blocker{m_autoPanAction};
        m_autoPanAction->setChecked(m_controller->hasSelection() && m_controller->selectedTracksAutoPanEnabled());
    }
}

void MainWindow::copySelectedNode()
{
    m_controller->copySelectedTracks();
    updateEditActionState();
}

void MainWindow::pasteNode()
{
    m_controller->pasteCopiedTracksAtCurrentFrame();
    updateEditActionState();
}

void MainWindow::cutSelectedNode()
{
    m_controller->cutSelectedTracks();
    updateEditActionState();
}

void MainWindow::undoNodeEdit()
{
    m_controller->undoLastTrackEdit();
    updateEditActionState();
}

void MainWindow::redoNodeEdit()
{
    m_controller->redoLastTrackEdit();
    updateEditActionState();
}

void MainWindow::deleteAllEmptyNodes()
{
    const auto emptyNodeCount = m_controller->emptyTrackCount();
    if (emptyNodeCount <= 0)
    {
        showStatus(QStringLiteral("There are no empty grey nodes to delete."));
        return;
    }

    const auto choice = m_dialogController->execMessage(
        QStringLiteral("Delete All Empty Nodes"),
        QStringLiteral("Are you sure?"),
        QStringLiteral("This will delete all %1 empty grey node(s) in the project. Nodes with attached audio will be kept.")
            .arg(emptyNodeCount),
        {DialogController::Button::Yes, DialogController::Button::Cancel},
        DialogController::Button::Cancel);
    if (choice != DialogController::Button::Yes)
    {
        return;
    }

    m_controller->deleteAllEmptyTracks();
    updateEditActionState();
}

void MainWindow::selectNextVisibleNode()
{
    if (timelineHasFocus())
    {
        m_controller->selectNextTimelineTrack();
        return;
    }

    m_controller->selectNextVisibleTrack();
}

void MainWindow::moveSelectedNodeUp()
{
    nudgeSelectedNode(QPointF{0.0, -8.0});
}

void MainWindow::moveSelectedNodeDown()
{
    nudgeSelectedNode(QPointF{0.0, 8.0});
}

void MainWindow::moveSelectedNodeLeft()
{
    nudgeSelectedNode(QPointF{-8.0, 0.0});
}

void MainWindow::moveSelectedNodeRight()
{
    nudgeSelectedNode(QPointF{8.0, 0.0});
}

void MainWindow::updateFrame(const QImage& image, const int frameIndex, const double timestampSeconds)
{
    m_debugUiController->updateFrame(image, frameIndex, timestampSeconds);
    if (m_nodeEditorPreviewSession)
    {
        m_nodeEditorPreviewSession->handleFrameAdvanced(frameIndex);
    }
    if (m_controller
        && m_controller->isPlaying()
        && m_audioPoolQuickWidget
        && m_audioPoolQuickWidget->isVisible()
        && (!m_audioPoolPlaybackRefreshTimer.isValid()
            || m_audioPoolPlaybackRefreshTimer.elapsed() >= kAudioPoolPlaybackRefreshIntervalMs))
    {
        updateAudioPoolPlaybackIndicators();
        m_audioPoolPlaybackRefreshTimer.restart();
    }
}

void MainWindow::updateMemoryUsage()
{
    m_debugUiController->updateMemoryUsage();
}

void MainWindow::updateDebugText()
{
    m_debugUiController->updateDebugText();
}

void MainWindow::refreshOverlays()
{
    m_videoViewportQuickController->setOverlays(m_controller->currentOverlays());
    if (m_detachedVideoViewportQuickController)
    {
        m_detachedVideoViewportQuickController->setOverlays(m_controller->currentOverlays());
    }
    if (m_nativeViewportWindow && m_nativeViewportWindow->isVisible() && m_nativeViewport)
    {
        m_nativeViewport->setOverlays(m_controller->currentOverlays());
    }

    if (m_controller && m_controller->isPlaying())
    {
        return;
    }

    refreshTimeline();
}

void MainWindow::updateInsertionFollowsPlaybackState(const bool enabled)
{
    if (m_insertionFollowsPlaybackAction && m_insertionFollowsPlaybackAction->isChecked() != enabled)
    {
        m_insertionFollowsPlaybackAction->setChecked(enabled);
    }
    if (m_nodeEditorQuickController)
    {
        m_nodeEditorQuickController->setInsertionFollowsPlayback(enabled);
    }
    updateDebugText();
}

void MainWindow::updatePlaybackState(const bool playing)
{
    clearStuckWaitCursor(this);
    if (m_nodeEditorPreviewSession)
    {
        m_nodeEditorPreviewSession->handlePlaybackStateChanged(playing);
    }
    if (m_mixQuickController)
    {
        m_mixQuickController->setPlaybackActive(playing);
    }
    if (m_timelineQuickController)
    {
        m_timelineQuickController->setPlaybackActive(playing);
    }
    if (m_thumbnailStripQuickController)
    {
        m_thumbnailStripQuickController->setPlaybackActive(playing);
    }
    const auto label = playing ? QStringLiteral("Pause (Space)") : QStringLiteral("Play (Space)");
    m_playAction->setText(label);
    m_debugTextTimer.invalidate();
    if (playing)
    {
        resetOutputFpsTracking();
        m_outputFpsTimer.start();
        m_audioPoolPlaybackRefreshTimer.invalidate();
    }
    else
    {
        resetOutputFpsTracking();
        m_audioPoolPlaybackRefreshTimer.invalidate();
    }
    m_timelinePlaybackUiTimer.invalidate();
    m_lastTimelinePlaybackUiFrame = -1;
    if (playing)
    {
        hideCanvasTipsOverlay();
    }
    if (m_audioPoolQuickWidget && m_audioPoolQuickWidget->isVisible())
    {
        updateAudioPoolPlaybackIndicators();
    }
    if (m_timelineQuickController && m_timelineClickSeeksAction)
    {
        setTimelineSeekOnClickEnabled(m_timelineClickSeeksAction->isChecked() || !playing);
    }
    updateDebugText();
}

void MainWindow::updateMotionTrackingState(const bool enabled)
{
    m_motionTrackingAction->setChecked(enabled);
    updateDebugText();
}

void MainWindow::updateSelectionState(const bool hasSelection)
{
    m_actionsController->updateSelectionState(hasSelection);
    syncThumbnailStripSelectedNodeRange();
    syncNodeEditorActionAvailability();
    if (m_nodeEditorWorkspaceSession)
    {
        m_nodeEditorWorkspaceSession->refresh(hasOpenProject());
    }
}

void MainWindow::updateTrackAvailabilityState(const bool hasTracks)
{
    m_actionsController->updateTrackAvailabilityState(hasTracks);
    Q_UNUSED(hasTracks);
    syncNodeEditorActionAvailability();
}

void MainWindow::handleVideoLoaded(const QString& filePath, const int totalFrames, const double fps)
{
    m_debugUiController->handleVideoLoaded(filePath, totalFrames, fps);
}

void MainWindow::updateDebugVisibility(const bool enabled)
{
    m_debugUiController->updateDebugVisibility(enabled);
}

void MainWindow::updateNativeViewportVisibility(const bool visible)
{
    m_debugUiController->updateNativeViewportVisibility(visible);
}

void MainWindow::updateAudioPoolVisibility(const bool visible)
{
    m_panelLayoutController->updateAudioPoolVisibility(visible);
}

void MainWindow::updateTimelineVisibility(const bool visible)
{
    m_panelLayoutController->updateTimelineVisibility(visible);
}

void MainWindow::updateNodeEditorVisibility(const bool visible)
{
    if (!visible)
    {
        if (m_nodeEditorPreviewSession)
        {
            m_nodeEditorPreviewSession->stop();
        }
    }
    m_panelLayoutController->updateNodeEditorVisibility(visible);
}

void MainWindow::updateMixVisibility(const bool visible)
{
    m_panelLayoutController->updateMixVisibility(visible);
}

void MainWindow::detachVideo()
{
    m_panelLayoutController->detachVideo();
}

void MainWindow::attachVideo()
{
    m_panelLayoutController->attachVideo();
}

void MainWindow::detachTimeline()
{
    m_panelLayoutController->detachTimeline();
}

void MainWindow::attachTimeline()
{
    m_panelLayoutController->attachTimeline();
}

void MainWindow::detachMix()
{
    m_panelLayoutController->detachMix();
}

void MainWindow::attachMix()
{
    m_panelLayoutController->attachMix();
}

void MainWindow::detachAudioPool()
{
    m_panelLayoutController->detachAudioPool();
}

void MainWindow::attachAudioPool()
{
    m_panelLayoutController->attachAudioPool();
}

void MainWindow::updateDetachedVideoUiState()
{
    m_panelLayoutController->updateDetachedVideoUiState();
}

void MainWindow::updateDetachedPanelUiState()
{
    m_panelLayoutController->updateDetachedPanelUiState();
}

void MainWindow::resetOutputFpsTracking()
{
    m_debugUiController->resetOutputFpsTracking();
}

void MainWindow::syncMainVerticalPanelSizes()
{
    m_panelLayoutController->syncMainVerticalPanelSizes();
}

void MainWindow::refreshAudioPool()
{
    if (!m_audioPoolQuickController)
    {
        return;
    }

    m_audioPoolQuickController->replaceItems(m_controller->audioPoolItems());
}

void MainWindow::updateAudioPoolPlaybackIndicators()
{
    if (!m_audioPoolQuickController)
    {
        return;
    }

    m_audioPoolQuickController->updatePlaybackState(m_controller->audioPoolItems());
}

void MainWindow::updateVideoAudioRow()
{
    if (!m_audioPoolQuickController)
    {
        return;
    }

    const auto hasVideoAudio = m_controller->hasEmbeddedVideoAudio();
    const auto displayName = hasVideoAudio ? m_controller->embeddedVideoAudioDisplayName() : QString{};
    const auto activePlaybackPath = m_controller->loadedPath();
    const auto proxyPath = m_controller->proxyVideoPath();
    const auto usingProxy =
        !activePlaybackPath.isEmpty()
        && !proxyPath.isEmpty()
        && pathsMatch(activePlaybackPath, proxyPath);
    const auto decodeBackend = m_controller->decoderBackendName();
    const auto decodeDetail =
        decodeBackend.isEmpty()
            ? QString{}
            : QStringLiteral("%1 | %2")
                  .arg(usingProxy ? QStringLiteral("Proxy") : QStringLiteral("Original"))
                  .arg(decodeBackend);
    const auto tooltip =
        hasVideoAudio
            ? QStringLiteral("Embedded audio from %1%2%3")
                .arg(displayName)
                .arg(decodeDetail.isEmpty() ? QString{} : QStringLiteral("\nDecode: %1").arg(decodeDetail))
                .arg(m_controller->isFastPlaybackEnabled() ? QStringLiteral("\nLow-Res Playback enabled") : QString{})
            : QString{};
    m_audioPoolQuickController->syncVideoAudioState(
        hasVideoAudio,
        displayName,
        decodeDetail,
        tooltip,
        m_controller->isEmbeddedVideoAudioMuted(),
        m_controller->isFastPlaybackEnabled());
}

void MainWindow::showStatus(const QString& message)
{
    if (!m_shellOverlayController || message.trimmed().isEmpty())
    {
        return;
    }

    m_shellOverlayController->showStatus(message);
    m_statusToastTimer.start();
}

void MainWindow::nudgeSelectedNode(const QPointF& delta)
{
    if (delta.isNull())
    {
        return;
    }

    m_controller->nudgeSelectedTracks(delta);
}

void MainWindow::beginHeldNodeNudge(const int key)
{
    QPointF delta;
    switch (key)
    {
    case Qt::Key_Up:
        delta = QPointF{0.0, -8.0};
        break;
    case Qt::Key_Left:
        delta = QPointF{-8.0, 0.0};
        break;
    case Qt::Key_Down:
        delta = QPointF{0.0, 8.0};
        break;
    case Qt::Key_Right:
        delta = QPointF{8.0, 0.0};
        break;
    default:
        return;
    }

    m_activeNodeNudgeKey = key;
    m_activeNodeNudgeDelta = delta;
    m_nodeNudgeFastMode = false;
    nudgeSelectedNode(delta);
    m_nodeNudgeTimer.start();
}

void MainWindow::endHeldNodeNudge(const int key)
{
    if (m_activeNodeNudgeKey != key)
    {
        return;
    }

    m_activeNodeNudgeKey = 0;
    m_activeNodeNudgeDelta = {};
    m_nodeNudgeFastMode = false;
    m_nodeNudgeTimer.stop();
    m_nodeNudgeTimer.setInterval(220);
}

void MainWindow::applyHeldNodeNudge()
{
    if (m_activeNodeNudgeKey == 0 || m_activeNodeNudgeDelta.isNull())
    {
        m_nodeNudgeTimer.stop();
        return;
    }

    if (!m_nodeNudgeFastMode)
    {
        m_nodeNudgeFastMode = true;
        m_nodeNudgeTimer.setInterval(24);
    }

    nudgeSelectedNode(m_activeNodeNudgeDelta);
}

bool MainWindow::shouldIgnoreNodeMovementShortcuts() const
{
    if (!m_controller || !m_controller->hasSelection())
    {
        return true;
    }

    if ((m_dialogController && m_dialogController->visible())
        || (m_filePickerController && m_filePickerController->visible()))
    {
        return true;
    }

    const auto* focused = activeFocusItem();
    if (!focused)
    {
        return false;
    }

    const auto className = QString::fromLatin1(focused->metaObject()->className());
    return className.contains(QStringLiteral("Text"), Qt::CaseInsensitive)
        || className.contains(QStringLiteral("Input"), Qt::CaseInsensitive)
        || className.contains(QStringLiteral("Spin"), Qt::CaseInsensitive)
        || className.contains(QStringLiteral("Combo"), Qt::CaseInsensitive);
}

void MainWindow::updateOverlayPositions()
{
    if (!m_shellOverlayController || !m_contentAreaItem)
    {
        return;
    }

    m_shellOverlayController->setStatusMaxWidth(
        std::max(220, static_cast<int>(std::lround(m_contentAreaItem->width() * 0.45))));
    m_shellOverlayController->setCanvasTipsPosition(16, 16);
    m_shellOverlayController->setCanvasTipsMaxWidth(
        std::max(220, static_cast<int>(std::lround(m_contentAreaItem->width() * 0.28))));
}

void MainWindow::showCanvasTipsOverlay()
{
    if (!m_shellOverlayController)
    {
        return;
    }

    m_shellOverlayController->showCanvasTips(QStringLiteral(
        "Left-click to add or select nodes\n"
        "Right-click a node for options\n"
        "Drag audio from Audio Pool onto the video\n"
        "Space plays, , and . step frames"));
    m_canvasTipsTimer.start();
}

void MainWindow::hideCanvasTipsOverlay()
{
    m_canvasTipsTimer.stop();
    if (m_shellOverlayController)
    {
        m_shellOverlayController->hideCanvasTips();
    }
}

void MainWindow::showNodeContextMenu(const QUuid& trackId, const QPoint& globalPosition, const bool includeSoundActions)
{
    if (trackId.isNull())
    {
        return;
    }

    m_controller->selectTrack(trackId);

    const auto nodeLabel = m_controller->trackLabel(trackId).isEmpty()
        ? QStringLiteral("Node")
        : m_controller->trackLabel(trackId);
    const auto hasAttachedAudio = m_controller->trackHasAttachedAudio(trackId);
    if (!m_contentAreaItem || !m_contextMenuController)
    {
        return;
    }

    QVariantList items;
    items.push_back(QVariantMap{
        {QStringLiteral("key"), QStringLiteral("node.openEditor")},
        {QStringLiteral("text"), QStringLiteral("Open Node Editor")},
        {QStringLiteral("enabled"), true},
        {QStringLiteral("checkable"), false},
        {QStringLiteral("checked"), false},
        {QStringLiteral("separator"), false}});
    items.push_back(QVariantMap{
        {QStringLiteral("key"), QStringLiteral("node.rename")},
        {QStringLiteral("text"), QStringLiteral("Rename Node...")},
        {QStringLiteral("enabled"), true},
        {QStringLiteral("checkable"), false},
        {QStringLiteral("checked"), false},
        {QStringLiteral("separator"), false}});
    items.push_back(QVariantMap{{QStringLiteral("separator"), true}});
    if (hasAttachedAudio)
    {
        items.push_back(QVariantMap{
            {QStringLiteral("key"), QStringLiteral("node.trim")},
            {QStringLiteral("text"), QStringLiteral("Trim Node (Shift+T)")},
            {QStringLiteral("enabled"), true},
            {QStringLiteral("checkable"), false},
            {QStringLiteral("checked"), false},
            {QStringLiteral("separator"), false}});
    }
    if (includeSoundActions)
    {
        items.push_back(QVariantMap{
            {QStringLiteral("key"), QStringLiteral("node.autoPan")},
            {QStringLiteral("text"), QStringLiteral("Auto Pan (R)")},
            {QStringLiteral("enabled"), true},
            {QStringLiteral("checkable"), true},
            {QStringLiteral("checked"), m_controller->trackAutoPanEnabled(trackId)},
            {QStringLiteral("separator"), false}});
    }

    m_contextMenuTrackId = trackId;
    m_contextMenuNodeLabel = nodeLabel;
    const auto localPosition = m_contentAreaItem->mapFromScene(mapFromGlobal(globalPosition));
    const auto menuPosition = clampContextMenuPosition(m_contentAreaItem, localPosition, items, !nodeLabel.isEmpty());
    m_contextMenuController->showMenu(
        nodeLabel,
        menuPosition.x(),
        menuPosition.y(),
        items);
    updateOverlayPositions();
}

void MainWindow::showLoopContextMenu(const QPoint& globalPosition)
{
    if (!m_timelineQuickController || m_timelineQuickController->selectedLoopIndex() < 0)
    {
        return;
    }

    if (!m_contentAreaItem || !m_contextMenuController)
    {
        return;
    }

    const QVariantList items{QVariantMap{
        {QStringLiteral("key"), QStringLiteral("loop.delete")},
        {QStringLiteral("text"), QStringLiteral("Delete")},
        {QStringLiteral("enabled"), true},
        {QStringLiteral("checkable"), false},
        {QStringLiteral("checked"), false},
        {QStringLiteral("separator"), false}}};
    const auto localPosition = m_contentAreaItem->mapFromScene(mapFromGlobal(globalPosition));
    const auto menuPosition = clampContextMenuPosition(m_contentAreaItem, localPosition, items, true);
    m_contextMenuTrackId = {};
    m_contextMenuNodeLabel.clear();
    m_contextMenuController->showMenu(
        QStringLiteral("Loop Range"),
        menuPosition.x(),
        menuPosition.y(),
        items);
    updateOverlayPositions();
}

void MainWindow::refreshTimeline()
{
    if (!m_timelineQuickController && !m_mixQuickController)
    {
        return;
    }

    const auto trackSpans = m_controller->timelineTrackSpans();
    m_timelineTrackSpans = trackSpans;
    const auto timelineVideoPath =
        !m_controller->projectVideoPath().isEmpty()
            ? m_controller->projectVideoPath()
            : m_controller->loadedPath();
    if (m_timelineQuickController)
    {
        m_timelineQuickController->setProjectRootPath(m_currentProjectRootPath);
        m_timelineQuickController->setVideoPath(timelineVideoPath);
        m_timelineQuickController->setTrackSpans(trackSpans);
        m_timelineQuickController->setLoopRanges(m_controller->loopRanges());
    }
    if (m_thumbnailStripQuickController)
    {
        m_thumbnailStripQuickController->setProjectRootPath(m_currentProjectRootPath);
        m_thumbnailStripQuickController->setVideoPath(timelineVideoPath);
        syncThumbnailStripViewWindow();
        syncThumbnailStripSelectedNodeRange();
    }
    const auto needsThumbnailManifest =
        !timelineVideoPath.isEmpty()
        && m_currentProjectRootPath.size() > 0
        && (!m_timelineThumbnailGenerationController || !m_timelineThumbnailGenerationController->isGenerating())
        && ((m_timelineQuickController && !m_timelineQuickController->hasThumbnailManifest())
            || (m_thumbnailStripQuickController && !m_thumbnailStripQuickController->hasThumbnailManifest()));
    if (needsThumbnailManifest)
    {
        requestProjectTimelineThumbnailsGeneration();
    }
    updateTimelineMinimumHeight();
    if (m_clearLoopRangeAction)
    {
        m_clearLoopRangeAction->setEnabled(timelineHasSelectedLoopRange());
    }
    refreshMixView();
}

void MainWindow::requestProjectTimelineThumbnailsGeneration()
{
    if (!m_timelineThumbnailGenerationController)
    {
        return;
    }
    m_timelineThumbnailGenerationController->requestGeneration();
}

void MainWindow::syncNodeEditorActionAvailability()
{
    if (!m_nodeEditorWorkspaceSession)
    {
        return;
    }
    const auto wasChecked = m_showNodeEditorAction && m_showNodeEditorAction->isChecked();
    m_nodeEditorWorkspaceSession->syncAvailability(m_showNodeEditorAction, m_actionRegistry);
    if (m_showNodeEditorAction && wasChecked && !m_showNodeEditorAction->isEnabled())
    {
        updateNodeEditorVisibility(false);
    }
}

void MainWindow::refreshNodeEditor()
{
    if (m_nodeEditorWorkspaceSession)
    {
        m_nodeEditorWorkspaceSession->refresh(hasOpenProject());
        syncNodeEditorActionAvailability();
    }
}

void MainWindow::refreshMixView()
{
    if (m_mixQuickController && (m_mixQuickWidget || m_detachedMixWindow))
    {
        const auto laneStrips = m_controller->mixLaneStrips();
        const auto shouldRebuild = needsMixRebuild(laneStrips);
        m_masterMixGainDb = m_controller->masterMixGainDb();
        m_masterMixMuted = m_controller->masterMixMuted();
        m_mixLaneStrips = laneStrips;

        if (shouldRebuild)
        {
            rebuildMixStrips();
        }
        else
        {
            syncMixStripStates();
        }

        updateMixMeterLevels();
    }
}

void MainWindow::updateMixMeterLevels()
{
    const auto mixVisible =
        (m_mixQuickWidget && m_mixQuickWidget->isVisible())
        || (m_detachedMixWindow && m_detachedMixWindow->isVisible());
    if (!m_mixQuickController || !mixVisible)
    {
        return;
    }

    const auto masterStereoLevels = m_controller->masterMixStereoLevels();
    m_mixQuickController->setMasterMeterLevels(masterStereoLevels.left, masterStereoLevels.right);

    const auto meterStates = m_controller->mixLaneMeterStates(m_timelineTrackSpans);
    for (const auto& state : meterStates)
    {
        m_mixQuickController->setLaneMeterLevels(state.laneIndex, state.meterLeftLevel, state.meterRightLevel);
    }
}

bool MainWindow::needsMixRebuild(const std::vector<MixLaneStrip>& laneStrips) const
{
    if (laneStrips.size() != m_mixLaneStrips.size())
    {
        return true;
    }

    for (std::size_t index = 0; index < laneStrips.size(); ++index)
    {
        const auto& nextStrip = laneStrips[index];
        const auto& currentStrip = m_mixLaneStrips[index];
        if (nextStrip.laneIndex != currentStrip.laneIndex
            || nextStrip.label != currentStrip.label
            || nextStrip.color != currentStrip.color
            || nextStrip.clipCount != currentStrip.clipCount
            || nextStrip.useStereoMeter != currentStrip.useStereoMeter)
        {
            return true;
        }
    }

    return false;
}

void MainWindow::syncMixStripStates()
{
    if (!m_mixQuickController)
    {
        return;
    }

    m_mixQuickController->setMasterState(m_masterMixGainDb, m_masterMixMuted);
    for (const auto& strip : m_mixLaneStrips)
    {
        m_mixQuickController->setLaneState(strip.laneIndex, strip.gainDb, strip.muted, strip.soloed);
    }
}

void MainWindow::rebuildMixStrips()
{
    if (!m_mixQuickController)
    {
        return;
    }

    QVariantList descriptors;
    descriptors.reserve(static_cast<qsizetype>(m_mixLaneStrips.size()));

    for (const auto& strip : m_mixLaneStrips)
    {
        QVariantMap descriptor;
        descriptor.insert(QStringLiteral("laneIndex"), strip.laneIndex);
        descriptor.insert(QStringLiteral("titleText"), strip.label);
        descriptor.insert(
            QStringLiteral("detailText"),
            strip.clipCount == 1
                ? QVariant(QStringLiteral("1 clip"))
                : QVariant(QStringLiteral("%1 clips").arg(strip.clipCount)));
        descriptor.insert(QStringLiteral("footerText"), QStringLiteral("Lane %1").arg(strip.laneIndex + 1));
        descriptor.insert(QStringLiteral("accentColor"), strip.color);
        descriptor.insert(QStringLiteral("gainDb"), strip.gainDb);
        descriptor.insert(QStringLiteral("meterLevel"), strip.meterLevel);
        descriptor.insert(QStringLiteral("meterLeftLevel"), strip.meterLeftLevel);
        descriptor.insert(QStringLiteral("meterRightLevel"), strip.meterRightLevel);
        descriptor.insert(QStringLiteral("muted"), strip.muted);
        descriptor.insert(QStringLiteral("soloEnabled"), true);
        descriptor.insert(QStringLiteral("soloed"), strip.soloed);
        descriptor.insert(QStringLiteral("useStereoMeter"), strip.useStereoMeter);
        descriptors.push_back(descriptor);
    }

    m_mixQuickController->setLaneStrips(descriptors);
    syncMixStripStates();
}

void MainWindow::updateMixQuickDiagnostics()
{
    if (!rendererInterface())
    {
        m_qtQuickGraphicsApiText = QStringLiteral("Unavailable");
        return;
    }

    const auto apiText = graphicsApiToString(rendererInterface()->graphicsApi());
    if (m_qtQuickGraphicsApiText != apiText)
    {
        m_qtQuickGraphicsApiText = apiText;
        qInfo().noquote() << "Qt Quick graphics API:" << apiText;
    }
}

void MainWindow::handleMixQuickStatusChanged()
{
    if (status() == QQuickView::Ready)
    {
        m_qtQuickLoadText = QStringLiteral("Ready");
        updateMixQuickDiagnostics();
        rebuildMixStrips();
        return;
    }

    if (status() == QQuickView::Loading)
    {
        m_qtQuickLoadText = QStringLiteral("Loading");
        return;
    }

    if (status() == QQuickView::Null)
    {
        m_qtQuickLoadText = QStringLiteral("Null");
        return;
    }

    if (status() != QQuickView::Error)
    {
        return;
    }

    QStringList messages;
    const auto quickErrors = errors();
    messages.reserve(quickErrors.size());
    for (const auto& error : quickErrors)
    {
        messages.push_back(error.toString());
    }

    m_qtQuickLoadText = QStringLiteral("Error");
    qWarning().noquote() << "MainWindow failed to load qrc:/qml/MixScene.qml\n" << messages.join(QLatin1Char('\n'));
}

void MainWindow::syncShellLayoutViewport()
{
    if (!m_contentAreaItem || !m_shellLayoutController)
    {
        return;
    }

    m_shellLayoutController->setViewportSize(
        static_cast<int>(std::lround(m_contentAreaItem->width())),
        static_cast<int>(std::lround(m_contentAreaItem->height())));
    syncShellPanelGeometry();
}

void MainWindow::syncShellPanelGeometry()
{
    if (!m_shellLayoutController)
    {
        return;
    }
    updateOverlayPositions();
}

void MainWindow::handleTimelineQuickStatusChanged()
{
    updateTimelineMinimumHeight();
}

void MainWindow::clearTimeline()
{
    m_timelineTrackSpans.clear();
    if (m_timelineQuickController)
    {
        m_timelineQuickController->setProjectRootPath({});
        m_timelineQuickController->clear();
    }
    if (m_thumbnailStripQuickController)
    {
        m_thumbnailStripQuickController->setProjectRootPath({});
        m_thumbnailStripQuickController->clear();
    }
    updateTimelineMinimumHeight();
}

void MainWindow::setTimelineVideoPath(const QString& videoPath)
{
    if (m_timelineQuickController)
    {
        m_timelineQuickController->setVideoPath(videoPath);
    }
    if (m_thumbnailStripQuickController)
    {
        m_thumbnailStripQuickController->setVideoPath(videoPath);
    }
}

void MainWindow::setTimelineState(const int totalFrames, const double fps)
{
    if (m_timelineQuickController)
    {
        m_timelineQuickController->setTimeline(totalFrames, fps);
    }
    if (m_thumbnailStripQuickController)
    {
        m_thumbnailStripQuickController->setTimeline(totalFrames, fps);
        syncThumbnailStripViewWindow();
        syncThumbnailStripSelectedNodeRange();
    }
}

void MainWindow::setTimelineCurrentFrame(const int frameIndex)
{
    if (m_timelineQuickController && m_timelineQuickWidget && m_timelineQuickWidget->isVisible())
    {
        m_timelineQuickController->setCurrentFrame(frameIndex);
    }
    if (m_thumbnailStripQuickController)
    {
        m_thumbnailStripQuickController->setCurrentFrame(frameIndex);
    }
}

void MainWindow::setTimelineSeekOnClickEnabled(const bool enabled)
{
    if (m_timelineQuickController)
    {
        m_timelineQuickController->setSeekOnClickEnabled(enabled);
    }
}

void MainWindow::setTimelineThumbnailsVisible(const bool visible)
{
    if (m_shellLayoutController)
    {
        m_shellLayoutController->setThumbnailsVisible(visible);
    }
    syncShellPanelGeometry();
}

void MainWindow::syncThumbnailStripViewWindow()
{
    if (!m_thumbnailStripQuickController || !m_timelineQuickController)
    {
        return;
    }

    m_thumbnailStripQuickController->setViewWindow(
        m_timelineQuickController->viewStartFrameValue(),
        m_timelineQuickController->visibleFrameSpanValue());
}

void MainWindow::syncThumbnailStripSelectedNodeRange()
{
    if (!m_thumbnailStripQuickController || !m_controller)
    {
        return;
    }

    const auto selectedRange = m_controller->selectedTrackFrameRange();
    if (!selectedRange.has_value())
    {
        m_thumbnailStripQuickController->setSelectedNodeRange(std::nullopt, std::nullopt);
        return;
    }

    m_thumbnailStripQuickController->setSelectedNodeRange(selectedRange->first, selectedRange->second);
}

std::optional<int> MainWindow::timelineLoopShortcutFrame() const
{
    return m_timelineQuickController ? m_timelineQuickController->loopShortcutFrame() : std::nullopt;
}

bool MainWindow::timelineHasSelectedLoopRange() const
{
    return m_timelineQuickController && m_timelineQuickController->hasSelectedLoopRange();
}

bool MainWindow::timelineHasFocus() const
{
    return itemHasActiveFocus(const_cast<MainWindow*>(this), m_timelineQuickWidget);
}

bool MainWindow::nodeEditorHasFocus() const
{
    return itemHasActiveFocus(const_cast<MainWindow*>(this), m_nodeEditorQuickWidget);
}

bool MainWindow::videoPanelHasFocus() const
{
    return itemHasActiveFocus(const_cast<MainWindow*>(this), m_videoViewportQuickWidget)
        || (m_detachedVideoWindow && m_detachedVideoWindow->isActive())
        || (m_nativeViewportWindow && m_nativeViewportWindow->isActiveWindow());
}

MainWindow::PlaybackContext MainWindow::preferredPlaybackContext() const
{
    if (nodeEditorHasFocus())
    {
        return PlaybackContext::NodeEditor;
    }

    if (timelineHasFocus()
        || itemHasActiveFocus(const_cast<MainWindow*>(this), m_thumbnailStripQuickWidget)
        || videoPanelHasFocus())
    {
        return PlaybackContext::Project;
    }

    return m_preferredPlaybackContext;
}

void MainWindow::playFromPreferredContext()
{
    const auto context = preferredPlaybackContext();

    if (context == PlaybackContext::NodeEditor)
    {
        if (m_nodeEditorPreviewSession)
        {
            m_nodeEditorPreviewSession->toggle();
        }
        return;
    }

    if (m_nodeEditorPreviewSession)
    {
        m_nodeEditorPreviewSession->stop();
    }
    m_controller->togglePlayback();
}

void MainWindow::setPreferredPlaybackContext(const PlaybackContext context)
{
    m_preferredPlaybackContext = context;
}

void MainWindow::deleteFromFocusedPanel()
{
    if (nodeEditorHasFocus())
    {
        if (m_nodeEditorWorkspaceSession)
        {
            m_nodeEditorWorkspaceSession->deleteSelection(hasOpenProject());
            syncNodeEditorActionAvailability();
        }
        return;
    }

    if (videoPanelHasFocus() && m_controller)
    {
        m_controller->deleteSelectedTrack();
    }
}

int MainWindow::timelineMinimumHeight() const
{
    return timelinePreferredHeight(m_timelineTrackSpans);
}

void MainWindow::updateTimelineMinimumHeight()
{
    const auto height = timelineMinimumHeight();
    if (m_shellLayoutController)
    {
        m_shellLayoutController->setTimelineMinimumHeight(height);
    }
    syncShellPanelGeometry();
}

void MainWindow::updateEditActionState()
{
    m_actionsController->updateEditActionState();
}

bool MainWindow::shouldApplyNodeShortcutToAll() const
{
    return timelineHasFocus()
        && !m_controller->hasSelection()
        && m_controller->hasTracks();
}

void MainWindow::adjustTrackMixGainFromWheel(const QUuid& trackId, const int wheelDelta, const QPoint& globalPosition)
{
    if (trackId.isNull() || wheelDelta == 0)
    {
        return;
    }

    const auto deltaDb = static_cast<float>(wheelDelta) / 120.0F;
    const auto nextGainDb = m_controller->adjustMixLaneGainForTrack(trackId, deltaDb);
    if (!nextGainDb.has_value())
    {
        return;
    }

    refreshMixView();
    if (!m_projectStateChangeInProgress && hasOpenProject())
    {
        setProjectDirty(true);
    }

    if (m_shellOverlayController)
    {
        showTrackMixGainPopup(trackId, globalPosition);
    }
}

void MainWindow::showTrackMixGainPopup(const QUuid& trackId, const QPoint& globalPosition)
{
    if (trackId.isNull() || !m_shellOverlayController || !m_shellOverlayQuickWidget)
    {
        return;
    }

    const auto gainDb = m_controller->mixLaneGainForTrack(trackId);
    if (!gainDb.has_value())
    {
        return;
    }

    m_trackGainPopupTrackId = trackId;
    const auto scenePoint = QPointF(mapFromGlobal(globalPosition));
    const auto localPoint = m_shellOverlayQuickWidget->mapFromScene(scenePoint);
    m_shellOverlayController->showTrackGainPopup(
        mixGainDisplayText(*gainDb),
        static_cast<int>(std::lround(localPoint.x())),
        static_cast<int>(std::lround(localPoint.y())),
        mixGainDbToSliderValue(*gainDb));
}

void MainWindow::hideTrackMixGainPopup()
{
    m_trackGainPopupTrackId = {};
    if (m_shellOverlayController)
    {
        m_shellOverlayController->hideTrackGainPopup();
    }
}

void MainWindow::updateTrackMixGainPopupValue(const float gainDb)
{
    if (m_shellOverlayController)
    {
        m_shellOverlayController->setTrackGainPopupValue(
            mixGainDisplayText(gainDb),
            mixGainDbToSliderValue(gainDb));
    }
}

std::optional<int> MainWindow::timelineLoopTargetFrame() const
{
    if (!m_controller || !m_controller->hasVideoLoaded())
    {
        return std::nullopt;
    }

    if (const auto shortcutFrame = timelineLoopShortcutFrame(); shortcutFrame.has_value())
    {
        return shortcutFrame;
    }

    return m_controller->currentFrameIndex();
}

void MainWindow::createLoopRangeFromShortcutFrames(const int startFrame, const int endFrame)
{
    if (!m_timelineQuickController)
    {
        return;
    }

    const auto normalizedStart = std::min(startFrame, endFrame);
    const auto normalizedEnd = std::max(startFrame, endFrame);
    if (normalizedEnd <= normalizedStart)
    {
        m_pendingLoopShortcutStartFrame.reset();
        m_pendingLoopShortcutEndFrame.reset();
        m_timelineQuickController->setPendingLoopDraftFrame(std::nullopt);
        return;
    }

    m_timelineQuickController->preparePendingLoopSelection(normalizedStart, normalizedEnd);
    m_timelineQuickController->setPendingLoopDraftFrame(std::nullopt);
    if (!m_controller->addLoopRange(normalizedStart, normalizedEnd))
    {
        showStatus(QStringLiteral("Loop range could not be created there."));
    }

    m_pendingLoopShortcutStartFrame.reset();
    m_pendingLoopShortcutEndFrame.reset();
}

void MainWindow::buildMenus()
{
    m_actionsController->buildMenus();
}

void MainWindow::buildUi()
{
    if (m_shellUiSetupController)
    {
        m_shellUiSetupController->buildUi();
    }
}

void MainWindow::armClearAllShortcut()
{
    if (!m_controller->hasTracks())
    {
        return;
    }

    m_clearAllShortcutArmed = true;
    m_clearAllShortcutTimer.start();
    showStatus(QStringLiteral("Clear all armed. Press Backspace to remove all nodes."));
}

void MainWindow::clearPendingClearAllShortcut()
{
    m_clearAllShortcutArmed = false;
    m_clearAllShortcutTimer.stop();
}
