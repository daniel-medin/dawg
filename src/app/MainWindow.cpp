#include "app/MainWindow.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>

#include <QApplication>
#include <QCursor>
#include <QDataStream>
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
#include <QPixmap>
#include <QQuickItem>
#include <QQuickImageProvider>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QQmlContext>
#include <QSGRendererInterface>
#include <QScrollArea>
#include <QScreen>
#include <QSettings>
#include <QShortcut>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QToolTip>
#include <QResizeEvent>
#include <QWidget>
#include <QUrl>

#ifdef Q_OS_WIN
#include <windows.h>
#include <windowsx.h>
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
#include "app/ShellLayoutController.h"
#include "app/ShellOverlayController.h"
#include "app/PanelLayoutController.h"
#include "app/DebugUiController.h"
#include "app/MediaImportController.h"
#include "app/ProjectDocument.h"
#include "app/WindowChromeController.h"
#include "ui/ClipEditorQuickController.h"
#include "ui/ClipWaveformQuickItem.h"
#include "ui/DebugOverlayWindow.h"
#include "ui/MixQuickController.h"
#include "ui/NativeVideoViewport.h"
#include "ui/QuickEngineSupport.h"
#include "ui/TimelineQuickController.h"
#include "ui/TimelineThumbnailCache.h"
#include "ui/VideoViewportQuickController.h"
#include <qqml.h>

namespace
{
QUrl quickTitleBarUrl()
{
    return QUrl(QStringLiteral("qrc:/qml/QuickTitleBar.qml"));
}

QUrl mixSceneUrl()
{
    return QUrl(QStringLiteral("qrc:/qml/MixScene.qml"));
}

QUrl clipEditorSceneUrl()
{
    return QUrl(QStringLiteral("qrc:/qml/ClipEditorScene.qml"));
}

QUrl shellLayoutSceneUrl()
{
    return QUrl(QStringLiteral("qrc:/qml/ShellLayoutScene.qml"));
}

QUrl timelineSceneUrl()
{
    return QUrl(QStringLiteral("qrc:/qml/TimelineScene.qml"));
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

        const auto image = m_controller.currentFrame();
        if (size)
        {
            *size = image.size();
        }

        if (image.isNull() || !requestedSize.isValid())
        {
            return image;
        }

        return image.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

private:
    VideoViewportQuickController& m_controller;
};

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

void ensureClipEditorQuickTypesRegistered()
{
    static const bool registered = []()
    {
        qmlRegisterType<ClipWaveformQuickItem>("Dawg", 1, 0, "ClipWaveformQuickItem");
        return true;
    }();
    Q_UNUSED(registered);
}

constexpr auto kLastProjectPathSettingsKey = "project/lastProjectPath";
constexpr auto kRecentProjectPathsSettingsKey = "project/recentProjectPaths";
constexpr int kMaxRecentProjectCount = 10;
constexpr int kMixGainPopupMinValue = -1000;
constexpr int kMixGainPopupMaxValue = 120;

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

QString uniqueTargetFilePath(
    const QString& targetDirectoryPath,
    const QString& sourceFilePath)
{
    const QFileInfo sourceInfo(sourceFilePath);
    const auto completeBaseName = sourceInfo.completeBaseName();
    const auto suffix = sourceInfo.suffix();
    const auto extension = suffix.isEmpty() ? QString{} : QStringLiteral(".") + suffix;
    QDir targetDirectory(targetDirectoryPath);

    auto candidatePath = targetDirectory.filePath(sourceInfo.fileName());
    if (!QFileInfo::exists(candidatePath))
    {
        return candidatePath;
    }

    for (int index = 2;; ++index)
    {
        candidatePath = targetDirectory.filePath(
            QStringLiteral("%1 (%2)%3").arg(completeBaseName).arg(index).arg(extension));
        if (!QFileInfo::exists(candidatePath))
        {
            return candidatePath;
        }
    }
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

int timelineLaneCount(const std::vector<TimelineTrackSpan>& trackSpans)
{
    int maxLaneIndex = -1;
    for (const auto& trackSpan : trackSpans)
    {
        maxLaneIndex = std::max(maxLaneIndex, trackSpan.laneIndex);
    }

    return std::max(0, maxLaneIndex + 1);
}

int timelinePreferredHeight(const std::vector<TimelineTrackSpan>& trackSpans)
{
    constexpr int baseHeight = 154;
    constexpr int verticalPadding = 90;
    constexpr int rowHeight = 10;
    constexpr int rowGap = 2;

    if (trackSpans.empty())
    {
        return baseHeight;
    }

    const auto trackLaneCount = timelineLaneCount(trackSpans);
    return std::max(
        baseHeight,
        verticalPadding + trackLaneCount * rowHeight + std::max(0, trackLaneCount - 1) * rowGap + 16);
}

}

MainWindow::MainWindow(QWindow* parent)
    : QQuickView(parent)
    , m_controller(new PlayerController(this))
{
    buildUi();
    m_actionsController = std::make_unique<MainWindowActions>(*this);
    m_projectWindowController = std::make_unique<ProjectWindowController>(*this);
    m_panelLayoutController = std::make_unique<PanelLayoutController>(*this);
    m_debugUiController = std::make_unique<DebugUiController>(*this);
    m_mediaImportController = std::make_unique<MediaImportController>(*this);
    buildMenus();
    if (m_actionRegistry)
    {
        m_actionRegistry->rebuild();
    }
    rebuildRecentProjectsMenu();
    updateDetachedVideoUiState();
    qApp->installEventFilter(this);
    m_clearAllShortcutTimer.setSingleShot(true);
    m_clearAllShortcutTimer.setInterval(1500);
    m_memoryUsageTimer.setInterval(1000);
    m_mixMeterTimer.setInterval(33);
    m_clipEditorPreviewTimer.setInterval(16);
    connect(&m_clearAllShortcutTimer, &QTimer::timeout, this, &MainWindow::clearPendingClearAllShortcut);
    connect(&m_memoryUsageTimer, &QTimer::timeout, this, &MainWindow::updateMemoryUsage);
    connect(&m_mixMeterTimer, &QTimer::timeout, this, &MainWindow::refreshMixView);
    connect(&m_clipEditorPreviewTimer, &QTimer::timeout, this, &MainWindow::refreshClipEditor);
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
    connect(m_importSoundAction, &QAction::triggered, this, &MainWindow::importSound);
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
    connect(m_showTimelineAction, &QAction::toggled, this, [this](const bool visible)
    {
        updateTimelineVisibility(visible);
        if (!m_projectStateChangeInProgress && hasOpenProject())
        {
            setProjectDirty(true);
        }
        showStatus(visible ? QStringLiteral("Timeline shown.") : QStringLiteral("Timeline hidden."));
    });
    connect(m_showClipEditorAction, &QAction::toggled, this, [this](const bool visible)
    {
        updateClipEditorVisibility(visible);
        if (!m_projectStateChangeInProgress && hasOpenProject())
        {
            setProjectDirty(true);
        }
        showStatus(visible ? QStringLiteral("Clip editor shown.") : QStringLiteral("Clip editor hidden."));
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
        showStatus(visible ? QStringLiteral("Timeline thumbnails shown.") : QStringLiteral("Timeline thumbnails hidden."));
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
    connect(m_goToStartAction, &QAction::triggered, m_controller, &PlayerController::goToStart);
    connect(m_playAction, &QAction::triggered, m_controller, &PlayerController::togglePlayback);
    connect(m_stepForwardAction, &QAction::triggered, m_controller, &PlayerController::stepForward);
    connect(m_stepBackAction, &QAction::triggered, m_controller, &PlayerController::stepBackward);
    connect(m_stepFastForwardAction, &QAction::triggered, m_controller, &PlayerController::stepFastForward);
    connect(m_stepFastBackAction, &QAction::triggered, m_controller, &PlayerController::stepFastBackward);
    connect(m_copyAction, &QAction::triggered, this, &MainWindow::copySelectedNode);
    connect(m_pasteAction, &QAction::triggered, this, &MainWindow::pasteNode);
    connect(m_cutAction, &QAction::triggered, this, &MainWindow::cutSelectedNode);
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
    connect(m_loopSoundAction, &QAction::toggled, this, [this](const bool enabled)
    {
        if (m_controller->setSelectedTrackLoopEnabled(enabled))
        {
            refreshClipEditor();
        }
    });
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
    connect(m_deleteNodeAction, &QAction::triggered, m_controller, &PlayerController::deleteSelectedTrack);
    connect(m_clearAllAction, &QAction::triggered, m_controller, &PlayerController::clearAllTracks);
    connect(m_timelineQuickController, &TimelineQuickController::frameRequested, m_controller, &PlayerController::seekToFrame);
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
        &TimelineQuickController::trackSelected,
        m_controller,
        &PlayerController::selectTrack);
    connect(m_timelineQuickController, &TimelineQuickController::trackActivated, this, [this](const QUuid& trackId)
    {
        m_controller->selectTrack(trackId);
        updateClipEditorVisibility(true);
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
    if (m_showTimelineThumbnailsAction)
    {
        setTimelineThumbnailsVisible(m_showTimelineThumbnailsAction->isChecked());
    }
    connect(m_toggleDebugAction, &QAction::toggled, this, [this](const bool enabled)
    {
        updateDebugVisibility(enabled);
        showStatus(enabled ? QStringLiteral("Debug info shown.") : QStringLiteral("Debug info hidden."));
    });
    connect(m_playPauseShortcut, &QShortcut::activated, m_controller, &PlayerController::togglePlayback);
    connect(m_startShortcut, &QShortcut::activated, m_controller, &PlayerController::goToStart);
    connect(m_numpadStartShortcut, &QShortcut::activated, m_controller, &PlayerController::goToStart);
    connect(m_stepBackShortcut, &QShortcut::activated, m_controller, &PlayerController::stepBackward);
    connect(m_stepForwardShortcut, &QShortcut::activated, m_controller, &PlayerController::stepForward);
    connect(m_stepFastForwardShortcut, &QShortcut::activated, m_controller, &PlayerController::stepFastForward);
    connect(m_stepFastBackShortcut, &QShortcut::activated, m_controller, &PlayerController::stepFastBackward);
    connect(m_copyShortcut, &QShortcut::activated, this, &MainWindow::copySelectedNode);
    connect(m_pasteShortcut, &QShortcut::activated, this, &MainWindow::pasteNode);
    connect(m_cutShortcut, &QShortcut::activated, this, &MainWindow::cutSelectedNode);
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
    connect(m_showClipEditorShortcut, &QShortcut::activated, m_showClipEditorAction, &QAction::trigger);
    connect(m_trimNodeShortcut, &QShortcut::activated, this, &MainWindow::trimSelectedNodeToSound);
    connect(m_autoPanShortcut, &QShortcut::activated, this, &MainWindow::toggleSelectedNodeAutoPan);
    connect(m_audioPoolShortcut, &QShortcut::activated, m_audioPoolAction, &QAction::trigger);
    connect(m_toggleNodeNameShortcut, &QShortcut::activated, m_controller, &PlayerController::toggleSelectedTrackLabels);
    connect(m_deleteShortcut, &QShortcut::activated, m_controller, &PlayerController::deleteSelectedTrack);
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
    connect(m_clipEditorQuickController, &ClipEditorQuickController::gainChanged, this, [this](const float gainDb)
    {
        if (m_controller->setSelectedTrackAudioGainDb(gainDb))
        {
            refreshClipEditor();
        }
    });
    connect(m_clipEditorQuickController, &ClipEditorQuickController::attachAudioRequested, this, &MainWindow::importSound);
    connect(m_clipEditorQuickController, &ClipEditorQuickController::loopSoundChanged, this, [this](const bool enabled)
    {
        if (m_controller->setSelectedTrackLoopEnabled(enabled))
        {
            refreshClipEditor();
        }
    });
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
    updateDebugVisibility(true);
    updateDebugText();
    refreshAudioPool();
    updateVideoAudioRow();
    m_memoryUsageTimer.start();
    clearCurrentProject();
    restoreLastProjectOnStartup();
    if (!hasOpenProject())
    {
        showStatus(QStringLiteral("Create or open a project to start adding nodes."));
    }
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
    if (watched == m_detachedVideoWindow && event && event->type() == QEvent::Close && m_videoDetached && !m_shuttingDown)
    {
        attachVideo();
        event->ignore();
        return true;
    }

    const auto clipEditorFocused = [this]() -> bool
    {
        return itemHasActiveFocus(this, m_clipEditorQuickWidget);
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

    if (m_contextMenuController && m_contextMenuController->visible() && m_contextMenuQuickWidget)
    {
        if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonDblClick)
        {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            if (!itemContainsPoint(m_contextMenuQuickWidget, mouseEvent->position()))
            {
                m_contextMenuController->dismiss();
            }
        }
    }

    if (event->type() == QEvent::ShortcutOverride)
    {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (!keyEvent->isAutoRepeat()
            && keyEvent->key() == Qt::Key_Space
            && keyEvent->modifiers() == Qt::NoModifier
            && clipEditorFocused())
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
            && keyEvent->key() == Qt::Key_Space
            && keyEvent->modifiers() == Qt::NoModifier
            && clipEditorFocused())
        {
            if (m_controller->isSelectedTrackClipPreviewPlaying())
            {
                m_controller->stopSelectedTrackClipPreview();
            }
            else
            {
                static_cast<void>(m_controller->startSelectedTrackClipPreview());
            }
            refreshClipEditor();
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
                && (m_controller->loopStartFrame().has_value() || m_controller->loopEndFrame().has_value()))
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
    if (!promptToSaveIfDirty(QStringLiteral("close the app")))
    {
        event->ignore();
        return;
    }

    if (hasOpenProject() && !m_projectDirty && !saveProjectToCurrentPath())
    {
        event->ignore();
        return;
    }

    m_shuttingDown = true;
    QQuickView::closeEvent(event);
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
        updateClipEditorVisibility(true);
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

    if (!uuids.isEmpty())
    {
        m_controller->selectTracks(uuids);
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

void MainWindow::importSound()
{
    m_mediaImportController->importSound();
}

void MainWindow::importAudioToPool()
{
    m_mediaImportController->importAudioToPool();
}

void MainWindow::handleLoopStartShortcut()
{
    if (const auto targetFrame = timelineLoopTargetFrame(); targetFrame.has_value())
    {
        m_controller->setLoopStartFrame(*targetFrame);
    }
}

void MainWindow::handleLoopEndShortcut()
{
    if (const auto targetFrame = timelineLoopTargetFrame(); targetFrame.has_value())
    {
        m_controller->setLoopEndFrame(*targetFrame);
    }
}

void MainWindow::clearLoopRange()
{
    m_controller->clearLoopRange();
}

void MainWindow::handleNodeStartShortcut()
{
    const auto clipEditorFocused = itemHasActiveFocus(this, m_clipEditorQuickWidget);
    if (clipEditorFocused)
    {
        if (const auto state = m_controller->selectedClipEditorState();
            state.has_value() && state->hasAttachedAudio && state->playheadMs.has_value())
        {
            const auto nextClipEndMs = std::max(*state->playheadMs + 1, state->clipEndMs);
            if (m_controller->setSelectedTrackClipRangeMs(*state->playheadMs, nextClipEndMs))
            {
                refreshClipEditor();
            }
            return;
        }
    }

    if (timelineLoopShortcutFrame().has_value())
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
    const auto clipEditorFocused = itemHasActiveFocus(this, m_clipEditorQuickWidget);
    if (clipEditorFocused)
    {
        if (const auto state = m_controller->selectedClipEditorState();
            state.has_value() && state->hasAttachedAudio && state->playheadMs.has_value())
        {
            const auto nextClipEndMs = std::max(state->clipStartMs + 1, *state->playheadMs);
            if (m_controller->setSelectedTrackClipRangeMs(state->clipStartMs, nextClipEndMs))
            {
                refreshClipEditor();
            }
            return;
        }
    }

    if (timelineLoopShortcutFrame().has_value())
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

void MainWindow::selectNextVisibleNode()
{
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
    if (m_nativeViewportWindow && m_nativeViewportWindow->isVisible() && m_nativeViewport)
    {
        m_nativeViewport->setOverlays(m_controller->currentOverlays());
    }

    if (m_controller && m_controller->isPlaying())
    {
        return;
    }

    refreshTimeline();
    refreshClipEditor();
}

void MainWindow::updateInsertionFollowsPlaybackState(const bool enabled)
{
    if (m_insertionFollowsPlaybackAction && m_insertionFollowsPlaybackAction->isChecked() != enabled)
    {
        m_insertionFollowsPlaybackAction->setChecked(enabled);
    }
    updateDebugText();
}

void MainWindow::updatePlaybackState(const bool playing)
{
    const auto label = playing ? QStringLiteral("Pause (Space)") : QStringLiteral("Play (Space)");
    m_playAction->setText(label);
    m_debugTextTimer.invalidate();
    if (playing)
    {
        resetOutputFpsTracking();
        m_outputFpsTimer.start();
    }
    else
    {
        resetOutputFpsTracking();
    }
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
}

void MainWindow::updateTrackAvailabilityState(const bool hasTracks)
{
    m_actionsController->updateTrackAvailabilityState(hasTracks);
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

void MainWindow::updateClipEditorVisibility(const bool visible)
{
    m_panelLayoutController->updateClipEditorVisibility(visible);
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

void MainWindow::updateDetachedVideoUiState()
{
    m_panelLayoutController->updateDetachedVideoUiState();
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
    refreshAudioPool();
}

void MainWindow::updateVideoAudioRow()
{
    if (!m_audioPoolQuickController)
    {
        return;
    }

    const auto hasVideoAudio = m_controller->hasEmbeddedVideoAudio();
    const auto displayName = hasVideoAudio ? m_controller->embeddedVideoAudioDisplayName() : QString{};
    const auto tooltip =
        hasVideoAudio
            ? QStringLiteral("Embedded audio from %1%2")
                .arg(displayName)
                .arg(m_controller->isFastPlaybackEnabled() ? QStringLiteral("\nFast Playback enabled") : QString{})
            : QString{};
    m_audioPoolQuickController->syncVideoAudioState(
        hasVideoAudio,
        displayName,
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
        {QStringLiteral("key"), QStringLiteral("node.rename")},
        {QStringLiteral("text"), QStringLiteral("Rename Node...")},
        {QStringLiteral("enabled"), true},
        {QStringLiteral("checkable"), false},
        {QStringLiteral("checked"), false},
        {QStringLiteral("separator"), false}});
    items.push_back(QVariantMap{{QStringLiteral("separator"), true}});
    items.push_back(QVariantMap{
        {QStringLiteral("key"), QStringLiteral("node.importAudio")},
        {QStringLiteral("text"), QStringLiteral("Import Audio...")},
        {QStringLiteral("enabled"), true},
        {QStringLiteral("checkable"), false},
        {QStringLiteral("checked"), false},
        {QStringLiteral("separator"), false}});
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
    m_contextMenuController->showMenu(
        nodeLabel,
        static_cast<int>(std::lround(localPosition.x())),
        static_cast<int>(std::lround(localPosition.y())),
        items);
    updateOverlayPositions();
}

void MainWindow::showLoopContextMenu(const QPoint& globalPosition)
{
    if (!m_controller->loopStartFrame().has_value() && !m_controller->loopEndFrame().has_value())
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
    m_contextMenuTrackId = {};
    m_contextMenuNodeLabel.clear();
    m_contextMenuController->showMenu(
        QStringLiteral("Loop Range"),
        static_cast<int>(std::lround(localPosition.x())),
        static_cast<int>(std::lround(localPosition.y())),
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
    if (m_timelineQuickController)
    {
        m_timelineQuickController->setVideoPath(m_controller->loadedPath());
        m_timelineQuickController->setTrackSpans(trackSpans);
        m_timelineQuickController->setLoopRange(m_controller->loopStartFrame(), m_controller->loopEndFrame());
    }
    updateTimelineMinimumHeight();
    if (m_clearLoopRangeAction)
    {
        m_clearLoopRangeAction->setEnabled(
            m_controller->loopStartFrame().has_value() || m_controller->loopEndFrame().has_value());
    }
    refreshMixView();
}

void MainWindow::refreshClipEditor()
{
    if (!m_clipEditorQuickController || !m_clipEditorQuickWidget || !m_clipEditorQuickWidget->isVisible())
    {
        m_clipEditorPreviewTimer.stop();
        return;
    }

    m_clipEditorState = m_controller->selectedClipEditorState();
    m_clipEditorQuickController->setState(m_clipEditorState);
    syncClipWaveformItem();
    if (m_controller->isSelectedTrackClipPreviewPlaying())
    {
        if (!m_clipEditorPreviewTimer.isActive())
        {
            m_clipEditorPreviewTimer.start();
        }
    }
    else
    {
        m_clipEditorPreviewTimer.stop();
    }
}

void MainWindow::handleClipEditorQuickStatusChanged()
{
    if (!m_shellRootItem)
    {
        return;
    }

    if (!m_clipWaveformItem)
    {
        m_clipWaveformItem = m_shellRootItem->findChild<ClipWaveformQuickItem*>(QStringLiteral("clipWaveform"));
        if (m_clipWaveformItem)
        {
            connect(m_clipWaveformItem, &ClipWaveformQuickItem::clipRangeChanged, m_controller, &PlayerController::setSelectedTrackClipRangeMs);
            connect(m_clipWaveformItem, &ClipWaveformQuickItem::playheadChanged, this, [this](const int playheadMs)
            {
                if (m_controller->setSelectedTrackClipPlayheadMs(playheadMs))
                {
                    if (!m_projectStateChangeInProgress && hasOpenProject())
                    {
                        setProjectDirty(true);
                    }
                    refreshClipEditor();
                }
            });
        }
    }

    syncClipWaveformItem();
}

void MainWindow::syncClipWaveformItem()
{
    if (!m_clipWaveformItem)
    {
        return;
    }

    m_clipWaveformItem->setState(m_clipEditorState);
}

void MainWindow::refreshMixView()
{
    if (m_mixQuickController && m_mixQuickWidget && m_mixQuickWidget->isVisible())
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

        m_mixQuickController->setMasterMeterLevel(m_controller->masterMixLevel());
        for (const auto& strip : laneStrips)
        {
            m_mixQuickController->setLaneMeterLevel(strip.laneIndex, strip.meterLevel);
        }
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
            || nextStrip.clipCount != currentStrip.clipCount)
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
        descriptor.insert(QStringLiteral("muted"), strip.muted);
        descriptor.insert(QStringLiteral("soloEnabled"), true);
        descriptor.insert(QStringLiteral("soloed"), strip.soloed);
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
        m_timelineQuickController->clear();
    }
    updateTimelineMinimumHeight();
}

void MainWindow::setTimelineVideoPath(const QString& videoPath)
{
    if (m_timelineQuickController)
    {
        m_timelineQuickController->setVideoPath(videoPath);
    }
}

void MainWindow::setTimelineState(const int totalFrames, const double fps)
{
    if (m_timelineQuickController)
    {
        m_timelineQuickController->setTimeline(totalFrames, fps);
    }
}

void MainWindow::setTimelineCurrentFrame(const int frameIndex)
{
    if (m_timelineQuickController && m_timelineQuickWidget && m_timelineQuickWidget->isVisible())
    {
        m_timelineQuickController->setCurrentFrame(frameIndex);
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
    if (m_timelineQuickController)
    {
        m_timelineQuickController->setThumbnailsVisible(visible);
    }
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

    QToolTip::showText(globalPosition, mixGainDisplayText(*nextGainDb), nullptr, {}, 900);
    if (m_shellOverlayController
        && m_shellOverlayController->trackGainPopupVisible()
        && m_trackGainPopupTrackId == trackId)
    {
        updateTrackMixGainPopupValue(*nextGainDb);
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

void MainWindow::buildMenus()
{
    m_actionsController->buildMenus();
}

void MainWindow::buildUi()
{
    setWindowTitle(QStringLiteral("dawg"));
    resize(1400, 900);
    setMinimumSize(QSize(1180, 760));
    setFlags(flags() | Qt::FramelessWindowHint);
    setColor(QColor(QStringLiteral("#0a0c10")));
    setResizeMode(QQuickView::SizeRootObjectToView);
    setIcon(QIcon(QStringLiteral(":/branding/dawg.png")));

    m_actionRegistry = new ActionRegistry(*this, this);
    m_windowChromeController = new WindowChromeController(*this, this);
    m_playPauseShortcut = new QShortcut(QKeySequence(Qt::Key_Space), this);
    m_startShortcut = new QShortcut(QKeySequence(Qt::Key_Return), this);
    m_numpadStartShortcut = new QShortcut(QKeySequence(Qt::Key_Enter), this);
    m_stepBackShortcut = new QShortcut(QKeySequence(Qt::Key_Comma), this);
    m_stepForwardShortcut = new QShortcut(QKeySequence(Qt::Key_Period), this);
    m_stepFastForwardShortcut = new QShortcut(QKeySequence(Qt::Key_Minus), this);
    m_stepFastBackShortcut = new QShortcut(QKeySequence(Qt::Key_M), this);
    m_insertionFollowsPlaybackShortcut = new QShortcut(QKeySequence(Qt::Key_N), this);
    m_copyShortcut = new QShortcut(QKeySequence::Copy, this);
    m_pasteShortcut = new QShortcut(QKeySequence::Paste, this);
    m_cutShortcut = new QShortcut(QKeySequence::Cut, this);
    m_undoShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Z), this);
    m_redoShortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Y), this);
    m_selectAllShortcut = new QShortcut(QKeySequence::SelectAll, this);
    m_nodeStartShortcut = new QShortcut(QKeySequence(Qt::Key_A), this);
    m_nodeEndShortcut = new QShortcut(QKeySequence(Qt::Key_S), this);
    m_selectNextNodeShortcut = new QShortcut(QKeySequence(Qt::Key_Tab), this);
    m_showTimelineShortcut = new QShortcut(QKeySequence(Qt::Key_T), this);
    m_showClipEditorShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+-")), this);
    m_showMixShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl++")), this);
    m_trimNodeShortcut = new QShortcut(QKeySequence(Qt::SHIFT | Qt::Key_T), this);
    m_autoPanShortcut = new QShortcut(QKeySequence(Qt::Key_R), this);
    m_audioPoolShortcut = new QShortcut(QKeySequence(Qt::Key_P), this);
    m_toggleNodeNameShortcut = new QShortcut(QKeySequence(Qt::Key_E), this);
    m_deleteShortcut = new QShortcut(QKeySequence(Qt::Key_Backspace), this);
    m_unselectAllShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    m_playPauseShortcut->setContext(Qt::ApplicationShortcut);
    m_startShortcut->setContext(Qt::ApplicationShortcut);
    m_numpadStartShortcut->setContext(Qt::ApplicationShortcut);
    m_stepBackShortcut->setContext(Qt::ApplicationShortcut);
    m_stepForwardShortcut->setContext(Qt::ApplicationShortcut);
    m_stepFastForwardShortcut->setContext(Qt::ApplicationShortcut);
    m_stepFastBackShortcut->setContext(Qt::ApplicationShortcut);
    m_insertionFollowsPlaybackShortcut->setContext(Qt::ApplicationShortcut);
    m_copyShortcut->setContext(Qt::ApplicationShortcut);
    m_pasteShortcut->setContext(Qt::ApplicationShortcut);
    m_cutShortcut->setContext(Qt::ApplicationShortcut);
    m_undoShortcut->setContext(Qt::ApplicationShortcut);
    m_redoShortcut->setContext(Qt::ApplicationShortcut);
    m_selectAllShortcut->setContext(Qt::ApplicationShortcut);
    m_nodeStartShortcut->setContext(Qt::ApplicationShortcut);
    m_nodeEndShortcut->setContext(Qt::ApplicationShortcut);
    m_selectNextNodeShortcut->setContext(Qt::ApplicationShortcut);
    m_showTimelineShortcut->setContext(Qt::ApplicationShortcut);
    m_showClipEditorShortcut->setContext(Qt::ApplicationShortcut);
    m_showMixShortcut->setContext(Qt::ApplicationShortcut);
    m_trimNodeShortcut->setContext(Qt::ApplicationShortcut);
    m_autoPanShortcut->setContext(Qt::ApplicationShortcut);
    m_audioPoolShortcut->setContext(Qt::ApplicationShortcut);
    m_toggleNodeNameShortcut->setContext(Qt::ApplicationShortcut);
    m_deleteShortcut->setContext(Qt::ApplicationShortcut);
    m_unselectAllShortcut->setContext(Qt::ApplicationShortcut);
    new QShortcut(QKeySequence::New, this, [this]()
    {
        if (m_newProjectAction)
        {
            m_newProjectAction->trigger();
        }
    }, Qt::ApplicationShortcut);
    new QShortcut(QKeySequence::Open, this, [this]()
    {
        if (m_openProjectAction)
        {
            m_openProjectAction->trigger();
        }
    }, Qt::ApplicationShortcut);
    new QShortcut(QKeySequence::Save, this, [this]()
    {
        if (m_saveProjectAction)
        {
            m_saveProjectAction->trigger();
        }
    }, Qt::ApplicationShortcut);
    new QShortcut(QKeySequence::SaveAs, this, [this]()
    {
        if (m_saveProjectAsAction)
        {
            m_saveProjectAsAction->trigger();
        }
    }, Qt::ApplicationShortcut);

    m_shellLayoutController = new ShellLayoutController(this);
    m_videoViewportQuickController = new VideoViewportQuickController(this);
    m_timelineQuickController = new TimelineQuickController(this);
    ensureClipEditorQuickTypesRegistered();
    m_clipEditorQuickController = new ClipEditorQuickController(this);
    m_mixQuickController = new MixQuickController(this);
    m_audioPoolQuickController = new AudioPoolQuickController(*this, this);
    m_audioPoolQuickController->setShowLength(m_audioPoolShowLength);
    m_audioPoolQuickController->setShowSize(m_audioPoolShowSize);
    m_contextMenuController = new ContextMenuController(this);
    m_dialogController = new DialogController(this);
    m_filePickerController = new FilePickerController(this);
    m_shellOverlayController = new ShellOverlayController(this);

    m_shellLayoutController->setPreferredSizes(
        m_audioPoolPreferredWidth,
        m_timelinePreferredHeight,
        m_clipEditorPreferredHeight,
        m_mixPreferredHeight);

    configureQuickEngine(*engine());
    engine()->addImageProvider(
        QStringLiteral("videoViewport"),
        new VideoViewportImageProvider(*m_videoViewportQuickController));
    engine()->addImageProvider(
        QStringLiteral("timeline-thumbnail"),
        new TimelineThumbnailProvider());
    rootContext()->setContextProperty(QStringLiteral("actionRegistry"), m_actionRegistry);
    rootContext()->setContextProperty(QStringLiteral("windowChrome"), m_windowChromeController);
    rootContext()->setContextProperty(QStringLiteral("shellLayoutController"), m_shellLayoutController);
    rootContext()->setContextProperty(QStringLiteral("videoViewportController"), m_videoViewportQuickController);
    rootContext()->setContextProperty(QStringLiteral("videoViewportBridge"), this);
    rootContext()->setContextProperty(QStringLiteral("timelineController"), m_timelineQuickController);
    rootContext()->setContextProperty(QStringLiteral("clipEditorController"), m_clipEditorQuickController);
    rootContext()->setContextProperty(QStringLiteral("mixController"), m_mixQuickController);
    rootContext()->setContextProperty(QStringLiteral("audioPoolController"), m_audioPoolQuickController);
    rootContext()->setContextProperty(QStringLiteral("contextMenuController"), m_contextMenuController);
    rootContext()->setContextProperty(QStringLiteral("dialogController"), m_dialogController);
    rootContext()->setContextProperty(QStringLiteral("filePickerController"), m_filePickerController);
    rootContext()->setContextProperty(QStringLiteral("shellOverlay"), m_shellOverlayController);
    setSource(appShellUrl());
    if (status() == QQuickView::Error)
    {
        for (const auto& error : errors())
        {
            qWarning().noquote() << "Quick shell error:" << error.toString();
        }
    }

    m_shellRootItem = qobject_cast<QQuickItem*>(rootObject());
    m_titleBarItem = m_shellRootItem ? m_shellRootItem->findChild<QQuickItem*>(QStringLiteral("quickTitleBar")) : nullptr;
    m_contentAreaItem = m_shellRootItem ? m_shellRootItem->findChild<QQuickItem*>(QStringLiteral("shellContentArea")) : nullptr;
    m_videoViewportQuickWidget =
        m_shellRootItem ? m_shellRootItem->findChild<QQuickItem*>(QStringLiteral("videoViewportScene")) : nullptr;
    m_timelineQuickWidget =
        m_shellRootItem ? m_shellRootItem->findChild<QQuickItem*>(QStringLiteral("timelineScene")) : nullptr;
    m_clipEditorQuickWidget =
        m_shellRootItem ? m_shellRootItem->findChild<QQuickItem*>(QStringLiteral("clipEditorScene")) : nullptr;
    m_mixQuickWidget =
        m_shellRootItem ? m_shellRootItem->findChild<QQuickItem*>(QStringLiteral("mixScene")) : nullptr;
    m_audioPoolQuickWidget =
        m_shellRootItem ? m_shellRootItem->findChild<QQuickItem*>(QStringLiteral("audioPoolScene")) : nullptr;
    m_contextMenuQuickWidget =
        m_shellRootItem ? m_shellRootItem->findChild<QQuickItem*>(QStringLiteral("contextMenuOverlay")) : nullptr;
    m_dialogOverlayQuickWidget =
        m_shellRootItem ? m_shellRootItem->findChild<QQuickItem*>(QStringLiteral("dialogOverlay")) : nullptr;
    m_filePickerQuickWidget =
        m_shellRootItem ? m_shellRootItem->findChild<QQuickItem*>(QStringLiteral("filePickerOverlay")) : nullptr;
    m_shellOverlayQuickWidget =
        m_shellRootItem ? m_shellRootItem->findChild<QQuickItem*>(QStringLiteral("shellOverlayScene")) : nullptr;

    if (m_titleBarItem)
    {
        m_windowChromeController->setTitleBarHeight(static_cast<int>(std::lround(m_titleBarItem->height())));
    }
    connect(
        m_shellLayoutController,
        &ShellLayoutController::layoutChanged,
        this,
        &MainWindow::syncShellPanelGeometry);
    connect(
        m_shellLayoutController,
        &ShellLayoutController::preferredSizesChanged,
        this,
        [this](const int audioPoolWidth, const int timelineHeight, const int clipEditorHeight, const int mixHeight)
        {
            m_audioPoolPreferredWidth = std::max(240, audioPoolWidth);
            m_timelinePreferredHeight = std::max(timelineMinimumHeight(), timelineHeight);
            m_clipEditorPreferredHeight = std::max(148, clipEditorHeight);
            m_mixPreferredHeight = std::max(132, mixHeight);
            if (!m_projectStateChangeInProgress && hasOpenProject())
            {
                setProjectDirty(true);
            }
        });

    m_nativeViewport = new NativeVideoViewport(nullptr);
    m_nativeViewport->setWindowTitle(QStringLiteral("Native Video Viewport Test"));
    m_nativeViewport->resize(960, 540);
    m_nativeViewport->hide();
    m_nativeViewport->installEventFilter(this);
    m_nativeViewport->setRenderService(nullptr);
    m_nativeViewportWindow = m_nativeViewport;
    connect(this, &QObject::destroyed, m_nativeViewportWindow, &QObject::deleteLater);
    m_detachedVideoWindow = new QQuickView();
    m_detachedVideoWindow->setTitle(QStringLiteral("Detached Video"));
    m_detachedVideoWindow->setIcon(icon());
    m_detachedVideoWindow->setColor(QColor(QStringLiteral("#0c1016")));
    m_detachedVideoWindow->setResizeMode(QQuickView::SizeRootObjectToView);
    configureQuickEngine(*m_detachedVideoWindow->engine());
    m_detachedVideoWindow->engine()->addImageProvider(
        QStringLiteral("videoViewport"),
        new VideoViewportImageProvider(*m_videoViewportQuickController));
    m_detachedVideoWindow->rootContext()->setContextProperty(
        QStringLiteral("videoViewportController"),
        m_videoViewportQuickController);
    m_detachedVideoWindow->rootContext()->setContextProperty(
        QStringLiteral("videoViewportBridge"),
        this);
    m_detachedVideoWindow->setSource(videoViewportSceneUrl());
    m_detachedVideoWindow->hide();
    m_detachedVideoWindow->installEventFilter(this);

    if (m_shellLayoutController)
    {
        m_shellLayoutController->setTimelineMinimumHeight(timelineMinimumHeight());
        m_shellLayoutController->setTimelineVisible(true);
        m_shellLayoutController->setClipEditorVisible(false);
        m_shellLayoutController->setMixVisible(false);
        m_shellLayoutController->setAudioPoolVisible(false);
        m_shellLayoutController->setVideoDetached(false);
        m_shellLayoutController->setPreferredSizes(
            m_audioPoolPreferredWidth,
            m_timelinePreferredHeight,
            m_clipEditorPreferredHeight,
            m_mixPreferredHeight);
    }

    syncShellLayoutViewport();

    connect(m_contextMenuController, &ContextMenuController::changed, this, [this]()
    {
        updateOverlayPositions();
    });
    connect(m_contextMenuController, &ContextMenuController::itemTriggered, this, [this](const QString& key)
    {
        if (key == QStringLiteral("node.rename"))
        {
            const auto text = m_dialogController
                ? m_dialogController->execTextInput(
                    QStringLiteral("Rename Node"),
                    QStringLiteral("Node name"),
                    m_contextMenuNodeLabel)
                : std::optional<QString>{};
            if (text.has_value())
            {
                const auto updatedLabel = text->trimmed();
                if (!updatedLabel.isEmpty() && updatedLabel != m_contextMenuNodeLabel && !m_contextMenuTrackId.isNull())
                {
                    m_controller->renameTrack(m_contextMenuTrackId, updatedLabel);
                }
            }
            return;
        }
        if (key == QStringLiteral("node.importAudio"))
        {
            importSound();
            return;
        }
        if (key == QStringLiteral("node.trim"))
        {
            trimSelectedNodeToSound();
            return;
        }
        if (key == QStringLiteral("node.autoPan"))
        {
            toggleSelectedNodeAutoPan();
            return;
        }
        if (key == QStringLiteral("loop.delete"))
        {
            clearLoopRange();
        }
    });

    connect(m_dialogController, &DialogController::changed, this, [this]()
    {
        updateOverlayPositions();
    });

    connect(m_filePickerController, &FilePickerController::changed, this, [this]()
    {
        updateOverlayPositions();
    });
    connect(m_shellOverlayController, &ShellOverlayController::changed, this, [this]()
    {
        if (!m_shellOverlayController->trackGainPopupVisible())
        {
            m_trackGainPopupTrackId = {};
        }
    });
    connect(m_shellOverlayController, &ShellOverlayController::trackGainSliderValueChanged, this, [this](const int sliderValue)
    {
        if (m_trackGainPopupTrackId.isNull())
        {
            return;
        }

        const auto gainDb = mixGainSliderValueToDb(sliderValue);
        updateTrackMixGainPopupValue(gainDb);
        if (!m_controller->setMixLaneGainForTrack(m_trackGainPopupTrackId, gainDb))
        {
            return;
        }

        refreshMixView();
        if (!m_projectStateChangeInProgress && hasOpenProject())
        {
            setProjectDirty(true);
        }
    });

    auto* debugOverlay = new DebugOverlayWindow();
    m_debugOverlay = debugOverlay;
    connect(this, &QObject::destroyed, debugOverlay, &QObject::deleteLater);
    m_debugOverlay->setTransientParent(this);
    m_debugOverlay->setPosition(mapToGlobal(QPoint(16, 48)));
    m_debugOverlay->setVisible(m_debugVisible);
    connect(debugOverlay, &DebugOverlayWindow::closeRequested, this, [this]()
    {
        updateDebugVisibility(false);
        showStatus(QStringLiteral("Debug window hidden."));
    });

    connect(this, &QQuickWindow::sceneGraphInitialized, this, [this]()
    {
        updateMixQuickDiagnostics();
    });
    handleTimelineQuickStatusChanged();
    handleClipEditorQuickStatusChanged();
    handleMixQuickStatusChanged();
    updateOverlayPositions();
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
