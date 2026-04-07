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
#include <QQuickImageProvider>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QQmlContext>
#include <QSGRendererInterface>
#include <QScrollArea>
#include <QScreen>
#include <QScopedValueRollback>
#include <QSettings>
#include <QShortcut>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QThread>
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
#include "app/ShellLayoutController.h"
#include "app/ShellOverlayController.h"
#include "app/PanelLayoutController.h"
#include "app/DebugUiController.h"
#include "app/MediaImportController.h"
#include "app/NodeDocument.h"
#include "app/ProjectDocument.h"
#include "app/ProjectTimelineThumbnails.h"
#include "app/TransportUiSyncController.h"
#include "app/VideoProxyController.h"
#include "app/WindowChromeController.h"
#include "core/audio/AudioDurationProbe.h"
#include "ui/ClipWaveformQuickItem.h"
#include "ui/DebugOverlayWindow.h"
#include "ui/MixQuickController.h"
#include "ui/NodeEditorQuickController.h"
#include "ui/NativeVideoViewport.h"
#include "ui/QuickEngineSupport.h"
#include "ui/ThumbnailStripQuickController.h"
#include "ui/TimelineQuickController.h"
#include "ui/TimelineThumbnailCache.h"
#include "ui/VideoOverlayQuickItem.h"
#include "ui/VideoViewportQuickItem.h"
#include "ui/VideoViewportQuickController.h"
#include <qqml.h>

namespace
{
QString selectedNodeDisplayLabel(PlayerController* controller)
{
    if (!controller)
    {
        return {};
    }

    const auto selectedTrackId = controller->selectedTrackId();
    if (selectedTrackId.isNull())
    {
        return {};
    }

    const auto label = controller->trackLabel(selectedTrackId).trimmed();
    return label.isEmpty() ? QStringLiteral("Node") : label;
}

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

QUrl quickTitleBarUrl()
{
    return QUrl(QStringLiteral("qrc:/qml/QuickTitleBar.qml"));
}

QUrl mixSceneUrl()
{
    return QUrl(QStringLiteral("qrc:/qml/MixScene.qml"));
}

QUrl shellLayoutSceneUrl()
{
    return QUrl(QStringLiteral("qrc:/qml/ShellLayoutScene.qml"));
}

QUrl timelineSceneUrl()
{
    return QUrl(QStringLiteral("qrc:/qml/TimelineScene.qml"));
}

QUrl thumbnailStripSceneUrl()
{
    return QUrl(QStringLiteral("qrc:/qml/ThumbnailStripScene.qml"));
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

dawg::node::AudioClipData nodeAudioClipFromClipState(const AudioClipPreviewState& state, const QString& fallbackLabel)
{
    dawg::node::AudioClipData clip;
    clip.label = state.label.trimmed().isEmpty() ? fallbackLabel : state.label.trimmed();
    if (state.hasAttachedAudio)
    {
        clip.attachedAudio = AudioAttachment{
            .assetPath = state.assetPath,
            .gainDb = state.gainDb,
            .clipStartMs = state.clipStartMs,
            .clipEndMs = state.clipEndMs,
            .loopEnabled = false
        };
    }
    return clip;
}

std::optional<AudioClipPreviewState> clipStateFromNodeAudioClip(
    const dawg::node::AudioClipData& clip,
    const QString& fallbackLabel)
{
    if (!clip.attachedAudio.has_value() || clip.attachedAudio->assetPath.isEmpty())
    {
        return std::nullopt;
    }

    const auto durationMs = dawg::audio::probeAudioDurationMs(clip.attachedAudio->assetPath);
    if (!durationMs.has_value() || *durationMs <= 0)
    {
        return std::nullopt;
    }

    const auto clipStartMs = std::clamp(clip.attachedAudio->clipStartMs, 0, *durationMs);
    const auto clipEndMs = std::clamp(
        clip.attachedAudio->clipEndMs.value_or(*durationMs),
        clipStartMs + 1,
        *durationMs);
    AudioClipPreviewState state;
    state.label = clip.label.trimmed().isEmpty() ? fallbackLabel : clip.label.trimmed();
    state.assetPath = clip.attachedAudio->assetPath;
    state.clipStartMs = clipStartMs;
    state.clipEndMs = clipEndMs;
    state.sourceDurationMs = *durationMs;
    state.playheadMs = clipStartMs;
    state.gainDb = clip.attachedAudio->gainDb;
    state.hasAttachedAudio = true;
    state.loopEnabled = false;
    return state;
}

using AudioChannelCountFn = std::function<std::optional<int>(const QString&)>;

QVariantList nodeTrackItemsFromDocument(
    const dawg::node::Document& document,
    const int nodeDurationMs,
    const AudioChannelCountFn& channelCountForPath)
{
    QVariantList items;
    const auto safeNodeDurationMs = std::max(1, nodeDurationMs);
    for (int laneIndex = 0; laneIndex < static_cast<int>(document.node.lanes.size()); ++laneIndex)
    {
        const auto& lane = document.node.lanes[static_cast<std::size_t>(laneIndex)];
        const auto title = lane.label.trimmed().isEmpty()
            ? QStringLiteral("Lane %1").arg(laneIndex + 1)
            : lane.label.trimmed();
        const auto clipCount = static_cast<int>(lane.audioClips.size());
        const auto subtitle = clipCount == 0
            ? QStringLiteral("Empty lane")
            : QStringLiteral("%1 audio clip(s)").arg(clipCount);

        QVariantList clipItems;
        QVariantMap waveformState;
        bool laneUsesStereoMeter = false;
        for (int clipIndex = 0; clipIndex < static_cast<int>(lane.audioClips.size()); ++clipIndex)
        {
            const auto& clip = lane.audioClips[static_cast<std::size_t>(clipIndex)];
            if (!clip.attachedAudio.has_value() || clip.attachedAudio->assetPath.isEmpty())
            {
                continue;
            }

            if (!laneUsesStereoMeter && channelCountForPath)
            {
                const auto channelCount = channelCountForPath(clip.attachedAudio->assetPath);
                laneUsesStereoMeter = channelCount.has_value() && *channelCount > 1;
            }
            const auto clipTitle = clip.label.trimmed().isEmpty()
                ? QStringLiteral("Audio Clip %1").arg(clipIndex + 1)
                : clip.label.trimmed();
            const auto clipFileName = QFileInfo(clip.attachedAudio->assetPath).fileName();
            QVariantMap clipWaveformState;
            int clipDurationMs = 1;
            double clipOffsetRatio = std::clamp(
                static_cast<double>(clip.laneOffsetMs) / static_cast<double>(safeNodeDurationMs),
                0.0,
                1.0);
            double clipWidthRatio = 0.0;
            const auto durationMs = dawg::audio::probeAudioDurationMs(clip.attachedAudio->assetPath);
            if (durationMs.has_value() && *durationMs > 0)
            {
                const auto clipStartMs = std::clamp(
                    clip.attachedAudio->clipStartMs,
                    0,
                    std::max(0, *durationMs - 1));
                const auto clipEndMs = std::clamp(
                    clip.attachedAudio->clipEndMs.value_or(*durationMs),
                    clipStartMs + 1,
                    *durationMs);
                clipDurationMs = std::max(1, clipEndMs - clipStartMs);
                clipWidthRatio = std::clamp(
                    static_cast<double>(clipDurationMs) / static_cast<double>(safeNodeDurationMs),
                    0.0,
                    1.0 - clipOffsetRatio);
                clipWaveformState = QVariantMap{
                    {QStringLiteral("label"), clipTitle},
                    {QStringLiteral("assetPath"), clip.attachedAudio->assetPath},
                    {QStringLiteral("clipStartMs"), clipStartMs},
                    {QStringLiteral("clipEndMs"), clipEndMs},
                    {QStringLiteral("sourceDurationMs"), *durationMs},
                    {QStringLiteral("playheadMs"), clipStartMs},
                    {QStringLiteral("gainDb"), clip.attachedAudio->gainDb},
                    {QStringLiteral("hasAttachedAudio"), true},
                    {QStringLiteral("loopEnabled"), false}
                };
                if (waveformState.isEmpty())
                {
                    waveformState = clipWaveformState;
                }
            }
            clipItems.push_back(QVariantMap{
                {QStringLiteral("clipId"), clip.id},
                {QStringLiteral("title"), clipTitle},
                {QStringLiteral("subtitle"), clipFileName.isEmpty() ? QStringLiteral("Embedded audio") : clipFileName},
                {QStringLiteral("laneOffsetMs"), clip.laneOffsetMs},
                {QStringLiteral("clipDurationMs"), clipDurationMs},
                {QStringLiteral("clipOffsetRatio"), clipOffsetRatio},
                {QStringLiteral("clipWidthRatio"), clipWidthRatio},
                {QStringLiteral("hasWaveform"), !clipWaveformState.isEmpty()},
                {QStringLiteral("waveformState"), clipWaveformState}
            });
        }

        items.push_back(QVariantMap{
            {QStringLiteral("laneId"), lane.id},
            {QStringLiteral("title"), title},
            {QStringLiteral("subtitle"), subtitle},
            {QStringLiteral("primary"), laneIndex == 0},
            {QStringLiteral("muted"), lane.muted},
            {QStringLiteral("soloed"), lane.soloed},
            {QStringLiteral("useStereoMeter"), laneUsesStereoMeter},
            {QStringLiteral("clipCount"), clipCount},
            {QStringLiteral("clips"), clipItems},
            {QStringLiteral("hasWaveform"), !waveformState.isEmpty()},
            {QStringLiteral("waveformState"), waveformState}
        });
    }
    return items;
}

QUuid uuidFromNodeDocumentId(const QString& id)
{
    auto cleaned = id.trimmed();
    if (cleaned.isEmpty())
    {
        return QUuid{};
    }
    if (!cleaned.startsWith(QLatin1Char('{')))
    {
        cleaned = QStringLiteral("{%1}").arg(cleaned);
    }
    return QUuid(cleaned);
}

std::vector<AudioPlaybackCoordinator::NodePreviewClip> nodePreviewClipsFromDocument(
    const dawg::node::Document& document,
    const AudioChannelCountFn& channelCountForPath)
{
    std::vector<AudioPlaybackCoordinator::NodePreviewClip> clips;
    const auto anyLaneSoloed = std::any_of(
        document.node.lanes.cbegin(),
        document.node.lanes.cend(),
        [](const dawg::node::LaneData& lane)
        {
            return lane.soloed;
        });
    for (const auto& lane : document.node.lanes)
    {
        if (lane.muted || (anyLaneSoloed && !lane.soloed))
        {
            continue;
        }

        for (const auto& clip : lane.audioClips)
        {
            if (!clip.attachedAudio.has_value() || clip.attachedAudio->assetPath.isEmpty())
            {
                continue;
            }

            const auto durationMs = dawg::audio::probeAudioDurationMs(clip.attachedAudio->assetPath);
            if (!durationMs.has_value() || *durationMs <= 0)
            {
                continue;
            }

            const auto clipStartMs = std::clamp(
                clip.attachedAudio->clipStartMs,
                0,
                std::max(0, *durationMs - 1));
            const auto clipEndMs = std::clamp(
                clip.attachedAudio->clipEndMs.value_or(*durationMs),
                clipStartMs + 1,
                *durationMs);
            const auto previewTrackId = uuidFromNodeDocumentId(clip.id);
            if (previewTrackId.isNull())
            {
                continue;
            }
            const auto channelCount = channelCountForPath
                ? channelCountForPath(clip.attachedAudio->assetPath)
                : std::optional<int>{};

            clips.push_back(AudioPlaybackCoordinator::NodePreviewClip{
                .previewTrackId = previewTrackId,
                .laneId = lane.id,
                .assetPath = clip.attachedAudio->assetPath,
                .laneOffsetMs = clip.laneOffsetMs,
                .clipStartMs = clipStartMs,
                .clipEndMs = clipEndMs,
                .gainDb = clip.attachedAudio->gainDb,
                .loopEnabled = false,
                .useStereoMeter = channelCount.has_value() && *channelCount > 1
            });
        }
    }
    return clips;
}

bool materializeNodeClipAudio(
    dawg::node::AudioClipData& clip,
    const QString& audioDirectoryPath,
    QString* errorMessage)
{
    if (!clip.attachedAudio.has_value())
    {
        return true;
    }

    if (!QDir().mkpath(audioDirectoryPath))
    {
        if (errorMessage)
        {
            *errorMessage = QStringLiteral("Failed to create the project audio folder.");
        }
        return false;
    }

    if (!clip.embeddedAudioData.isEmpty())
    {
        const auto embeddedAudioFileName = clip.embeddedAudioFileName.isEmpty()
            ? QStringLiteral("node-audio.wav")
            : clip.embeddedAudioFileName;
        const auto targetAudioPath = uniqueTargetFilePath(audioDirectoryPath, embeddedAudioFileName);
        QFile audioFile(targetAudioPath);
        if (!audioFile.open(QIODevice::WriteOnly)
            || audioFile.write(clip.embeddedAudioData) != clip.embeddedAudioData.size())
        {
            if (errorMessage)
            {
                *errorMessage = QStringLiteral("Failed to materialize embedded node audio.");
            }
            return false;
        }
        audioFile.close();
        clip.attachedAudio->assetPath = QDir::cleanPath(targetAudioPath);
        return true;
    }

    const auto assetPath = clip.attachedAudio->assetPath;
    if (assetPath.isEmpty() || !QFileInfo::exists(assetPath))
    {
        if (errorMessage)
        {
            *errorMessage = QStringLiteral("The node contains an audio clip that could not be found.");
        }
        return false;
    }

    const auto targetAudioPath = uniqueTargetFilePath(audioDirectoryPath, assetPath);
    if (!QFile::copy(assetPath, targetAudioPath))
    {
        if (errorMessage)
        {
            *errorMessage = QStringLiteral("Failed to gather node audio into the project.");
        }
        return false;
    }

    clip.attachedAudio->assetPath = QDir::cleanPath(targetAudioPath);
    return true;
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
    m_nodeEditorPreviewMeterTimer.setTimerType(Qt::PreciseTimer);
    m_nodeEditorPreviewMeterTimer.setInterval(16);
    connect(&m_clearAllShortcutTimer, &QTimer::timeout, this, &MainWindow::clearPendingClearAllShortcut);
    connect(&m_memoryUsageTimer, &QTimer::timeout, this, &MainWindow::updateMemoryUsage);
    connect(&m_mixMeterTimer, &QTimer::timeout, this, &MainWindow::updateMixMeterLevels);
    connect(&m_nodeEditorPreviewMeterTimer, &QTimer::timeout, this, &MainWindow::updateNodeEditorPreviewMeters);
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

            refreshNodeEditor();
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
    connect(m_playAction, &QAction::triggered, this, [this]()
    {
        if (nodeEditorHasFocus())
        {
            toggleNodeEditorPreview();
            return;
        }
        stopNodeEditorPreview();
        m_controller->togglePlayback();
    });
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
    connect(m_timelineQuickController, &TimelineQuickController::frameRequested, m_controller, &PlayerController::seekToFrame);
    connect(m_thumbnailStripQuickController, &ThumbnailStripQuickController::frameRequested, m_controller, &PlayerController::seekToFrame);
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
    connect(m_playPauseShortcut, &QShortcut::activated, this, [this]()
    {
        if (nodeEditorHasFocus())
        {
            toggleNodeEditorPreview();
            return;
        }
        stopNodeEditorPreview();
        m_controller->togglePlayback();
    });
    connect(m_startShortcut, &QShortcut::activated, this, [this]()
    {
        if (nodeEditorHasFocus())
        {
            resetNodeEditorPlayheadToStart();
            return;
        }
        m_controller->goToStart();
    });
    connect(m_numpadStartShortcut, &QShortcut::activated, this, [this]()
    {
        if (nodeEditorHasFocus())
        {
            resetNodeEditorPlayheadToStart();
            return;
        }
        m_controller->goToStart();
    });
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
            toggleNodeEditorPreview();
            return true;
        }
        if (!keyEvent->isAutoRepeat()
            && (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter)
            && keyEvent->modifiers() == Qt::NoModifier
            && nodeEditorFocused())
        {
            resetNodeEditorPlayheadToStart();
            return true;
        }
        if (!keyEvent->isAutoRepeat()
            && keyEvent->key() == Qt::Key_Backspace
            && keyEvent->modifiers() == Qt::NoModifier
            && nodeEditorFocused())
        {
            static_cast<void>(deleteSelectedNodeEditorSelection());
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
    stopNodeEditorPreview();
    m_nodeEditorWaveformItem = nullptr;
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
    handleNodeEditorFileAction(actionKey);
}

void MainWindow::requestNodeEditorAudioAction(const QString& actionKey)
{
    handleNodeEditorAudioAction(actionKey);
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

void MainWindow::importSound()
{
    m_mediaImportController->importSound();
}

void MainWindow::importAudioToPool()
{
    m_mediaImportController->importAudioToPool();
}

void MainWindow::handleNodeEditorFileAction(const QString& actionKey)
{
    if (!hasOpenProject() || !m_controller)
    {
        return;
    }

    const auto nodesDirectoryPath = projectNodesDirectoryPath();
    if (nodesDirectoryPath.isEmpty())
    {
        return;
    }
    static_cast<void>(QDir().mkpath(nodesDirectoryPath));

    if (actionKey == QStringLiteral("save"))
    {
        if (!m_controller->hasSelection())
        {
            showStatus(QStringLiteral("Select a node before saving it."));
            return;
        }
        const auto selectedTrackId = m_controller->selectedTrackId();
        const auto boundNodeDocumentPath = selectedTrackId.isNull()
            ? QString{}
            : m_controller->trackNodeDocumentPath(selectedTrackId).trimmed();
        const auto nodeLabel = selectedTrackId.isNull()
            ? QStringLiteral("Node")
            : m_controller->trackLabel(selectedTrackId).trimmed();
        const auto nodeFilePath = !boundNodeDocumentPath.isEmpty()
            ? QDir::cleanPath(boundNodeDocumentPath)
            : QDir(nodesDirectoryPath).filePath(
                dawg::node::nodeFileNameForName(nodeLabel.isEmpty() ? QStringLiteral("Node") : nodeLabel));
        static_cast<void>(saveSelectedNodeToFile(nodeFilePath));
        return;
    }

    if (actionKey == QStringLiteral("saveAs"))
    {
        if (!m_controller->hasSelection())
        {
            showStatus(QStringLiteral("Select a node before saving it."));
            return;
        }
        const auto selectedTrackId = m_controller->selectedTrackId();
        const auto nodeLabel = selectedTrackId.isNull()
            ? QStringLiteral("Node")
            : m_controller->trackLabel(selectedTrackId).trimmed();
        const auto selectedNodeFilePath = m_filePickerController
            ? m_filePickerController->execSaveFile(
                QStringLiteral("Save Node As"),
                nodesDirectoryPath,
                dawg::node::nodeFileNameForName(nodeLabel.isEmpty() ? QStringLiteral("Node") : nodeLabel),
                QStringLiteral("DAWG Nodes (*%1)").arg(QString::fromLatin1(dawg::node::kNodeFileSuffix)))
            : QString{};
        if (!selectedNodeFilePath.isEmpty())
        {
            const auto savedNodeLabel = QFileInfo(selectedNodeFilePath).completeBaseName().trimmed();
            static_cast<void>(saveSelectedNodeToFile(
                selectedNodeFilePath,
                true,
                savedNodeLabel.isEmpty() ? QStringLiteral("Node") : savedNodeLabel));
        }
        return;
    }

    if (actionKey == QStringLiteral("open"))
    {
        const auto selectedNodeFilePath = chooseOpenFileName(
            QStringLiteral("Open Node"),
            nodesDirectoryPath,
            QStringLiteral("DAWG Nodes (*%1 *%2);;All Files (*.*)")
                .arg(
                    QString::fromLatin1(dawg::node::kNodeFileSuffix),
                    QString::fromLatin1(dawg::node::kLegacyNodeFileSuffix)));
        if (!selectedNodeFilePath.isEmpty())
        {
            static_cast<void>(openNodeFileAsNewNode(selectedNodeFilePath));
        }
        return;
    }

    if (actionKey == QStringLiteral("export"))
    {
        if (!m_controller->hasSelection())
        {
            showStatus(QStringLiteral("Select a node before exporting it."));
            return;
        }
        const auto selectedTrackId = m_controller->selectedTrackId();
        const auto nodeLabel = selectedTrackId.isNull()
            ? QStringLiteral("Node")
            : m_controller->trackLabel(selectedTrackId).trimmed();
        const auto selectedNodeFilePath = m_filePickerController
            ? m_filePickerController->execSaveFile(
                QStringLiteral("Export Node"),
                nodesDirectoryPath,
                dawg::node::nodeFileNameForName(nodeLabel.isEmpty() ? QStringLiteral("Node") : nodeLabel),
                QStringLiteral("DAWG Nodes (*%1)").arg(QString::fromLatin1(dawg::node::kNodeFileSuffix)))
            : QString{};
        if (!selectedNodeFilePath.isEmpty())
        {
            static_cast<void>(saveSelectedNodeToFile(selectedNodeFilePath, false));
        }
    }
}

void MainWindow::handleNodeEditorAudioAction(const QString& actionKey)
{
    const auto isImportAction = actionKey == QStringLiteral("import");
    const auto isNewLaneAction = actionKey == QStringLiteral("newLane") || actionKey == QStringLiteral("createTrack");
    if (!isImportAction && !isNewLaneAction)
    {
        return;
    }

    if (!ensureProjectForMediaAction(isImportAction ? QStringLiteral("import audio") : QStringLiteral("create a node track")))
    {
        return;
    }

    if (!m_controller || !m_controller->hasSelection())
    {
        showStatus(isImportAction
            ? QStringLiteral("Select a node before importing audio.")
            : QStringLiteral("Select a node before creating a track."));
        return;
    }

    QString errorMessage;
    std::optional<QString> copiedFilePath;
    QFileInfo importedAudioInfo;
    if (isImportAction)
    {
        const auto filePath = chooseOpenFileName(
            QStringLiteral("Import Audio"),
            QStandardPaths::writableLocation(QStandardPaths::MusicLocation),
            QStringLiteral("Audio Files (*.wav *.mp3 *.flac *.aif *.aiff *.m4a *.aac *.ogg);;All Files (*.*)"));
        if (filePath.isEmpty())
        {
            return;
        }

        copiedFilePath = copyMediaIntoProject(filePath, QStringLiteral("audio"), &errorMessage);
        if (!copiedFilePath.has_value())
        {
            m_dialogController->execMessage(
                QStringLiteral("Import Audio"),
                errorMessage,
                {},
                {DialogController::Button::Ok});
            return;
        }

        importedAudioInfo = QFileInfo(*copiedFilePath);
        static_cast<void>(m_controller->importAudioToPool(*copiedFilePath));
    }

    const auto selectedTrackId = m_controller->selectedTrackId();
    if (selectedTrackId.isNull())
    {
        showStatus(isImportAction
            ? QStringLiteral("Select a node before importing audio.")
            : QStringLiteral("Select a node before creating a lane."));
        return;
    }

    const auto nodesDirectoryPath = projectNodesDirectoryPath();
    if (nodesDirectoryPath.isEmpty() || !QDir().mkpath(nodesDirectoryPath))
    {
        showStatus(QStringLiteral("Failed to create the project nodes folder."));
        return;
    }

    auto boundNodeDocumentPath = m_controller->trackNodeDocumentPath(selectedTrackId);
    dawg::node::Document nodeDocument;
    if (!boundNodeDocumentPath.isEmpty() && QFileInfo::exists(boundNodeDocumentPath))
    {
        const auto loadedDocument = dawg::node::loadDocument(boundNodeDocumentPath, &errorMessage);
        if (!loadedDocument.has_value())
        {
            m_dialogController->execMessage(
                isImportAction ? QStringLiteral("Import Audio") : QStringLiteral("Create Track"),
                errorMessage,
                {},
                {DialogController::Button::Ok});
            return;
        }
        nodeDocument = *loadedDocument;
    }
    else
    {
        const auto nodeLabel = m_controller->trackLabel(selectedTrackId).trimmed().isEmpty()
            ? QStringLiteral("Node")
            : m_controller->trackLabel(selectedTrackId).trimmed();
        boundNodeDocumentPath = uniqueTargetFilePath(
            nodesDirectoryPath,
            dawg::node::nodeFileNameForName(nodeLabel));
        nodeDocument.name = nodeLabel;
        nodeDocument.node.label = nodeLabel;
        nodeDocument.node.autoPanEnabled = m_controller->selectedTracksAutoPanEnabled();
        nodeDocument.node.timelineFrameCount = m_controller->totalFrames();
        nodeDocument.node.timelineFps = m_controller->fps();
    }

    if (nodeDocument.node.label.trimmed().isEmpty())
    {
        nodeDocument.node.label = m_controller->trackLabel(selectedTrackId).trimmed();
    }
    if (nodeDocument.name.trimmed().isEmpty())
    {
        nodeDocument.name = nodeDocument.node.label.trimmed().isEmpty()
            ? QStringLiteral("Node")
            : nodeDocument.node.label.trimmed();
    }
    nodeDocument.node.timelineFrameCount = m_controller->totalFrames();
    nodeDocument.node.timelineFps = m_controller->fps();
    nodeDocument.node.autoPanEnabled = m_controller->selectedTracksAutoPanEnabled();

    QString selectedLaneId;
    if (m_nodeEditorQuickController)
    {
        selectedLaneId = m_nodeEditorQuickController->selectedLaneId();
    }

    auto laneIt = selectedLaneId.isEmpty()
        ? nodeDocument.node.lanes.end()
        : std::find_if(
            nodeDocument.node.lanes.begin(),
            nodeDocument.node.lanes.end(),
            [&selectedLaneId](const dawg::node::LaneData& lane)
            {
                return lane.id == selectedLaneId;
            });
    if (isNewLaneAction || laneIt == nodeDocument.node.lanes.end())
    {
        dawg::node::LaneData lane;
        lane.label = QStringLiteral("Track %1").arg(static_cast<int>(nodeDocument.node.lanes.size()) + 1);
        nodeDocument.node.lanes.push_back(lane);
        laneIt = std::prev(nodeDocument.node.lanes.end());
        selectedLaneId = laneIt->id;
    }

    if (isImportAction && copiedFilePath.has_value())
    {
        dawg::node::AudioClipData importedClip;
        importedClip.label = importedAudioInfo.completeBaseName().isEmpty()
            ? importedAudioInfo.fileName()
            : importedAudioInfo.completeBaseName();
        importedClip.attachedAudio = AudioAttachment{
            .assetPath = *copiedFilePath,
            .gainDb = 0.0F,
            .clipStartMs = 0,
            .clipEndMs = std::nullopt,
            .loopEnabled = false
        };
        laneIt->audioClips.push_back(importedClip);
    }

    if (!dawg::node::saveDocument(boundNodeDocumentPath, nodeDocument, &errorMessage))
    {
        m_dialogController->execMessage(
            isImportAction ? QStringLiteral("Import Audio") : QStringLiteral("Create Track"),
            errorMessage,
            {},
            {DialogController::Button::Ok});
        return;
    }

    static_cast<void>(m_controller->setTrackNodeDocument(
        selectedTrackId,
        QDir::cleanPath(boundNodeDocumentPath),
        nodeDocument.node.timelineFrameCount,
        nodeDocument.node.timelineFps));

    refreshAudioPool();
    if (m_nodeEditorQuickController)
    {
        m_nodeEditorQuickController->selectLane(selectedLaneId);
    }
    refreshNodeEditor();
    if (!m_projectStateChangeInProgress && hasOpenProject())
    {
        setProjectDirty(true);
    }
    if (isImportAction)
    {
        showStatus(QStringLiteral("Imported %1 into lane %2.")
            .arg(importedAudioInfo.fileName(), laneIt->label));
    }
    else
    {
        showStatus(QStringLiteral("Created %1.").arg(laneIt->label));
    }
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
    if (m_transportUiSyncController)
    {
        const QScopedValueRollback playbackUpdateGuard{m_nodeEditorPreviewUpdatingPlayhead, true};
        m_transportUiSyncController->syncNodeEditorPlayheadToProjectFrame(frameIndex);
    }
    if (m_nodeEditorPreviewActive && m_nodeEditorQuickController)
    {
        const auto playheadMs = std::clamp(
            m_nodeEditorQuickController->playheadMs(),
            0,
            std::max(0, m_nodeEditorPreviewNodeDurationMs));
        const auto selectedRange = m_controller ? m_controller->selectedTrackFrameRange() : std::nullopt;
        if (playheadMs >= m_nodeEditorPreviewNodeDurationMs
            || (selectedRange.has_value() && frameIndex >= selectedRange->second))
        {
            stopNodeEditorPreview(false);
        }
        else if (m_controller && shouldSyncNodeEditorPreviewAudio(playheadMs))
        {
            static_cast<void>(m_controller->syncNodeEditorPreview(
                m_nodeEditorPreviewClips,
                m_nodeEditorPreviewNodeDurationMs,
                playheadMs));
        }
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
    refreshNodeEditor();
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
    if (!playing && m_nodeEditorPreviewActive)
    {
        stopNodeEditorPreview(false);
    }
    if (m_nodeEditorQuickController)
    {
        m_nodeEditorQuickController->setPlaybackActive(playing || m_nodeEditorPreviewActive);
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
        stopNodeEditorPreview();
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
        && !m_timelineThumbnailGenerationThread
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
    if (m_currentProjectRootPath.isEmpty())
    {
        return;
    }

    TimelineThumbnailGenerationRequest request;
    request.projectRootPath = m_currentProjectRootPath;
    request.videoPath = m_controller
        ? (!m_controller->projectVideoPath().isEmpty() ? m_controller->projectVideoPath() : m_controller->loadedPath())
        : QString{};
    request.totalFrames = m_controller ? m_controller->totalFrames() : 0;
    request.fps = m_controller ? m_controller->fps() : 0.0;

    m_pendingTimelineThumbnailGenerationRequest = request;
    if (m_timelineThumbnailGenerationThread)
    {
        return;
    }

    startProjectTimelineThumbnailsGeneration(*m_pendingTimelineThumbnailGenerationRequest);
    m_pendingTimelineThumbnailGenerationRequest.reset();
}

void MainWindow::startProjectTimelineThumbnailsGeneration(const TimelineThumbnailGenerationRequest& request)
{
    const auto generationId = ++m_timelineThumbnailGenerationId;
    if (m_shellOverlayController)
    {
        m_shellOverlayController->showTopProgress(0.0);
    }
    QPointer<MainWindow> window(this);
    auto* thread = QThread::create([window, generationId, request]()
    {
        QString errorMessage;
        const bool success = dawg::timeline::ensureProjectTimelineThumbnails(
            request.projectRootPath,
            request.videoPath,
            request.totalFrames,
            request.fps,
            [window, generationId](const double progress)
            {
                if (!window)
                {
                    return;
                }

                QMetaObject::invokeMethod(
                    window,
                    [window, generationId, progress]()
                    {
                        if (!window
                            || generationId != window->m_timelineThumbnailGenerationId
                            || !window->m_shellOverlayController)
                        {
                            return;
                        }

                        window->m_shellOverlayController->showTopProgress(progress);
                    },
                    Qt::QueuedConnection);
            },
            &errorMessage);
        if (!window)
        {
            return;
        }
        QMetaObject::invokeMethod(
            window,
            [window, generationId, request, success, errorMessage]()
            {
                if (!window)
                {
                    return;
                }
                window->handleProjectTimelineThumbnailsGenerationFinished(
                    generationId,
                    request,
                    success,
                    errorMessage);
            },
            Qt::QueuedConnection);
    });

    m_timelineThumbnailGenerationThread = thread;
    connect(
        thread,
        &QThread::finished,
        thread,
        &QObject::deleteLater);
    thread->start();
}

void MainWindow::handleProjectTimelineThumbnailsGenerationFinished(
    const quint64 generationId,
    const TimelineThumbnailGenerationRequest& request,
    const bool success,
    const QString& errorMessage)
{
    if (generationId != m_timelineThumbnailGenerationId)
    {
        return;
    }

    if (m_shellOverlayController)
    {
        m_shellOverlayController->hideTopProgress();
    }

    if (m_timelineThumbnailGenerationThread)
    {
        m_timelineThumbnailGenerationThread = nullptr;
    }

    const bool requestMatchesCurrentProject =
        QDir::cleanPath(QDir::fromNativeSeparators(request.projectRootPath))
            == QDir::cleanPath(QDir::fromNativeSeparators(m_currentProjectRootPath))
        && QDir::cleanPath(QDir::fromNativeSeparators(request.videoPath))
            == QDir::cleanPath(QDir::fromNativeSeparators(
                m_controller
                    ? (!m_controller->projectVideoPath().isEmpty()
                           ? m_controller->projectVideoPath()
                           : m_controller->loadedPath())
                    : QString{}));

    if (!success)
    {
        if (!errorMessage.isEmpty() && requestMatchesCurrentProject)
        {
            showStatus(QStringLiteral("Timeline thumbnails unavailable: %1").arg(errorMessage));
        }
    }
    else if (requestMatchesCurrentProject)
    {
        refreshTimeline();
    }

    if (m_pendingTimelineThumbnailGenerationRequest.has_value())
    {
        const auto nextRequest = *m_pendingTimelineThumbnailGenerationRequest;
        m_pendingTimelineThumbnailGenerationRequest.reset();
        startProjectTimelineThumbnailsGeneration(nextRequest);
    }
}

void MainWindow::refreshNodeEditor()
{
    if (!m_nodeEditorQuickController)
    {
        return;
    }

    m_nodeEditorState.reset();
    QVariantList nodeTrackItems;
    const auto selectedTrackId = m_controller->selectedTrackId();
    const auto nodeDocumentPath = m_controller->trackNodeDocumentPath(selectedTrackId);
    const auto selectedNodeLabel = selectedNodeDisplayLabel(m_controller);
    const auto runtimeNodeState = m_controller->selectedAudioClipPreviewState();
    auto nodeTimelineState = runtimeNodeState.value_or(AudioClipPreviewState{});
    nodeTimelineState.trackId = selectedTrackId;
    nodeTimelineState.label = selectedNodeLabel;
    nodeTimelineState.loopEnabled = false;
    if (const auto selectedFrameRange = m_controller->selectedTrackFrameRange(); selectedFrameRange.has_value())
    {
        nodeTimelineState.nodeStartFrame = selectedFrameRange->first;
        nodeTimelineState.nodeEndFrame = selectedFrameRange->second;
    }
    const auto nodeDurationMs = nodeTimelineState.nodeEndFrame >= nodeTimelineState.nodeStartFrame
        ? std::max(
            1,
            static_cast<int>(std::lround(
                (static_cast<double>(nodeTimelineState.nodeEndFrame - nodeTimelineState.nodeStartFrame + 1) * 1000.0)
                / std::max(0.0001, m_controller ? m_controller->fps() : 0.0))))
        : 1;
    auto hasUnsavedNodeChanges =
        !selectedTrackId.isNull()
        && (nodeDocumentPath.isEmpty() || m_nodeTracksWithUnsavedChanges.contains(selectedTrackId));
    if (!nodeDocumentPath.isEmpty() && QFileInfo::exists(nodeDocumentPath))
    {
        QString errorMessage;
        const auto nodeDocument = dawg::node::loadDocument(nodeDocumentPath, &errorMessage);
        if (nodeDocument.has_value())
        {
            const auto savedNodeLabel = nodeDocument->node.label.trimmed().isEmpty()
                ? nodeDocument->name.trimmed()
                : nodeDocument->node.label.trimmed();
            const auto labelDiffers = savedNodeLabel != selectedNodeLabel.trimmed();
            if (!labelDiffers)
            {
                m_nodeTracksWithUnsavedChanges.remove(selectedTrackId);
            }
            hasUnsavedNodeChanges =
                m_nodeTracksWithUnsavedChanges.contains(selectedTrackId)
                || labelDiffers;
            nodeTrackItems = nodeTrackItemsFromDocument(
                *nodeDocument,
                nodeDurationMs,
                [this](const QString& filePath) -> std::optional<int>
                {
                    return m_controller ? m_controller->audioFileChannelCount(filePath) : std::nullopt;
                });
            for (const auto& lane : nodeDocument->node.lanes)
            {
                for (const auto& clip : lane.audioClips)
                {
                    if (const auto previewState = clipStateFromNodeAudioClip(clip, nodeDocument->node.label);
                        previewState.has_value())
                    {
                        m_nodeEditorState = previewState;
                        m_nodeEditorState->trackId = nodeTimelineState.trackId;
                        m_nodeEditorState->nodeStartFrame = nodeTimelineState.nodeStartFrame;
                        m_nodeEditorState->nodeEndFrame = nodeTimelineState.nodeEndFrame;
                        break;
                    }
                }
                if (m_nodeEditorState.has_value())
                {
                    break;
                }
            }
        }
    }

    if (!m_nodeEditorState.has_value())
    {
        m_nodeEditorState = nodeTimelineState;
    }
    if (m_nodeEditorState.has_value())
    {
        m_nodeEditorState->loopEnabled = false;
    }
    if (nodeTrackItems.isEmpty() && m_nodeEditorState.has_value())
    {
        const auto title = m_nodeEditorState->label.trimmed().isEmpty()
            ? QStringLiteral("Track 1")
            : m_nodeEditorState->label.trimmed();
        const auto subtitle = m_nodeEditorState->hasAttachedAudio
            ? QFileInfo(m_nodeEditorState->assetPath).fileName()
            : QStringLiteral("No audio");
        QVariantMap waveformState;
        if (m_nodeEditorState->hasAttachedAudio && !m_nodeEditorState->assetPath.isEmpty())
        {
            waveformState = QVariantMap{
                {QStringLiteral("label"), title},
                {QStringLiteral("assetPath"), m_nodeEditorState->assetPath},
                {QStringLiteral("clipStartMs"), m_nodeEditorState->clipStartMs},
                {QStringLiteral("clipEndMs"), m_nodeEditorState->clipEndMs},
                {QStringLiteral("sourceDurationMs"), m_nodeEditorState->sourceDurationMs},
                {QStringLiteral("playheadMs"), m_nodeEditorState->playheadMs.value_or(m_nodeEditorState->clipStartMs)},
                {QStringLiteral("gainDb"), m_nodeEditorState->gainDb},
                {QStringLiteral("hasAttachedAudio"), true},
                {QStringLiteral("loopEnabled"), false}
            };
        }
        nodeTrackItems.push_back(QVariantMap{
            {QStringLiteral("laneId"), QStringLiteral("runtime")},
            {QStringLiteral("title"), title},
            {QStringLiteral("subtitle"), subtitle},
            {QStringLiteral("primary"), true},
            {QStringLiteral("muted"), false},
            {QStringLiteral("soloed"), false},
            {QStringLiteral("hasWaveform"), !waveformState.isEmpty()},
            {QStringLiteral("waveformState"), waveformState}
        });
    }

    m_nodeEditorQuickController->setState(
        hasOpenProject() && m_controller->hasVideoLoaded(),
        selectedNodeLabel,
        nodeDocumentPath,
        hasUnsavedNodeChanges,
        m_controller ? m_controller->fps() : 0.0,
        m_nodeEditorState,
        nodeTrackItems);
    syncNodeWaveformItem();
    syncNodeEditorActionAvailability();
}

void MainWindow::syncNodeEditorActionAvailability()
{
    if (!m_showNodeEditorAction || !m_controller)
    {
        return;
    }

    const auto enabled = m_controller->hasSelection();
    const auto previousEnabled = m_showNodeEditorAction->isEnabled();
    if (previousEnabled != enabled)
    {
        m_showNodeEditorAction->setEnabled(enabled);
    }

    if (!enabled)
    {
        if (m_showNodeEditorAction->isChecked())
        {
            setActionCheckedSilently(m_showNodeEditorAction, false);
            updateNodeEditorVisibility(false);
        }
    }

    if (previousEnabled != enabled && m_actionRegistry)
    {
        m_actionRegistry->rebuild();
    }
}

void MainWindow::syncNodeWaveformItem()
{
    if (!m_shellRootItem)
    {
        return;
    }

    if (!m_nodeEditorWaveformItem)
    {
        m_nodeEditorWaveformItem = m_shellRootItem->findChild<ClipWaveformQuickItem*>(QStringLiteral("nodeEditorWaveform"));
        if (m_nodeEditorWaveformItem)
        {
            connect(m_nodeEditorWaveformItem, &ClipWaveformQuickItem::clipRangeChanged, m_controller, &PlayerController::setSelectedTrackClipRangeMs);
            connect(m_nodeEditorWaveformItem, &ClipWaveformQuickItem::playheadChanged, this, [this](const int playheadMs)
            {
                if (m_controller->setSelectedTrackClipPlayheadMs(playheadMs))
                {
                    if (!m_projectStateChangeInProgress && hasOpenProject())
                    {
                        setProjectDirty(true);
                    }
                    refreshNodeEditor();
                }
            });
        }
    }

    if (m_nodeEditorWaveformItem)
    {
        m_nodeEditorWaveformItem->setState(m_nodeEditorState);
    }
}

QString MainWindow::projectNodesDirectoryPath() const
{
    return hasOpenProject()
        ? QDir(m_currentProjectRootPath).filePath(QStringLiteral("nodes"))
        : QString{};
}

bool MainWindow::saveSelectedNodeToFile(
    const QString& nodeFilePath,
    const bool bindToSelectedTrack,
    const QString& nodeLabelOverride)
{
    if (!m_controller || !m_controller->hasSelection())
    {
        showStatus(QStringLiteral("Select a node before saving it."));
        return false;
    }

    const auto selectedTrackId = m_controller->selectedTrackId();
    if (selectedTrackId.isNull())
    {
        showStatus(QStringLiteral("Select a node before saving it."));
        return false;
    }

    const auto currentState = m_controller->selectedAudioClipPreviewState();
    const auto trackLabel = m_controller->trackLabel(selectedTrackId).trimmed();
    const auto nodeLabel = !nodeLabelOverride.trimmed().isEmpty()
        ? nodeLabelOverride.trimmed()
        : (trackLabel.isEmpty() ? QStringLiteral("Node") : trackLabel);
    dawg::node::Document nodeDocument;
    const auto boundNodeDocumentPath = m_controller->trackNodeDocumentPath(selectedTrackId);
    auto targetNodeFilePath = QDir::cleanPath(nodeFilePath);
    QString obsoleteNodeFilePath;
    if (!boundNodeDocumentPath.isEmpty() && QFileInfo::exists(boundNodeDocumentPath))
    {
        QString loadError;
        const auto loadedDocument = dawg::node::loadDocument(boundNodeDocumentPath, &loadError);
        if (!loadedDocument.has_value())
        {
            m_dialogController->execMessage(
                QStringLiteral("Save Node"),
                loadError,
                {},
                {DialogController::Button::Ok});
            return false;
        }
        nodeDocument = *loadedDocument;
    }

    if (bindToSelectedTrack && !boundNodeDocumentPath.trimmed().isEmpty())
    {
        const auto cleanedBoundPath = QDir::cleanPath(boundNodeDocumentPath);
        if (QString::compare(targetNodeFilePath, cleanedBoundPath, pathCaseSensitivity()) == 0)
        {
            const QFileInfo boundInfo(cleanedBoundPath);
            const auto renamedNodeFilePath = QDir(boundInfo.absolutePath()).filePath(
                dawg::node::nodeFileNameForName(nodeLabel));
            const auto cleanedRenamedNodeFilePath = QDir::cleanPath(renamedNodeFilePath);
            if (QString::compare(cleanedRenamedNodeFilePath, cleanedBoundPath, pathCaseSensitivity()) != 0)
            {
                if (QFileInfo::exists(cleanedRenamedNodeFilePath))
                {
                    m_dialogController->execMessage(
                        QStringLiteral("Save Node"),
                        QStringLiteral(
                            "A node file named \"%1\" already exists.\nUse Save Node As... to choose a different file.")
                            .arg(QFileInfo(cleanedRenamedNodeFilePath).fileName()),
                        {},
                        {DialogController::Button::Ok});
                    return false;
                }
                targetNodeFilePath = cleanedRenamedNodeFilePath;
                obsoleteNodeFilePath = cleanedBoundPath;
            }
        }
    }

    nodeDocument.name = nodeLabel;
    nodeDocument.node.label = nodeLabel;
    nodeDocument.node.autoPanEnabled = m_controller->selectedTracksAutoPanEnabled();
    nodeDocument.node.timelineFrameCount = m_controller->totalFrames();
    nodeDocument.node.timelineFps = m_controller->fps();

    if (nodeDocument.node.lanes.empty())
    {
        nodeDocument.node.lanes.push_back(dawg::node::LaneData{.label = QStringLiteral("Lane 1")});
    }

    if (currentState.has_value())
    {
        auto runtimeClip = nodeAudioClipFromClipState(*currentState, nodeLabel);
        auto* targetLane = &nodeDocument.node.lanes.front();
        dawg::node::AudioClipData* targetClip = nullptr;
        for (auto& lane : nodeDocument.node.lanes)
        {
            const auto targetClipIt = std::find_if(
                lane.audioClips.begin(),
                lane.audioClips.end(),
                [](const dawg::node::AudioClipData& clip)
                {
                    return clip.attachedAudio.has_value();
                });
            if (targetClipIt != lane.audioClips.end())
            {
                targetLane = &lane;
                targetClip = &(*targetClipIt);
                break;
            }
        }
        if (targetClip == nullptr)
        {
            targetLane->audioClips.push_back(runtimeClip);
        }
        else
        {
            *targetClip = runtimeClip;
        }
    }

    QString errorMessage;
    const auto saved = dawg::node::saveDocument(
        targetNodeFilePath,
        nodeDocument,
        &errorMessage);
    if (!saved)
    {
        m_dialogController->execMessage(
            QStringLiteral("Save Node"),
            errorMessage,
            {},
            {DialogController::Button::Ok});
        return false;
    }

    if (bindToSelectedTrack)
    {
        if (m_controller->trackLabel(selectedTrackId).trimmed() != nodeLabel)
        {
            static_cast<void>(m_controller->renameTrack(selectedTrackId, nodeLabel));
        }
        if (!obsoleteNodeFilePath.isEmpty()
            && QString::compare(obsoleteNodeFilePath, targetNodeFilePath, pathCaseSensitivity()) != 0
            && QFileInfo::exists(obsoleteNodeFilePath))
        {
            QFile::remove(obsoleteNodeFilePath);
        }
        static_cast<void>(m_controller->setTrackNodeDocument(
            selectedTrackId,
            targetNodeFilePath,
            nodeDocument.node.timelineFrameCount,
            nodeDocument.node.timelineFps));
        m_nodeTracksWithUnsavedChanges.remove(selectedTrackId);
        refreshNodeEditor();
    }
    showStatus(QStringLiteral("Saved node to %1.").arg(QFileInfo(targetNodeFilePath).fileName()));
    return true;
}

bool MainWindow::openNodeFileAsNewNode(const QString& nodeFilePath)
{
    if (!m_controller || !m_controller->hasVideoLoaded())
    {
        showStatus(QStringLiteral("Open a video before opening a saved node."));
        return false;
    }

    QString errorMessage;
    const auto document = dawg::node::loadDocument(nodeFilePath, &errorMessage);
    if (!document.has_value())
    {
        m_dialogController->execMessage(
            QStringLiteral("Open Node"),
            errorMessage,
            {},
            {DialogController::Button::Ok});
        return false;
    }

    const auto frameSize = m_controller->videoFrameSize();
    const auto imageCenter = QPointF{
        std::max(1, frameSize.width()) * 0.5,
        std::max(1, frameSize.height()) * 0.5
    };
    const auto nodesDirectoryPath = projectNodesDirectoryPath();
    if (nodesDirectoryPath.isEmpty() || !QDir().mkpath(nodesDirectoryPath))
    {
        showStatus(QStringLiteral("Failed to create the project nodes folder."));
        return false;
    }

    dawg::node::Document materializedDocument = *document;
    const auto preferredNodeName = !materializedDocument.node.label.trimmed().isEmpty()
        ? materializedDocument.node.label.trimmed()
        : QFileInfo(nodeFilePath).completeBaseName();
    const auto targetNodePath = uniqueTargetFilePath(
        nodesDirectoryPath,
        dawg::node::nodeFileNameForName(preferredNodeName.isEmpty() ? QStringLiteral("Node") : preferredNodeName));
    const auto audioDirectoryPath = QDir(m_currentProjectRootPath).filePath(QStringLiteral("audio"));
    for (auto& lane : materializedDocument.node.lanes)
    {
        for (auto& clip : lane.audioClips)
        {
            if (!materializeNodeClipAudio(clip, audioDirectoryPath, &errorMessage))
            {
                m_dialogController->execMessage(
                    QStringLiteral("Open Node"),
                    errorMessage,
                    {},
                    {DialogController::Button::Ok});
                return false;
            }
        }
    }
    if (!dawg::node::saveDocument(targetNodePath, materializedDocument, &errorMessage))
    {
        m_dialogController->execMessage(
            QStringLiteral("Open Node"),
            errorMessage,
            {},
            {DialogController::Button::Ok});
        return false;
    }

    const dawg::node::AudioClipData* primaryClip = nullptr;
    for (const auto& lane : materializedDocument.node.lanes)
    {
        const auto clipIt = std::find_if(
            lane.audioClips.cbegin(),
            lane.audioClips.cend(),
            [](const dawg::node::AudioClipData& clip)
            {
                return clip.attachedAudio.has_value() && !clip.attachedAudio->assetPath.isEmpty();
            });
        if (clipIt != lane.audioClips.cend())
        {
            primaryClip = &(*clipIt);
            break;
        }
    }

    if (primaryClip != nullptr
        && primaryClip->attachedAudio.has_value()
        && !primaryClip->attachedAudio->assetPath.isEmpty())
    {
        if (!m_controller->createTrackWithAudioAtCurrentFrame(primaryClip->attachedAudio->assetPath, imageCenter))
        {
            return false;
        }
        const auto importedState = m_controller->selectedAudioClipPreviewState();
        static_cast<void>(m_controller->setSelectedTrackClipRangeMs(
            primaryClip->attachedAudio->clipStartMs,
            primaryClip->attachedAudio->clipEndMs.value_or(
                importedState.has_value() ? importedState->clipEndMs : primaryClip->attachedAudio->clipStartMs)));
        static_cast<void>(m_controller->setSelectedTrackAudioGainDb(primaryClip->attachedAudio->gainDb));
    }
    else
    {
        m_controller->seedTrack(imageCenter);
    }

    const auto selectedTrackId = m_controller->selectedTrackId();
    const auto preferredLabel = !materializedDocument.node.label.trimmed().isEmpty()
        ? materializedDocument.node.label.trimmed()
        : (primaryClip != nullptr ? primaryClip->label.trimmed() : QString{});
    if (!selectedTrackId.isNull() && !preferredLabel.isEmpty())
    {
        m_controller->renameTrack(selectedTrackId, preferredLabel);
    }
    if (!selectedTrackId.isNull())
    {
        static_cast<void>(m_controller->setTrackNodeDocument(
            selectedTrackId,
            targetNodePath,
            materializedDocument.node.timelineFrameCount > 0 ? materializedDocument.node.timelineFrameCount : m_controller->totalFrames(),
            materializedDocument.node.timelineFps > 0.0 ? materializedDocument.node.timelineFps : m_controller->fps()));
        m_nodeTracksWithUnsavedChanges.remove(selectedTrackId);
    }

    if (m_controller->selectedTracksAutoPanEnabled() != materializedDocument.node.autoPanEnabled)
    {
        m_controller->toggleSelectedTrackAutoPan();
    }

    if (!m_projectStateChangeInProgress && hasOpenProject())
    {
        setProjectDirty(true);
    }
    refreshNodeEditor();
    showStatus(
        primaryClip != nullptr && materializedDocument.node.lanes.size() > 1
            ? QStringLiteral("Opened node %1 using the first lane audio clip for now.")
                .arg(QFileInfo(nodeFilePath).completeBaseName())
            : QStringLiteral("Opened node %1.").arg(QFileInfo(nodeFilePath).completeBaseName()));
    return true;
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

void MainWindow::resetNodeEditorPlayheadToStart()
{
    if (!m_nodeEditorQuickController)
    {
        return;
    }

    {
        const QScopedValueRollback playbackUpdateGuard{m_nodeEditorPreviewUpdatingPlayhead, true};
        m_nodeEditorQuickController->setInsertionMarkerMs(0);
        m_nodeEditorQuickController->setPlayheadMs(0);
    }

    m_nodeEditorPreviewAnchorMs = 0;
    m_nodeEditorPreviewStartMs = 0;
    if (m_nodeEditorPreviewActive)
    {
        if (m_transportUiSyncController)
        {
            m_transportUiSyncController->syncProjectPlayheadToNodeEditor(0);
        }
        if (m_controller)
        {
            static_cast<void>(m_controller->syncNodeEditorPreview(
                m_nodeEditorPreviewClips,
                m_nodeEditorPreviewNodeDurationMs,
                0,
                true));
        }
        updateMixMeterLevels();
    }
    else
    {
        if (m_transportUiSyncController)
        {
            m_transportUiSyncController->syncProjectPlayheadToNodeEditor(0);
        }
    }
}

bool MainWindow::deleteSelectedNodeEditorSelection()
{
    if (!m_controller || !m_nodeEditorQuickController || !m_controller->hasSelection())
    {
        return false;
    }

    const auto selectedLaneId = m_nodeEditorQuickController->selectedLaneId();
    const auto selectedLaneHeaderId = m_nodeEditorQuickController->selectedLaneHeaderId();
    const auto selectedClipId = m_nodeEditorQuickController->selectedClipId();
    if (selectedClipId.isEmpty() && selectedLaneHeaderId.isEmpty())
    {
        showStatus(QStringLiteral("Select a lane name or double-click an audio clip before deleting."));
        return false;
    }

    const auto selectedTrackId = m_controller->selectedTrackId();
    const auto nodeDocumentPath = selectedTrackId.isNull()
        ? QString{}
        : m_controller->trackNodeDocumentPath(selectedTrackId).trimmed();
    if (nodeDocumentPath.isEmpty() || !QFileInfo::exists(nodeDocumentPath))
    {
        showStatus(QStringLiteral("Save or import audio into this node before deleting clips."));
        return false;
    }

    QString errorMessage;
    auto nodeDocument = dawg::node::loadDocument(nodeDocumentPath, &errorMessage);
    if (!nodeDocument.has_value())
    {
        showStatus(errorMessage.isEmpty() ? QStringLiteral("Failed to load node clip.") : errorMessage);
        return false;
    }

    auto laneIt = std::find_if(
        nodeDocument->node.lanes.begin(),
        nodeDocument->node.lanes.end(),
        [&selectedLaneId, &selectedLaneHeaderId, &selectedClipId](const dawg::node::LaneData& lane)
        {
            return lane.id == (selectedClipId.isEmpty() ? selectedLaneHeaderId : selectedLaneId);
        });
    if (laneIt == nodeDocument->node.lanes.end())
    {
        showStatus(QStringLiteral("Selected node track no longer exists."));
        return false;
    }

    QString selectionLaneId = laneIt->id;
    QString nextSelectedLaneId = selectionLaneId;
    QString statusText;
    if (!selectedClipId.isEmpty())
    {
        const auto originalClipCount = laneIt->audioClips.size();
        laneIt->audioClips.erase(
            std::remove_if(
                laneIt->audioClips.begin(),
                laneIt->audioClips.end(),
                [&selectedClipId](const dawg::node::AudioClipData& clip)
                {
                    return clip.id == selectedClipId;
                }),
            laneIt->audioClips.end());
        if (laneIt->audioClips.size() == originalClipCount)
        {
            showStatus(QStringLiteral("Selected audio clip no longer exists."));
            return false;
        }
        statusText = QStringLiteral("Deleted audio clip.");
    }
    else
    {
        const auto clipCount = static_cast<int>(laneIt->audioClips.size());
        if (clipCount > 0)
        {
            if (!m_dialogController)
            {
                showStatus(QStringLiteral("This lane contains audio clips."));
                return false;
            }

            const auto choice = m_dialogController->execMessage(
                QStringLiteral("Delete Lane"),
                QStringLiteral("This lane contains %1 audio clip(s).").arg(clipCount),
                QStringLiteral("Deleting the lane will also remove every audio clip in it."),
                {DialogController::Button::Yes, DialogController::Button::Cancel},
                DialogController::Button::Cancel);
            if (choice != DialogController::Button::Yes)
            {
                showStatus(QStringLiteral("Lane delete canceled."));
                return false;
            }
        }

        const auto laneIndex = static_cast<std::size_t>(std::distance(nodeDocument->node.lanes.begin(), laneIt));
        laneIt = nodeDocument->node.lanes.erase(laneIt);
        if (!nodeDocument->node.lanes.empty())
        {
            const auto nextLaneIndex = std::min(laneIndex, nodeDocument->node.lanes.size() - 1);
            nextSelectedLaneId = nodeDocument->node.lanes[nextLaneIndex].id;
        }
        else
        {
            nextSelectedLaneId.clear();
        }
        statusText = QStringLiteral("Deleted lane.");
    }

    if (!dawg::node::saveDocument(nodeDocumentPath, *nodeDocument, &errorMessage))
    {
        showStatus(errorMessage.isEmpty() ? QStringLiteral("Failed to delete node editor selection.") : errorMessage);
        return false;
    }

    static_cast<void>(m_controller->setTrackNodeDocument(
        selectedTrackId,
        QDir::cleanPath(nodeDocumentPath),
        nodeDocument->node.timelineFrameCount,
        nodeDocument->node.timelineFps));
    m_nodeEditorQuickController->selectLane(nextSelectedLaneId);
    if (!m_projectStateChangeInProgress && hasOpenProject())
    {
        setProjectDirty(true);
    }
    if (m_nodeEditorPreviewActive)
    {
        const auto hasPreviewableAudio = std::any_of(
            nodeDocument->node.lanes.cbegin(),
            nodeDocument->node.lanes.cend(),
            [](const dawg::node::LaneData& lane)
            {
                return std::any_of(
                    lane.audioClips.cbegin(),
                    lane.audioClips.cend(),
                    [](const dawg::node::AudioClipData& clip)
                    {
                        return clip.attachedAudio.has_value() && !clip.attachedAudio->assetPath.isEmpty();
                    });
            });
        stopNodeEditorPreview(false);
        if (hasPreviewableAudio)
        {
            static_cast<void>(startNodeEditorPreview());
        }
    }
    refreshNodeEditor();
    showStatus(statusText);
    return true;
}

void MainWindow::deleteFromFocusedPanel()
{
    if (nodeEditorHasFocus())
    {
        static_cast<void>(deleteSelectedNodeEditorSelection());
        return;
    }

    if (videoPanelHasFocus() && m_controller)
    {
        m_controller->deleteSelectedTrack();
    }
}

void MainWindow::setNodeEditorLaneMuted(const QString& laneId, const bool muted)
{
    if (!m_controller || laneId.isEmpty())
    {
        return;
    }

    const auto selectedTrackId = m_controller->selectedTrackId();
    const auto nodeDocumentPath = selectedTrackId.isNull()
        ? QString{}
        : m_controller->trackNodeDocumentPath(selectedTrackId).trimmed();
    if (nodeDocumentPath.isEmpty() || !QFileInfo::exists(nodeDocumentPath))
    {
        showStatus(QStringLiteral("Save or import audio into this node before muting lanes."));
        return;
    }

    QString errorMessage;
    auto nodeDocument = dawg::node::loadDocument(nodeDocumentPath, &errorMessage);
    if (!nodeDocument.has_value())
    {
        showStatus(errorMessage.isEmpty() ? QStringLiteral("Failed to load node lane.") : errorMessage);
        return;
    }

    auto laneIt = std::find_if(
        nodeDocument->node.lanes.begin(),
        nodeDocument->node.lanes.end(),
        [&laneId](const dawg::node::LaneData& lane)
        {
            return lane.id == laneId;
        });
    if (laneIt == nodeDocument->node.lanes.end())
    {
        showStatus(QStringLiteral("Selected node lane no longer exists."));
        return;
    }
    if (laneIt->muted == muted)
    {
        return;
    }

    laneIt->muted = muted;
    if (!dawg::node::saveDocument(nodeDocumentPath, *nodeDocument, &errorMessage))
    {
        showStatus(errorMessage.isEmpty() ? QStringLiteral("Failed to update node lane mute.") : errorMessage);
        return;
    }

    if (!m_projectStateChangeInProgress && hasOpenProject())
    {
        setProjectDirty(true);
    }
    if (m_nodeEditorPreviewActive)
    {
        const auto playheadMs = m_nodeEditorQuickController ? m_nodeEditorQuickController->playheadMs() : 0;
        m_nodeEditorPreviewClips = nodePreviewClipsFromDocument(
            *nodeDocument,
            [this](const QString& filePath) -> std::optional<int>
            {
                return m_controller ? m_controller->audioFileChannelCount(filePath) : std::nullopt;
            });
        m_nodeEditorPreviewActiveAudioSignature.clear();
        m_lastNodeEditorPreviewAudioSyncMs = -1;
        if (m_controller && !m_nodeEditorPreviewClips.empty())
        {
            static_cast<void>(m_controller->syncNodeEditorPreview(
                m_nodeEditorPreviewClips,
                m_nodeEditorPreviewNodeDurationMs,
                playheadMs));
        }
        else if (m_controller)
        {
            m_controller->stopNodeEditorPreview();
        }
        refreshMixView();
    }
    refreshNodeEditor();
    showStatus(muted ? QStringLiteral("Node lane muted.") : QStringLiteral("Node lane unmuted."));
}

void MainWindow::setNodeEditorLaneSoloed(const QString& laneId, const bool soloed)
{
    if (!m_controller || laneId.isEmpty())
    {
        return;
    }

    const auto selectedTrackId = m_controller->selectedTrackId();
    const auto nodeDocumentPath = selectedTrackId.isNull()
        ? QString{}
        : m_controller->trackNodeDocumentPath(selectedTrackId).trimmed();
    if (nodeDocumentPath.isEmpty() || !QFileInfo::exists(nodeDocumentPath))
    {
        showStatus(QStringLiteral("Save or import audio into this node before soloing lanes."));
        return;
    }

    QString errorMessage;
    auto nodeDocument = dawg::node::loadDocument(nodeDocumentPath, &errorMessage);
    if (!nodeDocument.has_value())
    {
        showStatus(errorMessage.isEmpty() ? QStringLiteral("Failed to load node lane.") : errorMessage);
        return;
    }

    auto laneIt = std::find_if(
        nodeDocument->node.lanes.begin(),
        nodeDocument->node.lanes.end(),
        [&laneId](const dawg::node::LaneData& lane)
        {
            return lane.id == laneId;
        });
    if (laneIt == nodeDocument->node.lanes.end())
    {
        showStatus(QStringLiteral("Selected node lane no longer exists."));
        return;
    }

    bool changed = false;
    if (m_controller->isMixSoloXorMode() && soloed)
    {
        for (auto& lane : nodeDocument->node.lanes)
        {
            const auto nextSoloed = lane.id == laneId;
            if (lane.soloed != nextSoloed)
            {
                lane.soloed = nextSoloed;
                changed = true;
            }
        }
    }
    else if (laneIt->soloed != soloed)
    {
        laneIt->soloed = soloed;
        changed = true;
    }
    if (!changed)
    {
        return;
    }

    if (!dawg::node::saveDocument(nodeDocumentPath, *nodeDocument, &errorMessage))
    {
        showStatus(errorMessage.isEmpty() ? QStringLiteral("Failed to update node lane solo.") : errorMessage);
        return;
    }

    if (!m_projectStateChangeInProgress && hasOpenProject())
    {
        setProjectDirty(true);
    }
    if (m_nodeEditorPreviewActive)
    {
        const auto playheadMs = m_nodeEditorQuickController ? m_nodeEditorQuickController->playheadMs() : 0;
        m_nodeEditorPreviewClips = nodePreviewClipsFromDocument(
            *nodeDocument,
            [this](const QString& filePath) -> std::optional<int>
            {
                return m_controller ? m_controller->audioFileChannelCount(filePath) : std::nullopt;
            });
        m_nodeEditorPreviewActiveAudioSignature.clear();
        m_lastNodeEditorPreviewAudioSyncMs = -1;
        if (m_controller && !m_nodeEditorPreviewClips.empty())
        {
            static_cast<void>(m_controller->syncNodeEditorPreview(
                m_nodeEditorPreviewClips,
                m_nodeEditorPreviewNodeDurationMs,
                playheadMs));
        }
        else if (m_controller)
        {
            m_controller->stopNodeEditorPreview();
        }
        refreshMixView();
    }
    refreshNodeEditor();
    showStatus(soloed ? QStringLiteral("Node lane soloed.") : QStringLiteral("Node lane solo cleared."));
}

bool MainWindow::startNodeEditorPreview()
{
    if (!m_controller || !m_nodeEditorQuickController || !m_controller->hasSelection())
    {
        return false;
    }

    const auto selectedTrackId = m_controller->selectedTrackId();
    const auto nodeDocumentPath = m_controller->trackNodeDocumentPath(selectedTrackId);
    if (nodeDocumentPath.isEmpty() || !QFileInfo::exists(nodeDocumentPath))
    {
        showStatus(QStringLiteral("Save or import audio into this node before previewing it."));
        return false;
    }

    QString errorMessage;
    const auto nodeDocument = dawg::node::loadDocument(nodeDocumentPath, &errorMessage);
    if (!nodeDocument.has_value())
    {
        showStatus(errorMessage.isEmpty() ? QStringLiteral("Failed to load node preview.") : errorMessage);
        return false;
    }

    m_nodeEditorPreviewClips = nodePreviewClipsFromDocument(
        *nodeDocument,
        [this](const QString& filePath) -> std::optional<int>
        {
            return m_controller ? m_controller->audioFileChannelCount(filePath) : std::nullopt;
        });
    if (m_nodeEditorPreviewClips.empty())
    {
        showStatus(QStringLiteral("This node has no audio clips to preview."));
        return false;
    }

    m_nodeEditorPreviewNodeDurationMs = std::max(1, m_nodeEditorQuickController->nodeDurationMs());
    if (m_transportUiSyncController)
    {
        m_transportUiSyncController->resetNodeEditorSync();
    }
    m_nodeEditorPreviewActiveAudioSignature.clear();
    m_lastNodeEditorPreviewAudioSyncMs = -1;
    auto playheadMs = std::clamp(
        m_nodeEditorQuickController->playheadMs(),
        0,
        m_nodeEditorPreviewNodeDurationMs);
    if (playheadMs >= m_nodeEditorPreviewNodeDurationMs)
    {
        playheadMs = 0;
        m_nodeEditorQuickController->setPlayheadMs(playheadMs);
    }

    const auto projectFrame = m_transportUiSyncController
        ? m_transportUiSyncController->nodeEditorProjectFrameForPlayheadMs(playheadMs)
        : std::nullopt;
    if (!projectFrame.has_value())
    {
        showStatus(QStringLiteral("Failed to resolve the node preview timeline position."));
        return false;
    }

    if (!m_controller->startNodeEditorPreview(
            m_nodeEditorPreviewClips,
            m_nodeEditorPreviewNodeDurationMs,
            playheadMs,
            *projectFrame))
    {
        showStatus(QStringLiteral("Failed to start node preview."));
        return false;
    }

    m_nodeEditorPreviewAnchorMs = playheadMs;
    m_nodeEditorPreviewStartMs = playheadMs;
    m_nodeEditorPreviewActiveAudioSignature = nodeEditorPreviewActiveAudioSignature(playheadMs);
    m_lastNodeEditorPreviewAudioSyncMs = playheadMs;
    m_nodeEditorQuickController->setInsertionMarkerMs(playheadMs);
    m_nodeEditorPreviewActive = true;
    m_nodeEditorQuickController->setPlaybackActive(true);
    m_nodeEditorPreviewMixMeterTimer.invalidate();
    updateNodeEditorPreviewMeters();
    m_nodeEditorPreviewMeterTimer.start();
    if (m_transportUiSyncController)
    {
        m_transportUiSyncController->syncThumbnailStripMarkerToNodeEditor(playheadMs);
    }
    showStatus(QStringLiteral("Playing node preview."));
    return true;
}

void MainWindow::stopNodeEditorPreview(const bool restorePlaybackAnchor)
{
    const auto wasPlaying = m_nodeEditorPreviewActive;
    m_nodeEditorPreviewActive = false;
    if (m_nodeEditorPreviewMeterTimer.isActive())
    {
        m_nodeEditorPreviewMeterTimer.stop();
    }
    m_nodeEditorPreviewMixMeterTimer.invalidate();
    if (m_controller)
    {
        m_controller->stopNodeEditorPreview();
    }
    if (m_nodeEditorQuickController)
    {
        m_nodeEditorQuickController->setLaneMeterStates({});
    }
    m_nodeEditorPreviewClips.clear();
    m_nodeEditorPreviewNodeDurationMs = 0;
    if (m_transportUiSyncController)
    {
        m_transportUiSyncController->resetNodeEditorSync();
    }
    m_nodeEditorPreviewActiveAudioSignature.clear();
    m_lastNodeEditorPreviewAudioSyncMs = -1;
    if (m_nodeEditorQuickController)
    {
        if (wasPlaying
            && restorePlaybackAnchor
            && m_controller
            && !m_controller->isInsertionFollowsPlayback())
        {
            const QScopedValueRollback playbackUpdateGuard{m_nodeEditorPreviewUpdatingPlayhead, true};
            m_nodeEditorQuickController->setPlayheadMs(m_nodeEditorPreviewAnchorMs);
        }
        m_nodeEditorQuickController->setPlaybackActive(false);
    }
    m_nodeEditorPreviewAnchorMs = 0;
    m_nodeEditorPreviewStartMs = 0;
    if (wasPlaying)
    {
        refreshMixView();
    }
}

void MainWindow::toggleNodeEditorPreview()
{
    if (m_nodeEditorPreviewActive)
    {
        stopNodeEditorPreview();
        showStatus(QStringLiteral("Stopped node preview."));
        return;
    }

    static_cast<void>(startNodeEditorPreview());
}

void MainWindow::updateNodeEditorPreviewMeters()
{
    constexpr qint64 kNodePreviewMixMeterIntervalMs = 66;

    if (!m_nodeEditorQuickController)
    {
        return;
    }

    QVariantList meterStates;
    if (m_nodeEditorPreviewActive && m_controller)
    {
        const auto laneMeterStates = m_controller->nodePreviewLaneMeterStates();
        meterStates.reserve(static_cast<qsizetype>(laneMeterStates.size()));
        for (const auto& state : laneMeterStates)
        {
            meterStates.push_back(QVariantMap{
                {QStringLiteral("laneId"), state.laneId},
                {QStringLiteral("meterLevel"), state.meterLevel},
                {QStringLiteral("meterLeftLevel"), state.meterLeftLevel},
                {QStringLiteral("meterRightLevel"), state.meterRightLevel},
                {QStringLiteral("useStereoMeter"), state.useStereoMeter}
            });
        }
    }
    m_nodeEditorQuickController->setLaneMeterStates(meterStates);

    if (!m_nodeEditorPreviewActive)
    {
        return;
    }
    if (!m_nodeEditorPreviewMixMeterTimer.isValid()
        || m_nodeEditorPreviewMixMeterTimer.elapsed() >= kNodePreviewMixMeterIntervalMs)
    {
        updateMixMeterLevels();
        m_nodeEditorPreviewMixMeterTimer.restart();
    }
}

QString MainWindow::nodeEditorPreviewActiveAudioSignature(const int playheadMs) const
{
    QStringList activeClipIds;
    activeClipIds.reserve(static_cast<int>(m_nodeEditorPreviewClips.size()));
    const auto clampedPlayheadMs = std::clamp(playheadMs, 0, std::max(0, m_nodeEditorPreviewNodeDurationMs));
    for (const auto& clip : m_nodeEditorPreviewClips)
    {
        if (clip.previewTrackId.isNull()
            || clip.assetPath.isEmpty()
            || clip.clipEndMs <= clip.clipStartMs)
        {
            continue;
        }

        const auto elapsedWithinClipMs = clampedPlayheadMs - std::max(0, clip.laneOffsetMs);
        const auto clipDurationMs = std::max(1, clip.clipEndMs - clip.clipStartMs);
        if (elapsedWithinClipMs < 0 || (!clip.loopEnabled && elapsedWithinClipMs >= clipDurationMs))
        {
            continue;
        }

        activeClipIds.push_back(clip.previewTrackId.toString(QUuid::WithoutBraces));
    }
    activeClipIds.sort();
    return activeClipIds.join(QLatin1Char('|'));
}

bool MainWindow::shouldSyncNodeEditorPreviewAudio(const int playheadMs)
{
    constexpr int kNodePreviewAudioResyncIntervalMs = 5000;
    const auto activeSignature = nodeEditorPreviewActiveAudioSignature(playheadMs);
    const auto activeSetChanged = activeSignature != m_nodeEditorPreviewActiveAudioSignature;
    const auto needsPeriodicResync = m_lastNodeEditorPreviewAudioSyncMs < 0
        || std::abs(playheadMs - m_lastNodeEditorPreviewAudioSyncMs) >= kNodePreviewAudioResyncIntervalMs;
    if (!activeSetChanged && !needsPeriodicResync)
    {
        return false;
    }

    m_nodeEditorPreviewActiveAudioSignature = activeSignature;
    m_lastNodeEditorPreviewAudioSyncMs = playheadMs;
    return true;
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
    setWindowTitle(QStringLiteral("dawg"));
    resize(1400, 900);
    setMinimumSize(QSize(1180, 760));
    setFlags(flags() | Qt::FramelessWindowHint);
    setColor(QColor(QStringLiteral("#0a0c10")));
    setResizeMode(QQuickView::SizeRootObjectToView);
    setIcon(QIcon(QStringLiteral(":/branding/dawg.png")));

    m_actionRegistry = new ActionRegistry(*this, this);
    if (m_mixSoloModeAction)
    {
        const QSignalBlocker blocker(m_mixSoloModeAction);
        m_mixSoloModeAction->setChecked(m_controller->isMixSoloXorMode());
    }
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
    m_showTimelineShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+T")), this);
    m_showMixShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl++")), this);
    m_trimNodeShortcut = new QShortcut(QKeySequence(Qt::SHIFT | Qt::Key_T), this);
    m_autoPanShortcut = new QShortcut(QKeySequence(Qt::Key_R), this);
    m_audioPoolShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+P")), this);
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
    new QShortcut(QKeySequence(QStringLiteral("Ctrl+Q")), this, [this]()
    {
        if (m_quitAction)
        {
            m_quitAction->trigger();
        }
    }, Qt::ApplicationShortcut);
    new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_I), this, [this]()
    {
        if (m_importSoundAction && m_importSoundAction->isEnabled())
        {
            m_importSoundAction->trigger();
        }
    }, Qt::ApplicationShortcut);

    m_shellLayoutController = new ShellLayoutController(this);
    m_videoViewportQuickController = new VideoViewportQuickController(this);
    m_detachedVideoViewportQuickController = new VideoViewportQuickController(this);
    m_detachedVideoViewportQuickController->setNativePresentationEnabled(false);
    m_timelineQuickController = new TimelineQuickController(this);
    m_timelineQuickController->setThumbnailsVisible(false);
    m_thumbnailStripQuickController = new ThumbnailStripQuickController(this);
    ensureQuickTypesRegistered();
    m_nodeEditorQuickController = new NodeEditorQuickController(this);
    m_mixQuickController = new MixQuickController(this);
    m_audioPoolQuickController = new AudioPoolQuickController(*this, this);
    m_audioPoolQuickController->setShowLength(m_audioPoolShowLength);
    m_audioPoolQuickController->setShowSize(m_audioPoolShowSize);
    m_transportUiSyncController = std::make_unique<TransportUiSyncController>(
        *m_controller,
        *m_nodeEditorQuickController,
        *m_timelineQuickController,
        *m_thumbnailStripQuickController,
        [this]()
        {
            return m_timelineQuickWidget && m_timelineQuickWidget->isVisible();
        });
    m_contextMenuController = new ContextMenuController(this);
    m_dialogController = new DialogController(this);
    m_filePickerController = new FilePickerController(this);
    m_shellOverlayController = new ShellOverlayController(this);
    connect(
        m_nodeEditorQuickController,
        &NodeEditorQuickController::fileActionRequested,
        this,
        &MainWindow::handleNodeEditorFileAction);
    connect(
        m_nodeEditorQuickController,
        &NodeEditorQuickController::audioActionRequested,
        this,
        &MainWindow::handleNodeEditorAudioAction);
    connect(
        m_nodeEditorQuickController,
        &NodeEditorQuickController::laneMuteRequested,
        this,
        &MainWindow::setNodeEditorLaneMuted);
    connect(
        m_nodeEditorQuickController,
        &NodeEditorQuickController::laneSoloRequested,
        this,
        &MainWindow::setNodeEditorLaneSoloed);
    connect(
        m_nodeEditorQuickController,
        &NodeEditorQuickController::playheadChanged,
        this,
        [this](const int playheadMs)
        {
            if (m_nodeEditorPreviewUpdatingPlayhead)
            {
                return;
            }

            if (m_nodeEditorPreviewActive)
            {
                if (m_transportUiSyncController)
                {
                    m_transportUiSyncController->syncProjectPlayheadToNodeEditor(playheadMs);
                }
            }
            else
            {
                if (m_transportUiSyncController)
                {
                    m_transportUiSyncController->syncProjectPlayheadToNodeEditor(playheadMs);
                }
            }
            if (!m_nodeEditorPreviewActive
                || m_nodeEditorPreviewNodeDurationMs <= 0)
            {
                return;
            }

            m_nodeEditorPreviewStartMs = std::clamp(playheadMs, 0, m_nodeEditorPreviewNodeDurationMs);
            m_nodeEditorPreviewAnchorMs = m_nodeEditorPreviewStartMs;
            m_nodeEditorQuickController->setInsertionMarkerMs(m_nodeEditorPreviewAnchorMs);
            if (m_controller)
            {
                static_cast<void>(m_controller->syncNodeEditorPreview(
                    m_nodeEditorPreviewClips,
                    m_nodeEditorPreviewNodeDurationMs,
                    m_nodeEditorPreviewStartMs,
                    true));
            }
        });

    m_shellLayoutController->setPreferredSizes(
        m_audioPoolPreferredWidth,
        m_timelinePreferredHeight,
        m_nodeEditorPreferredHeight,
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
    rootContext()->setContextProperty(QStringLiteral("videoViewportAllowNativePresentation"), false);
    rootContext()->setContextProperty(QStringLiteral("timelineController"), m_timelineQuickController);
    rootContext()->setContextProperty(QStringLiteral("thumbnailStripController"), m_thumbnailStripQuickController);
    rootContext()->setContextProperty(QStringLiteral("nodeEditorController"), m_nodeEditorQuickController);
    rootContext()->setContextProperty(QStringLiteral("mainWindowBridge"), this);
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
    m_thumbnailStripQuickWidget =
        m_shellRootItem ? m_shellRootItem->findChild<QQuickItem*>(QStringLiteral("thumbnailStripScene")) : nullptr;
    m_nodeEditorQuickWidget =
        m_shellRootItem ? m_shellRootItem->findChild<QQuickItem*>(QStringLiteral("nodeEditorScene")) : nullptr;
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
        [this](const int audioPoolWidth, const int timelineHeight, const int nodeEditorHeight, const int mixHeight)
        {
            m_audioPoolPreferredWidth = std::max(240, audioPoolWidth);
            m_timelinePreferredHeight = std::max(timelineMinimumHeight(), timelineHeight);
            m_nodeEditorPreferredHeight = std::max(148, nodeEditorHeight);
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
    m_detachedVideoWindow->setFlags(
        Qt::Window
        | Qt::WindowStaysOnTopHint
        | Qt::WindowTitleHint
        | Qt::WindowSystemMenuHint
        | Qt::WindowMinimizeButtonHint
        | Qt::WindowMaximizeButtonHint
        | Qt::WindowCloseButtonHint);
    m_detachedVideoWindow->setResizeMode(QQuickView::SizeRootObjectToView);
    configureQuickEngine(*m_detachedVideoWindow->engine());
    m_detachedVideoWindow->engine()->addImageProvider(
        QStringLiteral("videoViewport"),
        new VideoViewportImageProvider(*m_detachedVideoViewportQuickController));
    m_detachedVideoWindow->rootContext()->setContextProperty(
        QStringLiteral("videoViewportController"),
        m_detachedVideoViewportQuickController);
    m_detachedVideoWindow->rootContext()->setContextProperty(
        QStringLiteral("videoViewportBridge"),
        this);
    m_detachedVideoWindow->rootContext()->setContextProperty(
        QStringLiteral("videoViewportAllowNativePresentation"),
        false);
    m_detachedVideoWindow->setSource(videoViewportSceneUrl());
#ifdef Q_OS_WIN
    applyDarkTitleBar(m_detachedVideoWindow);
#endif
    m_detachedVideoWindow->hide();
    m_detachedVideoWindow->installEventFilter(this);
    auto createDetachedPanelWindow =
        [this](const QString& title, const QColor& color) -> QQuickView*
    {
        auto* window = new QQuickView();
        window->setTitle(title);
        window->setIcon(icon());
        window->setColor(color);
        window->setFlags(
            Qt::Window
            | Qt::WindowTitleHint
            | Qt::WindowSystemMenuHint
            | Qt::WindowMinimizeButtonHint
            | Qt::WindowMaximizeButtonHint
            | Qt::WindowCloseButtonHint);
        window->setResizeMode(QQuickView::SizeRootObjectToView);
        configureQuickEngine(*window->engine());
        return window;
    };
    m_detachedTimelineWindow = createDetachedPanelWindow(
        QStringLiteral("Detached Timeline"),
        QColor(QStringLiteral("#050608")));
    m_detachedTimelineWindow->rootContext()->setContextProperty(QStringLiteral("timelineController"), m_timelineQuickController);
    m_detachedTimelineWindow->rootContext()->setContextProperty(QStringLiteral("videoViewportBridge"), this);
    m_detachedTimelineWindow->setSource(timelineSceneUrl());
#ifdef Q_OS_WIN
    applyDarkTitleBar(m_detachedTimelineWindow);
#endif
    m_detachedTimelineWindow->hide();
    m_detachedTimelineWindow->installEventFilter(this);

    m_detachedMixWindow = createDetachedPanelWindow(
        QStringLiteral("Detached Mixer"),
        QColor(QStringLiteral("#080b10")));
    m_detachedMixWindow->rootContext()->setContextProperty(QStringLiteral("mixController"), m_mixQuickController);
    m_detachedMixWindow->setSource(mixSceneUrl());
#ifdef Q_OS_WIN
    applyDarkTitleBar(m_detachedMixWindow);
#endif
    m_detachedMixWindow->hide();
    m_detachedMixWindow->installEventFilter(this);

    m_detachedAudioPoolWindow = createDetachedPanelWindow(
        QStringLiteral("Detached Audio Pool"),
        QColor(QStringLiteral("#07090c")));
    m_detachedAudioPoolWindow->rootContext()->setContextProperty(
        QStringLiteral("audioPoolController"),
        m_audioPoolQuickController);
    m_detachedAudioPoolWindow->rootContext()->setContextProperty(QStringLiteral("windowChrome"), m_windowChromeController);
    m_detachedAudioPoolWindow->setSource(audioPoolSceneUrl());
#ifdef Q_OS_WIN
    applyDarkTitleBar(m_detachedAudioPoolWindow);
#endif
    m_detachedAudioPoolWindow->hide();
    m_detachedAudioPoolWindow->installEventFilter(this);

    if (m_shellLayoutController)
    {
        m_shellLayoutController->setTimelineMinimumHeight(timelineMinimumHeight());
        m_shellLayoutController->setThumbnailsVisible(true);
        m_shellLayoutController->setTimelineVisible(true);
        m_shellLayoutController->setNodeEditorVisible(false);
        m_shellLayoutController->setMixVisible(false);
        m_shellLayoutController->setAudioPoolVisible(false);
        m_shellLayoutController->setVideoDetached(false);
        m_shellLayoutController->setPreferredSizes(
            m_audioPoolPreferredWidth,
            m_timelinePreferredHeight,
            m_nodeEditorPreferredHeight,
            m_mixPreferredHeight);
    }

    syncShellLayoutViewport();

    connect(m_contextMenuController, &ContextMenuController::changed, this, [this]()
    {
        updateOverlayPositions();
    });
    connect(m_contextMenuController, &ContextMenuController::itemTriggered, this, [this](const QString& key)
    {
        if (key == QStringLiteral("node.openEditor"))
        {
            if (!m_contextMenuTrackId.isNull())
            {
                m_controller->selectTrack(m_contextMenuTrackId);
            }
            if (m_showNodeEditorAction)
            {
                m_showNodeEditorAction->setChecked(true);
            }
            return;
        }
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
                    m_nodeTracksWithUnsavedChanges.insert(m_contextMenuTrackId);
                    refreshNodeEditor();
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

    connect(
        this,
        &QQuickWindow::sceneGraphInitialized,
        this,
        [this]()
        {
#ifdef Q_OS_WIN
            auto* quickDevice = rendererInterface()
                ? static_cast<ID3D11Device*>(rendererInterface()->getResource(this, QSGRendererInterface::DeviceResource))
                : nullptr;
            if (quickDevice)
            {
                enableD3D11MultithreadProtection(quickDevice);
                quickDevice->AddRef();
            }

            QMetaObject::invokeMethod(
                this,
                [this, quickDevice]()
                {
                    bool nativePresentationReady = quickDevice != nullptr;
                    clearStuckWaitCursor(this);
                    m_controller->setPreferredD3D11Device(quickDevice);

                    if (hasOpenProject() && m_controller->hasVideoLoaded() && m_controller->videoHardwareAccelerated())
                    {
                        QString errorMessage;
                        const auto controllerState = m_controller->snapshotProjectState();
                        m_projectStateChangeInProgress = true;
                        const auto restored = m_controller->restoreProjectState(controllerState, &errorMessage);
                        m_projectStateChangeInProgress = false;
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

                    m_nativeVideoPresentationAllowed = nativePresentationReady;
                    updateDetachedPanelUiState();
                    if (quickDevice)
                    {
                        quickDevice->Release();
                    }
                    clearStuckWaitCursor(this);
                    updateMixQuickDiagnostics();
                },
                Qt::QueuedConnection);
#else
            QMetaObject::invokeMethod(
                this,
                [this]()
                {
                    clearStuckWaitCursor(this);
                    m_nativeVideoPresentationAllowed = false;
                    updateDetachedPanelUiState();
                    updateMixQuickDiagnostics();
                },
                Qt::QueuedConnection);
#endif
        },
        Qt::DirectConnection);
    handleTimelineQuickStatusChanged();
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
