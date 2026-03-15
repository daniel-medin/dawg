#include "app/MainWindow.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>

#include <QApplication>
#include <QCoreApplication>
#include <QCursor>
#include <QDir>
#include <QDrag>
#include <QEnterEvent>
#include <QEvent>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLineEdit>
#include <QMenuBar>
#include <QMenu>
#include <QMimeData>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QScrollArea>
#include <QScreen>
#include <QSettings>
#include <QSizePolicy>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSlider>
#include <QStandardPaths>
#include <QResizeEvent>
#include <QStatusBar>
#include <QStyle>
#include <QToolButton>
#include <QToolTip>
#include <QWidgetAction>
#include <QVBoxLayout>
#include <QWidget>
#include <QUrl>

#ifdef Q_OS_WIN
#include <windows.h>
#include <dxgi1_4.h>
#include <psapi.h>
#endif

#include "app/PlayerController.h"
#include "app/ProjectDocument.h"
#include "ui/ClipEditorView.h"
#include "ui/DebugOverlayWindow.h"
#include "ui/MixView.h"
#include "ui/NativeVideoViewport.h"
#include "ui/TimelineView.h"
#include "ui/VideoCanvas.h"

namespace
{
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

class TrackGainPopup final : public QFrame
{
public:
    explicit TrackGainPopup(QWidget* parent = nullptr)
        : QFrame(parent, Qt::Popup | Qt::FramelessWindowHint)
    {
        setObjectName(QStringLiteral("trackGainPopup"));
        setAttribute(Qt::WA_ShowWithoutActivating);
        setFrameShape(QFrame::NoFrame);

        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(10, 10, 10, 10);
        layout->setSpacing(6);

        m_slider = new QSlider(Qt::Vertical, this);
        m_slider->setRange(kMixGainPopupMinValue, kMixGainPopupMaxValue);
        m_slider->setSingleStep(1);
        m_slider->setPageStep(10);
        m_slider->setFixedHeight(132);
        m_slider->setInvertedAppearance(false);
        m_slider->setStyleSheet(QStringLiteral(
            "QSlider::groove:vertical {"
            "  background: #0b1016;"
            "  border: 1px solid #1d2733;"
            "  border-radius: 4px;"
            "  width: 8px;"
            "}"
            "QSlider::sub-page:vertical {"
            "  background: #3d6ea5;"
            "  border-radius: 3px;"
            "}"
            "QSlider::add-page:vertical {"
            "  background: #111821;"
            "  border-radius: 3px;"
            "}"
            "QSlider::handle:vertical {"
            "  background: #d7dee7;"
            "  border: 1px solid #eff3f7;"
            "  border-radius: 6px;"
            "  height: 12px;"
            "  margin: -2px -6px;"
            "}"));
        layout->addWidget(m_slider, 0, Qt::AlignHCenter);

        m_valueLabel = new QLabel(this);
        m_valueLabel->setAlignment(Qt::AlignCenter);
        m_valueLabel->setStyleSheet(QStringLiteral("color: #eef2f6; font-size: 8pt;"));
        layout->addWidget(m_valueLabel);

        setStyleSheet(QStringLiteral(
            "#trackGainPopup {"
            "  background: rgba(12, 16, 22, 0.97);"
            "  border: 1px solid #2a3644;"
            "  border-radius: 8px;"
            "}"));
    }

    [[nodiscard]] QSlider* slider() const
    {
        return m_slider;
    }

    [[nodiscard]] QLabel* valueLabel() const
    {
        return m_valueLabel;
    }

private:
    QSlider* m_slider = nullptr;
    QLabel* m_valueLabel = nullptr;
};

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

bool controllerStateHasSavedMedia(const dawg::project::ControllerState& state)
{
    return !state.videoPath.isEmpty()
        || !state.audioPoolAssetPaths.empty()
        || !state.trackerState.tracks.empty();
}

bool recoverProjectMediaFromFolders(
    dawg::project::ControllerState* state,
    const QString& projectRootPath,
    QString* message)
{
    if (!state || controllerStateHasSavedMedia(*state))
    {
        return false;
    }

    bool recovered = false;
    const auto videoFiles = projectMediaFilesInDirectory(
        QDir(projectRootPath).filePath(QStringLiteral("video")),
        projectVideoExtensions());
    if (videoFiles.size() == 1)
    {
        state->videoPath = videoFiles.front();
        recovered = true;
    }

    const auto audioFiles = projectMediaFilesInDirectory(
        QDir(projectRootPath).filePath(QStringLiteral("audio")),
        projectAudioExtensions());
    if (!audioFiles.isEmpty())
    {
        state->audioPoolAssetPaths.assign(audioFiles.cbegin(), audioFiles.cend());
        recovered = true;
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

QString formatAudioPoolDuration(const int durationMs)
{
    if (durationMs <= 0)
    {
        return QStringLiteral("--");
    }

    const auto totalSeconds = std::max(0, static_cast<int>(std::lround(durationMs / 1000.0)));
    const auto hours = totalSeconds / 3600;
    const auto minutes = (totalSeconds / 60) % 60;
    const auto seconds = totalSeconds % 60;
    if (hours > 0)
    {
        return QStringLiteral("%1:%2:%3")
            .arg(hours)
            .arg(minutes, 2, 10, QLatin1Char('0'))
            .arg(seconds, 2, 10, QLatin1Char('0'));
    }

    return QStringLiteral("%1:%2")
        .arg(totalSeconds / 60)
        .arg(seconds, 2, 10, QLatin1Char('0'));
}

QString formatAudioPoolSize(const std::int64_t fileSizeBytes)
{
    if (fileSizeBytes < 0)
    {
        return QStringLiteral("--");
    }

    constexpr std::array<const char*, 4> units{"B", "KB", "MB", "GB"};
    double size = static_cast<double>(fileSizeBytes);
    std::size_t unitIndex = 0;
    while (size >= 1024.0 && unitIndex + 1 < units.size())
    {
        size /= 1024.0;
        ++unitIndex;
    }

    const auto decimals = unitIndex == 0 ? 0 : 1;
    return QStringLiteral("%1 %2")
        .arg(size, 0, 'f', decimals)
        .arg(QString::fromLatin1(units[unitIndex]));
}

class AudioPoolRow final : public QWidget
{
public:
    explicit AudioPoolRow(
        const QString& assetPath,
        std::function<bool(const QString&)> startPreview,
        std::function<void()> stopPreview,
        std::function<void()> activateItem,
        std::function<void()> doubleActivateItem,
        QWidget* parent = nullptr)
        : QWidget(parent)
        , m_assetPath(assetPath)
        , m_startPreview(std::move(startPreview))
        , m_stopPreview(std::move(stopPreview))
        , m_activateItem(std::move(activateItem))
        , m_doubleActivateItem(std::move(doubleActivateItem))
    {
        setCursor(Qt::OpenHandCursor);
        setMouseTracking(true);
        qApp->installEventFilter(this);
    }

    ~AudioPoolRow() override
    {
        qApp->removeEventFilter(this);
        stopPreviewIfNeeded();
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        Q_UNUSED(watched);

        switch (event->type())
        {
        case QEvent::KeyPress:
        case QEvent::KeyRelease:
            updateCursorState();
            break;
        case QEvent::MouseButtonRelease:
            if (m_previewHeld)
            {
                auto* mouseEvent = static_cast<QMouseEvent*>(event);
                if (mouseEvent->button() == Qt::LeftButton)
                {
                    stopPreviewIfNeeded();
                    updateCursorState();
                }
            }
            break;
        default:
            break;
        }

        return QWidget::eventFilter(watched, event);
    }

    void enterEvent(QEnterEvent* event) override
    {
        m_hovered = true;
        updateCursorState();
        QWidget::enterEvent(event);
    }

    void leaveEvent(QEvent* event) override
    {
        m_hovered = false;
        updateCursorState();
        QWidget::leaveEvent(event);
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton)
        {
            m_dragStartPosition = event->position().toPoint();
            m_dragPerformed = false;

            if (previewModifierActive(event->modifiers()))
            {
                if (m_startPreview)
                {
                    m_previewHeld = m_startPreview(m_assetPath);
                }
                updateCursorState();
                event->accept();
                return;
            }

            setCursor(Qt::ClosedHandCursor);
        }

        QWidget::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (m_previewHeld)
        {
            event->accept();
            return;
        }

        if (!(event->buttons() & Qt::LeftButton))
        {
            updateCursorState();
            QWidget::mouseMoveEvent(event);
            return;
        }

        if (previewModifierActive(event->modifiers()))
        {
            event->accept();
            return;
        }

        if ((event->position().toPoint() - m_dragStartPosition).manhattanLength() < QApplication::startDragDistance())
        {
            QWidget::mouseMoveEvent(event);
            return;
        }

        auto* mimeData = new QMimeData();
        mimeData->setData("application/x-dawg-audio-path", m_assetPath.toUtf8());
        mimeData->setText(m_assetPath);
        mimeData->setUrls({QUrl::fromLocalFile(m_assetPath)});

        auto* drag = new QDrag(this);
        drag->setMimeData(mimeData);
        m_dragPerformed = true;
        drag->exec(Qt::CopyAction, Qt::CopyAction);
        updateCursorState();
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton && m_skipNextReleaseActivation)
        {
            m_skipNextReleaseActivation = false;
            updateCursorState();
            event->accept();
            return;
        }

        if (event->button() == Qt::LeftButton && m_previewHeld)
        {
            stopPreviewIfNeeded();
            updateCursorState();
            event->accept();
            return;
        }

        if (event->button() == Qt::LeftButton
            && !m_dragPerformed
            && !previewModifierActive(event->modifiers())
            && rect().contains(event->position().toPoint()))
        {
            if (m_activateItem)
            {
                m_activateItem();
            }
            updateCursorState();
            event->accept();
            return;
        }

        updateCursorState();
        QWidget::mouseReleaseEvent(event);
    }

    void mouseDoubleClickEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton
            && !previewModifierActive(event->modifiers())
            && rect().contains(event->position().toPoint()))
        {
            m_skipNextReleaseActivation = true;
            if (m_doubleActivateItem)
            {
                m_doubleActivateItem();
            }
            updateCursorState();
            event->accept();
            return;
        }

        QWidget::mouseDoubleClickEvent(event);
    }

private:
    [[nodiscard]] bool previewModifierActive(const Qt::KeyboardModifiers modifiers) const
    {
        return modifiers & Qt::ControlModifier;
    }

    void stopPreviewIfNeeded()
    {
        if (!m_previewHeld)
        {
            return;
        }

        m_previewHeld = false;
        if (m_stopPreview)
        {
            m_stopPreview();
        }
    }

    void updateCursorState()
    {
        if (m_previewHeld || (m_hovered && previewModifierActive(QGuiApplication::keyboardModifiers())))
        {
            setCursor(audioPoolPreviewCursor());
            return;
        }

        if (QApplication::mouseButtons() & Qt::LeftButton)
        {
            setCursor(Qt::ClosedHandCursor);
            return;
        }

        setCursor(Qt::OpenHandCursor);
    }

    QString m_assetPath;
    std::function<bool(const QString&)> m_startPreview;
    std::function<void()> m_stopPreview;
    std::function<void()> m_activateItem;
    std::function<void()> m_doubleActivateItem;
    QPoint m_dragStartPosition;
    bool m_dragPerformed = false;
    bool m_hovered = false;
    bool m_previewHeld = false;
    bool m_skipNextReleaseActivation = false;
};
}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_controller(new PlayerController(this))
{
    buildUi();
    buildMenus();
    rebuildRecentProjectsMenu();
    updateDetachedVideoUiState();
    qApp->installEventFilter(this);
    m_clearAllShortcutTimer.setSingleShot(true);
    m_clearAllShortcutTimer.setInterval(1500);
    m_memoryUsageTimer.setInterval(1000);
    m_mixMeterTimer.setInterval(33);
    m_clipEditorPreviewTimer.setInterval(33);
    connect(&m_clearAllShortcutTimer, &QTimer::timeout, this, &MainWindow::clearPendingClearAllShortcut);
    connect(&m_memoryUsageTimer, &QTimer::timeout, this, &MainWindow::updateMemoryUsage);
    connect(&m_mixMeterTimer, &QTimer::timeout, this, &MainWindow::refreshMixView);
    connect(&m_clipEditorPreviewTimer, &QTimer::timeout, this, &MainWindow::refreshClipEditor);
    m_statusToastTimer.setSingleShot(true);
    m_statusToastTimer.setInterval(2800);
    connect(&m_statusToastTimer, &QTimer::timeout, this, [this]()
    {
        if (m_statusToast)
        {
            m_statusToast->hide();
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
    connect(m_quitAction, &QAction::triggered, this, &QWidget::close);
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
    connect(m_timelineClickSeeksAction, &QAction::toggled, this, [this](const bool enabled)
    {
        m_timeline->setSeekOnClickEnabled(enabled);
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
        m_canvas->setShowAllLabels(enabled);
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
    connect(m_canvas, &VideoCanvas::importVideoRequested, this, &MainWindow::openVideo);
    connect(m_canvas, &VideoCanvas::seedPointRequested, m_controller, &PlayerController::seedTrack);
    connect(m_canvas, &VideoCanvas::audioDropped, this, [this](const QString& assetPath, const QPointF& imagePoint)
    {
        m_controller->createTrackWithAudioAtCurrentFrame(assetPath, imagePoint);
    });
    connect(m_canvas, &VideoCanvas::tracksSelected, m_controller, &PlayerController::selectTracks);
    connect(m_canvas, &VideoCanvas::trackSelected, m_controller, &PlayerController::selectTrack);
    connect(m_canvas, &VideoCanvas::trackActivated, this, [this](const QUuid& trackId)
    {
        m_controller->selectTrack(trackId);
        updateClipEditorVisibility(true);
    });
    connect(m_canvas, &VideoCanvas::trackContextMenuRequested, this, [this](const QUuid& trackId, const QPoint& globalPosition)
    {
        showNodeContextMenu(trackId, globalPosition, true);
    });
    connect(m_canvas, &VideoCanvas::trackGainAdjustRequested, this, &MainWindow::adjustTrackMixGainFromWheel);
    connect(m_canvas, &VideoCanvas::trackGainPopupRequested, this, &MainWindow::showTrackMixGainPopup);
    connect(m_canvas, &VideoCanvas::selectedTrackMoved, m_controller, &PlayerController::moveSelectedTrack);
    connect(m_timeline, &TimelineView::frameRequested, m_controller, &PlayerController::seekToFrame);
    connect(m_timeline, &TimelineView::loopStartFrameRequested, m_controller, &PlayerController::setLoopStartFrame);
    connect(m_timeline, &TimelineView::loopEndFrameRequested, m_controller, &PlayerController::setLoopEndFrame);
    connect(m_timeline, &TimelineView::trackSelected, m_controller, &PlayerController::selectTrack);
    connect(m_timeline, &TimelineView::trackActivated, this, [this](const QUuid& trackId)
    {
        m_controller->selectTrack(trackId);
        updateClipEditorVisibility(true);
    });
    connect(m_timeline, &TimelineView::trackStartFrameRequested, m_controller, &PlayerController::setTrackStartFrame);
    connect(m_timeline, &TimelineView::trackEndFrameRequested, m_controller, &PlayerController::setTrackEndFrame);
    connect(m_timeline, &TimelineView::trackSpanMoveRequested, m_controller, &PlayerController::moveTrackFrameSpan);
    connect(m_timeline, &TimelineView::trackContextMenuRequested, this, [this](const QUuid& trackId, const QPoint& globalPosition)
    {
        showNodeContextMenu(trackId, globalPosition, false);
    });
    connect(m_timeline, &TimelineView::trackGainAdjustRequested, this, &MainWindow::adjustTrackMixGainFromWheel);
    connect(m_timeline, &TimelineView::trackGainPopupRequested, this, &MainWindow::showTrackMixGainPopup);
    connect(m_timeline, &TimelineView::loopContextMenuRequested, this, [this](const QPoint& globalPosition)
    {
        if (!m_controller->loopStartFrame().has_value() && !m_controller->loopEndFrame().has_value())
        {
            return;
        }

        QMenu menu(this);
        auto* deleteAction = menu.addAction(QStringLiteral("Delete"));
        if (menu.exec(globalPosition) == deleteAction)
        {
            clearLoopRange();
        }
    });
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
    connect(m_clipEditorView, &ClipEditorView::clipRangeChanged, m_controller, &PlayerController::setSelectedTrackClipRangeMs);
    connect(m_clipEditorView, &ClipEditorView::playheadChanged, this, [this](const int playheadMs)
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
    connect(m_clipEditorView, &ClipEditorView::gainChanged, this, [this](const float gainDb)
    {
        if (m_controller->setSelectedTrackAudioGainDb(gainDb))
        {
            refreshClipEditor();
        }
    });
    connect(m_clipEditorView, &ClipEditorView::attachAudioRequested, this, &MainWindow::importSound);
    connect(m_clipEditorView, &ClipEditorView::loopSoundChanged, this, [this](const bool enabled)
    {
        if (m_controller->setSelectedTrackLoopEnabled(enabled))
        {
            refreshClipEditor();
        }
    });
    connect(m_mixView, &MixView::masterGainChanged, m_controller, &PlayerController::setMasterMixGainDb);
    connect(m_mixView, &MixView::masterMutedChanged, m_controller, &PlayerController::setMasterMixMuted);
    connect(m_mixView, &MixView::laneGainChanged, m_controller, &PlayerController::setMixLaneGainDb);
    connect(m_mixView, &MixView::laneMutedChanged, m_controller, &PlayerController::setMixLaneMuted);
    connect(m_mixView, &MixView::laneSoloChanged, m_controller, &PlayerController::setMixLaneSoloed);
    connect(m_mixView, &MixView::masterGainChanged, this, [this](float)
    {
        if (!m_projectStateChangeInProgress && hasOpenProject())
        {
            setProjectDirty(true);
        }
    });
    connect(m_mixView, &MixView::masterMutedChanged, this, [this](bool)
    {
        if (!m_projectStateChangeInProgress && hasOpenProject())
        {
            setProjectDirty(true);
        }
    });
    connect(m_mixView, &MixView::laneGainChanged, this, [this](int, float)
    {
        if (!m_projectStateChangeInProgress && hasOpenProject())
        {
            setProjectDirty(true);
        }
    });
    connect(m_mixView, &MixView::laneMutedChanged, this, [this](int, bool)
    {
        if (!m_projectStateChangeInProgress && hasOpenProject())
        {
            setProjectDirty(true);
        }
    });
    connect(m_mixView, &MixView::laneSoloChanged, this, [this](int, bool)
    {
        if (!m_projectStateChangeInProgress && hasOpenProject())
        {
            setProjectDirty(true);
        }
    });

    updatePlaybackState(false);
    updateInsertionFollowsPlaybackState(m_controller->isInsertionFollowsPlayback());
    syncMotionTrackingUi(m_controller->isMotionTrackingEnabled());
    if (m_showAllNodeNamesAction && m_showAllNodeNamesAction->isChecked())
    {
        m_canvas->setShowAllLabels(true);
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

bool MainWindow::openProjectFilePath(const QString& projectFilePath)
{
    if (projectFilePath.isEmpty())
    {
        return false;
    }

    return loadProjectFile(projectFilePath);
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
        if (!m_clipEditorView)
        {
            return false;
        }

        const auto* focused = QApplication::focusWidget();
        return focused && (focused == m_clipEditorView || m_clipEditorView->isAncestorOf(focused));
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

    if (m_canvasTipsOverlay && m_canvasTipsOverlay->isVisible())
    {
        const auto eventType = event->type();
        if (eventType == QEvent::KeyPress
            || eventType == QEvent::MouseButtonPress
            || eventType == QEvent::Wheel)
        {
            if (auto* widget = qobject_cast<QWidget*>(watched))
            {
                if (widget == this || isAncestorOf(widget))
                {
                    hideCanvasTipsOverlay();
                }
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
            && m_trackGainPopup
            && m_trackGainPopup->isVisible())
        {
            m_trackGainPopup->hide();
            m_trackGainPopupTrackId = {};
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
                && m_timeline
                && m_timeline->hasSelectedLoopRange()
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

    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::resizeEvent(QResizeEvent* event)
{
    QMainWindow::resizeEvent(event);
    updateOverlayPositions();
}

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
    QMainWindow::closeEvent(event);
    if (event->isAccepted())
    {
        return;
    }

    m_shuttingDown = false;
}

bool MainWindow::hasOpenProject() const
{
    return !m_currentProjectFilePath.isEmpty();
}

void MainWindow::clearCurrentProject()
{
    m_currentProjectFilePath.clear();
    m_currentProjectRootPath.clear();
    m_currentProjectName.clear();
    m_projectDirty = false;
    if (m_saveProjectAction)
    {
        m_saveProjectAction->setEnabled(false);
    }
    if (m_saveProjectAsAction)
    {
        m_saveProjectAsAction->setEnabled(false);
    }
    updateWindowTitle();
}

void MainWindow::setCurrentProject(const QString& projectFilePath, const QString& projectName)
{
    m_currentProjectFilePath = normalizeProjectFilePath(projectFilePath);
    m_currentProjectRootPath = QFileInfo(m_currentProjectFilePath).absolutePath();
    m_currentProjectName = dawg::project::sanitizeProjectName(projectName);
    m_projectDirty = false;
    if (m_saveProjectAction)
    {
        m_saveProjectAction->setEnabled(false);
    }
    if (m_saveProjectAsAction)
    {
        m_saveProjectAsAction->setEnabled(true);
    }

    QSettings settings;
    settings.setValue(QString::fromLatin1(kLastProjectPathSettingsKey), m_currentProjectFilePath);
    updateWindowTitle();
}

QStringList MainWindow::recentProjectPaths() const
{
    QSettings settings;
    const auto storedPaths = settings.value(QString::fromLatin1(kRecentProjectPathsSettingsKey)).toStringList();
    QStringList normalizedPaths;
    normalizedPaths.reserve(static_cast<qsizetype>(std::min(storedPaths.size(), static_cast<qsizetype>(kMaxRecentProjectCount))));
    for (const auto& storedPath : storedPaths)
    {
        const auto normalizedPath = normalizeProjectFilePath(storedPath);
        if (normalizedPath.isEmpty())
        {
            continue;
        }

        const auto duplicateIt = std::find_if(
            normalizedPaths.cbegin(),
            normalizedPaths.cend(),
            [&normalizedPath](const QString& existingPath)
            {
                return projectPathMatches(existingPath, normalizedPath);
            });
        if (duplicateIt != normalizedPaths.cend())
        {
            continue;
        }

        normalizedPaths.push_back(normalizedPath);
        if (normalizedPaths.size() >= kMaxRecentProjectCount)
        {
            break;
        }
    }
    return normalizedPaths;
}

void MainWindow::storeRecentProjectPaths(const QStringList& projectPaths)
{
    QSettings settings;
    if (projectPaths.isEmpty())
    {
        settings.remove(QString::fromLatin1(kRecentProjectPathsSettingsKey));
        return;
    }

    settings.setValue(QString::fromLatin1(kRecentProjectPathsSettingsKey), projectPaths);
}

void MainWindow::addRecentProjectPath(const QString& projectFilePath)
{
    const auto normalizedPath = normalizeProjectFilePath(projectFilePath);
    if (normalizedPath.isEmpty())
    {
        return;
    }

    auto updatedPaths = recentProjectPaths();
    updatedPaths.erase(
        std::remove_if(
            updatedPaths.begin(),
            updatedPaths.end(),
            [&normalizedPath](const QString& existingPath)
            {
                return projectPathMatches(existingPath, normalizedPath);
            }),
        updatedPaths.end());
    updatedPaths.push_front(normalizedPath);
    while (updatedPaths.size() > kMaxRecentProjectCount)
    {
        updatedPaths.removeLast();
    }

    storeRecentProjectPaths(updatedPaths);
    rebuildRecentProjectsMenu();
}

void MainWindow::removeRecentProjectPath(const QString& projectFilePath)
{
    const auto normalizedPath = normalizeProjectFilePath(projectFilePath);
    if (normalizedPath.isEmpty())
    {
        return;
    }

    auto updatedPaths = recentProjectPaths();
    const auto originalSize = updatedPaths.size();
    updatedPaths.erase(
        std::remove_if(
            updatedPaths.begin(),
            updatedPaths.end(),
            [&normalizedPath](const QString& existingPath)
            {
                return projectPathMatches(existingPath, normalizedPath);
            }),
        updatedPaths.end());
    if (updatedPaths.size() == originalSize)
    {
        return;
    }

    storeRecentProjectPaths(updatedPaths);
    rebuildRecentProjectsMenu();
}

void MainWindow::rebuildRecentProjectsMenu()
{
    if (!m_openRecentMenu)
    {
        return;
    }

    m_openRecentMenu->clear();
    m_openRecentMenu->setToolTipsVisible(true);

    const auto storedPaths = recentProjectPaths();
    QStringList existingPaths;
    existingPaths.reserve(storedPaths.size());
    for (const auto& storedPath : storedPaths)
    {
        if (QFileInfo::exists(storedPath))
        {
            existingPaths.push_back(storedPath);
        }
    }

    if (existingPaths != storedPaths)
    {
        storeRecentProjectPaths(existingPaths);
    }

    if (existingPaths.isEmpty())
    {
        auto* placeholderAction = m_openRecentMenu->addAction(QStringLiteral("No Recent Projects"));
        placeholderAction->setEnabled(false);
        return;
    }

    for (const auto& projectPath : existingPaths)
    {
        const QFileInfo projectInfo(projectPath);
        const auto displayName = projectInfo.completeBaseName().isEmpty()
            ? projectInfo.fileName()
            : projectInfo.completeBaseName();
        const auto parentPath = QDir::toNativeSeparators(projectInfo.absolutePath());
        auto* recentAction = m_openRecentMenu->addAction(
            QStringLiteral("%1  -  %2").arg(displayName, parentPath));
        recentAction->setToolTip(QDir::toNativeSeparators(projectPath));
        recentAction->setStatusTip(QDir::toNativeSeparators(projectPath));
        connect(recentAction, &QAction::triggered, this, [this, projectPath]()
        {
            static_cast<void>(openProjectFileWithPrompt(projectPath, QStringLiteral("open another project")));
        });
    }
}

void MainWindow::setProjectDirty(const bool dirty)
{
    if (!hasOpenProject() || m_projectDirty == dirty)
    {
        return;
    }

    m_projectDirty = dirty;
    if (m_saveProjectAction)
    {
        m_saveProjectAction->setEnabled(m_projectDirty);
    }
    updateWindowTitle();
}

void MainWindow::updateWindowTitle()
{
    QStringList parts{QStringLiteral("dawg")};
    if (!m_currentProjectName.isEmpty())
    {
        parts.push_back(m_currentProjectName + (m_projectDirty ? QStringLiteral("*") : QString{}));
    }
    if (!m_clipName.isEmpty())
    {
        parts.push_back(m_clipName);
    }
    setWindowTitle(parts.join(QStringLiteral(" - ")));

    if (m_detachedVideoWindow)
    {
        QStringList detachedParts{QStringLiteral("Detached Video"), QStringLiteral("dawg")};
        if (!m_currentProjectName.isEmpty())
        {
            detachedParts.push_back(m_currentProjectName);
        }
        m_detachedVideoWindow->setWindowTitle(detachedParts.join(QStringLiteral(" - ")));
        m_detachedVideoWindow->setWindowIcon(windowIcon());
    }
}

bool MainWindow::promptToSaveIfDirty(const QString& actionLabel)
{
    if (!hasOpenProject() || !m_projectDirty)
    {
        return true;
    }

    QMessageBox messageBox(this);
    messageBox.setIcon(QMessageBox::Warning);
    messageBox.setWindowTitle(QStringLiteral("Unsaved Changes"));
    const auto projectLabel = m_currentProjectName.isEmpty() ? m_currentProjectFilePath : m_currentProjectName;
    messageBox.setText(QStringLiteral("Do you want to save the changes made to \"%1\"?").arg(projectLabel));
    messageBox.setInformativeText(QStringLiteral("Your changes will be lost if you don't save them before you %1.").arg(actionLabel));
    messageBox.setStandardButtons(QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    messageBox.setDefaultButton(QMessageBox::Save);
    const auto result = messageBox.exec();

    if (result == QMessageBox::Save)
    {
        return saveProjectToCurrentPath();
    }

    return result == QMessageBox::Discard;
}

bool MainWindow::ensureProjectForMediaAction(const QString& actionLabel)
{
    if (hasOpenProject())
    {
        return true;
    }

    QMessageBox messageBox(this);
    messageBox.setIcon(QMessageBox::Information);
    messageBox.setWindowTitle(QStringLiteral("Project Required"));
    messageBox.setText(QStringLiteral("Create or open a project before you %1.").arg(actionLabel));
    auto* newButton = messageBox.addButton(QStringLiteral("New Project"), QMessageBox::AcceptRole);
    auto* openButton = messageBox.addButton(QStringLiteral("Open Project"), QMessageBox::ActionRole);
    messageBox.addButton(QStringLiteral("Cancel"), QMessageBox::RejectRole);
    messageBox.exec();

    if (messageBox.clickedButton() == newButton)
    {
        newProject();
    }
    else if (messageBox.clickedButton() == openButton)
    {
        openProject();
    }

    return hasOpenProject();
}

void MainWindow::applyFileDialogChrome(QFileDialog& dialog) const
{
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);

    QList<QUrl> sidebarUrls;
    const auto addSidebarUrl = [&sidebarUrls](const QUrl& url)
    {
        if (!url.isValid() || sidebarUrls.contains(url))
        {
            return;
        }

        sidebarUrls.push_back(url);
    };
    const auto addSidebarLocation = [&addSidebarUrl](const QStandardPaths::StandardLocation location)
    {
        const auto path = QStandardPaths::writableLocation(location);
        if (path.isEmpty())
        {
            return;
        }

        addSidebarUrl(QUrl::fromLocalFile(path));
    };

    addSidebarUrl(QUrl(QStringLiteral("file:///")));
    addSidebarLocation(QStandardPaths::DesktopLocation);
    addSidebarLocation(QStandardPaths::HomeLocation);
    addSidebarLocation(QStandardPaths::DocumentsLocation);
    addSidebarLocation(QStandardPaths::MoviesLocation);
    addSidebarLocation(QStandardPaths::MusicLocation);
    for (const auto& url : dialog.sidebarUrls())
    {
        addSidebarUrl(url);
    }

    if (!sidebarUrls.isEmpty())
    {
        dialog.setSidebarUrls(sidebarUrls);
    }
}

QString MainWindow::chooseOpenFileName(
    const QString& title,
    const QString& directory,
    const QString& filter) const
{
    const auto initialDirectory = directory.isEmpty() ? QString{} : directory;
    QFileDialog dialog(const_cast<MainWindow*>(this), title, initialDirectory, filter);
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
    dialog.setFileMode(QFileDialog::ExistingFile);
    applyFileDialogChrome(dialog);
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty())
    {
        return {};
    }

    return dialog.selectedFiles().constFirst();
}

QString MainWindow::chooseExistingDirectory(const QString& title, const QString& directory) const
{
    const auto initialDirectory = directory.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::DesktopLocation)
        : directory;
    QFileDialog dialog(const_cast<MainWindow*>(this), title, initialDirectory);
    dialog.setAcceptMode(QFileDialog::AcceptOpen);
    dialog.setFileMode(QFileDialog::Directory);
    dialog.setOption(QFileDialog::ShowDirsOnly, true);
    applyFileDialogChrome(dialog);
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty())
    {
        return {};
    }

    return dialog.selectedFiles().constFirst();
}

std::optional<QString> MainWindow::copyMediaIntoProject(
    const QString& sourcePath,
    const QString& subdirectory,
    QString* errorMessage) const
{
    if (!hasOpenProject())
    {
        if (errorMessage)
        {
            *errorMessage = QStringLiteral("No project is open.");
        }
        return std::nullopt;
    }

    const QFileInfo sourceInfo(sourcePath);
    if (!sourceInfo.exists() || !sourceInfo.isFile())
    {
        if (errorMessage)
        {
            *errorMessage = QStringLiteral("Media file does not exist: %1").arg(sourcePath);
        }
        return std::nullopt;
    }

    const auto targetDirectoryPath = QDir(m_currentProjectRootPath).filePath(subdirectory);
    QDir rootDirectory(m_currentProjectRootPath);
    if (!rootDirectory.mkpath(subdirectory))
    {
        if (errorMessage)
        {
            *errorMessage = QStringLiteral("Failed to create project folder %1.").arg(targetDirectoryPath);
        }
        return std::nullopt;
    }

    if (pathIsInsideRoot(targetDirectoryPath, sourceInfo.absoluteFilePath()))
    {
        return QDir::cleanPath(sourceInfo.absoluteFilePath());
    }

    const auto targetPath = uniqueTargetFilePath(targetDirectoryPath, sourceInfo.absoluteFilePath());
    if (!QFile::copy(sourceInfo.absoluteFilePath(), targetPath))
    {
        if (errorMessage)
        {
            *errorMessage = QStringLiteral("Failed to copy %1 into the project.")
                .arg(sourceInfo.fileName());
        }
        return std::nullopt;
    }

    return QDir::cleanPath(targetPath);
}

dawg::project::UiState MainWindow::snapshotProjectUiState() const
{
    dawg::project::UiState state;
    state.videoDetached = m_videoDetached;
    state.detachedVideoWindowGeometry = m_videoDetached && m_detachedVideoWindow
                                            ? m_detachedVideoWindow->saveGeometry()
                                            : m_detachedVideoWindowGeometry;
    state.timelineVisible = m_timelinePanel && m_timelinePanel->isVisible();
    state.clipEditorVisible = m_clipEditorPanel && m_clipEditorPanel->isVisible();
    state.mixVisible = m_mixPanel && m_mixPanel->isVisible();
    state.audioPoolVisible = m_audioPoolPanel && m_audioPoolPanel->isVisible();
    state.audioPoolShowLength = m_audioPoolShowLength;
    state.audioPoolShowSize = m_audioPoolShowSize;
    state.showAllNodeNames = m_showAllNodeNamesAction && m_showAllNodeNamesAction->isChecked();
    state.timelineClickSeeks = m_timelineClickSeeksAction && m_timelineClickSeeksAction->isChecked();
    state.audioPoolPreferredWidth = m_audioPoolPreferredWidth;
    state.timelinePreferredHeight = m_timelinePreferredHeight;
    state.clipEditorPreferredHeight = m_clipEditorPreferredHeight;
    state.mixPreferredHeight = m_mixPreferredHeight;
    state.windowGeometry = saveGeometry();
    state.windowMaximized = isMaximized();
    if (m_contentSplitter)
    {
        const auto sizes = m_contentSplitter->sizes();
        state.contentSplitterSizes.reserve(static_cast<std::size_t>(sizes.size()));
        for (const auto size : sizes)
        {
            state.contentSplitterSizes.push_back(size);
        }
    }
    if (m_mainVerticalSplitter)
    {
        const auto sizes = m_mainVerticalSplitter->sizes();
        state.mainVerticalSplitterSizes.reserve(static_cast<std::size_t>(sizes.size()));
        auto persistedSizes = sizes;
        if (m_videoDetached && persistedSizes.size() == 4)
        {
            persistedSizes[0] = std::max(400, m_canvasPanel ? m_canvasPanel->height() : 400);
        }
        for (const auto size : persistedSizes)
        {
            state.mainVerticalSplitterSizes.push_back(size);
        }
    }
    return state;
}

void MainWindow::applyProjectUiState(const dawg::project::UiState& state)
{
    m_projectStateChangeInProgress = true;
    m_detachedVideoWindowGeometry = state.detachedVideoWindowGeometry;
    m_audioPoolPreferredWidth = std::max(240, state.audioPoolPreferredWidth);
    m_audioPoolShowLength = state.audioPoolShowLength;
    m_audioPoolShowSize = state.audioPoolShowSize;
    m_timelinePreferredHeight = std::max(96, state.timelinePreferredHeight);
    m_clipEditorPreferredHeight = std::max(148, state.clipEditorPreferredHeight);
    m_mixPreferredHeight = std::max(132, state.mixPreferredHeight);

    if (!state.windowGeometry.isEmpty())
    {
        restoreGeometry(state.windowGeometry);
    }
    if (state.windowMaximized)
    {
        showMaximized();
    }
    else if (isMaximized())
    {
        showNormal();
    }

    if (m_showAllNodeNamesAction)
    {
        const QSignalBlocker blocker{m_showAllNodeNamesAction};
        m_showAllNodeNamesAction->setChecked(state.showAllNodeNames);
    }
    m_canvas->setShowAllLabels(state.showAllNodeNames);
    if (m_nativeViewport)
    {
        m_nativeViewport->setShowAllLabels(state.showAllNodeNames);
    }

    if (m_timelineClickSeeksAction)
    {
        const QSignalBlocker blocker{m_timelineClickSeeksAction};
        m_timelineClickSeeksAction->setChecked(state.timelineClickSeeks);
    }
    if (m_timeline)
    {
        m_timeline->setSeekOnClickEnabled(state.timelineClickSeeks);
    }

    updateTimelineVisibility(state.timelineVisible);
    updateClipEditorVisibility(state.clipEditorVisible);
    updateMixVisibility(state.mixVisible);
    updateAudioPoolVisibility(state.audioPoolVisible);
    refreshAudioPool();

    if (m_contentSplitter && state.contentSplitterSizes.size() == 2)
    {
        QList<int> sizes;
        for (const auto size : state.contentSplitterSizes)
        {
            sizes.push_back(size);
        }
        m_contentSplitter->setSizes(sizes);
    }
    if (m_mainVerticalSplitter && state.mainVerticalSplitterSizes.size() == 4)
    {
        QList<int> sizes;
        for (const auto size : state.mainVerticalSplitterSizes)
        {
            sizes.push_back(size);
        }
        m_mainVerticalSplitter->setSizes(sizes);
    }
    else
    {
        syncMainVerticalPanelSizes();
    }

    if (state.videoDetached != m_videoDetached)
    {
        if (state.videoDetached)
        {
            detachVideo();
        }
        else
        {
            attachVideo();
        }
    }

    m_projectStateChangeInProgress = false;
}

bool MainWindow::saveProjectToPath(const QString& projectFilePath, const QString& projectName)
{
    if (projectFilePath.isEmpty())
    {
        return false;
    }

    auto controllerState = m_controller->snapshotProjectState();
    const auto projectRootPath = QFileInfo(projectFilePath).absolutePath();
    const QDir projectRoot(projectRootPath);

    const auto makeRelativePath = [&projectRootPath, &projectRoot](const QString& path, QString* errorMessage) -> std::optional<QString>
    {
        if (path.isEmpty())
        {
            return QString{};
        }

        const auto cleanedPath = QDir::cleanPath(QDir::fromNativeSeparators(path));
        const auto absolutePath = QFileInfo(cleanedPath).isAbsolute()
            ? QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo(cleanedPath).absoluteFilePath()))
            : QDir::cleanPath(QDir::fromNativeSeparators(projectRoot.absoluteFilePath(cleanedPath)));
        const auto relativePath = QDir::cleanPath(QDir::fromNativeSeparators(projectRoot.relativeFilePath(absolutePath)));
        const auto escapesProjectRoot =
            relativePath == QStringLiteral("..")
            || relativePath.startsWith(QStringLiteral("../"))
            || QDir::isAbsolutePath(relativePath);
        if (escapesProjectRoot)
        {
            if (errorMessage)
            {
                *errorMessage = QStringLiteral("Project media is outside the project folder: %1").arg(absolutePath);
            }
            return std::nullopt;
        }
        return relativePath;
    };

    QString errorMessage;
    if (const auto relativeVideoPath = makeRelativePath(controllerState.videoPath, &errorMessage); relativeVideoPath.has_value())
    {
        controllerState.videoPath = *relativeVideoPath;
    }
    else
    {
        QMessageBox::warning(this, QStringLiteral("Save Project"), errorMessage);
        return false;
    }

    for (auto& assetPath : controllerState.audioPoolAssetPaths)
    {
        const auto relativeAssetPath = makeRelativePath(assetPath, &errorMessage);
        if (!relativeAssetPath.has_value())
        {
            QMessageBox::warning(this, QStringLiteral("Save Project"), errorMessage);
            return false;
        }
        assetPath = *relativeAssetPath;
    }

    for (auto& track : controllerState.trackerState.tracks)
    {
        if (!track.attachedAudio.has_value())
        {
            continue;
        }
        const auto relativeAssetPath = makeRelativePath(track.attachedAudio->assetPath, &errorMessage);
        if (!relativeAssetPath.has_value())
        {
            QMessageBox::warning(this, QStringLiteral("Save Project"), errorMessage);
            return false;
        }
        track.attachedAudio->assetPath = *relativeAssetPath;
    }

    const dawg::project::Document document{
        .name = dawg::project::sanitizeProjectName(projectName),
        .controller = controllerState,
        .ui = snapshotProjectUiState()
    };

    if (!dawg::project::saveDocument(projectFilePath, document, &errorMessage))
    {
        QMessageBox::warning(this, QStringLiteral("Save Project"), errorMessage);
        return false;
    }

    setCurrentProject(projectFilePath, document.name);
    addRecentProjectPath(projectFilePath);
    setProjectDirty(false);
    showStatus(QStringLiteral("Saved project %1.").arg(document.name));
    return true;
}

bool MainWindow::saveProjectToCurrentPath()
{
    if (!hasOpenProject())
    {
        return false;
    }

    return saveProjectToPath(m_currentProjectFilePath, m_currentProjectName);
}

bool MainWindow::createProjectAt(const QString& projectName, const QString& parentDirectory)
{
    const auto sanitizedProjectName = dawg::project::sanitizeProjectName(projectName);
    const auto projectRootPath = QDir(parentDirectory).filePath(sanitizedProjectName);
    QDir projectRoot(projectRootPath);
    if (projectRoot.exists() && !projectRoot.entryList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty())
    {
        QMessageBox::warning(
            this,
            QStringLiteral("New Project"),
            QStringLiteral("Project folder already exists and is not empty:\n%1").arg(projectRootPath));
        return false;
    }

    if (!QDir().mkpath(projectRoot.filePath(QStringLiteral("audio")))
        || !QDir().mkpath(projectRoot.filePath(QStringLiteral("video")))
        || !QDir().mkpath(projectRoot.filePath(QStringLiteral("settings"))))
    {
        QMessageBox::warning(
            this,
            QStringLiteral("New Project"),
            QStringLiteral("Failed to create project folders in:\n%1").arg(projectRootPath));
        return false;
    }

    const auto projectFilePath = projectRoot.filePath(dawg::project::projectFileNameForName(sanitizedProjectName));
    m_projectStateChangeInProgress = true;
    m_controller->resetProjectState();
    m_projectStateChangeInProgress = false;
    setCurrentProject(projectFilePath, sanitizedProjectName);
    if (!saveProjectToPath(projectFilePath, sanitizedProjectName))
    {
        clearCurrentProject();
        return false;
    }
    return true;
}

bool MainWindow::openProjectFileWithPrompt(const QString& projectFilePath, const QString& actionLabel)
{
    const auto normalizedProjectPath = normalizeProjectFilePath(projectFilePath);
    if (normalizedProjectPath.isEmpty())
    {
        return false;
    }

    if (!promptToSaveIfDirty(actionLabel))
    {
        return false;
    }

    if (hasOpenProject() && !m_projectDirty && !saveProjectToCurrentPath())
    {
        return false;
    }

    if (!QFileInfo::exists(normalizedProjectPath))
    {
        removeRecentProjectPath(normalizedProjectPath);
        QMessageBox::warning(
            this,
            QStringLiteral("Open Project"),
            QStringLiteral("Project file not found:\n%1").arg(QDir::toNativeSeparators(normalizedProjectPath)));
        return false;
    }

    return loadProjectFile(normalizedProjectPath);
}

bool MainWindow::loadProjectFile(const QString& projectFilePath)
{
    QString errorMessage;
    const auto document = dawg::project::loadDocument(projectFilePath, &errorMessage);
    if (!document.has_value())
    {
        QMessageBox::warning(this, QStringLiteral("Open Project"), errorMessage);
        return false;
    }

    auto absoluteControllerState = document->controller;
    const auto projectRootPath = QFileInfo(projectFilePath).absolutePath();
    const QDir projectRoot(projectRootPath);

    const auto makeAbsolutePath = [&projectRoot](const QString& relativePath) -> QString
    {
        if (relativePath.isEmpty())
        {
            return {};
        }
        return QDir::cleanPath(projectRoot.absoluteFilePath(relativePath));
    };

    absoluteControllerState.videoPath = makeAbsolutePath(absoluteControllerState.videoPath);
    for (auto& assetPath : absoluteControllerState.audioPoolAssetPaths)
    {
        assetPath = makeAbsolutePath(assetPath);
    }
    for (auto& track : absoluteControllerState.trackerState.tracks)
    {
        if (track.attachedAudio.has_value())
        {
            track.attachedAudio->assetPath = makeAbsolutePath(track.attachedAudio->assetPath);
        }
    }

    QString recoveryMessage;
    const auto recoveredMediaFromFolders =
        recoverProjectMediaFromFolders(&absoluteControllerState, projectRootPath, &recoveryMessage);

    m_projectStateChangeInProgress = true;
    const auto restored = m_controller->restoreProjectState(absoluteControllerState, &errorMessage);
    if (restored)
    {
        applyProjectUiState(document->ui);
        setCurrentProject(projectFilePath, document->name);
        addRecentProjectPath(projectFilePath);
        setProjectDirty(false);
        if (recoveredMediaFromFolders)
        {
            setProjectDirty(true);
        }
    }
    m_projectStateChangeInProgress = false;

    if (!restored)
    {
        QMessageBox::warning(this, QStringLiteral("Open Project"), errorMessage);
        return false;
    }

    showStatus(
        recoveredMediaFromFolders
            ? QStringLiteral("Opened project %1. %2").arg(document->name, recoveryMessage)
            : QStringLiteral("Opened project %1.").arg(document->name));
    return true;
}

bool MainWindow::saveProjectAsNewCopy()
{
    if (!hasOpenProject())
    {
        return false;
    }

    bool ok = false;
    const auto projectName = QInputDialog::getText(
        this,
        QStringLiteral("Save Project As"),
        QStringLiteral("Project name:"),
        QLineEdit::Normal,
        m_currentProjectName,
        &ok);
    if (!ok)
    {
        return false;
    }

    const auto sanitizedProjectName = dawg::project::sanitizeProjectName(projectName);
    const auto parentDirectory = chooseExistingDirectory(QStringLiteral("Choose Destination Folder"));
    if (parentDirectory.isEmpty())
    {
        return false;
    }

    const auto targetRootPath = QDir(parentDirectory).filePath(sanitizedProjectName);
    QDir targetRoot(targetRootPath);
    if (targetRoot.exists() && !targetRoot.entryList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty())
    {
        QMessageBox::warning(
            this,
            QStringLiteral("Save Project As"),
            QStringLiteral("Destination folder already exists and is not empty:\n%1").arg(targetRootPath));
        return false;
    }

    if (!QDir().mkpath(targetRoot.filePath(QStringLiteral("audio")))
        || !QDir().mkpath(targetRoot.filePath(QStringLiteral("video")))
        || !QDir().mkpath(targetRoot.filePath(QStringLiteral("settings"))))
    {
        QMessageBox::warning(
            this,
            QStringLiteral("Save Project As"),
            QStringLiteral("Failed to create destination project folders."));
        return false;
    }

    auto controllerState = m_controller->snapshotProjectState();
    QHash<QString, QString> copiedAudioPaths;
    QString errorMessage;

    const auto copyIntoTargetProject = [&targetRootPath](const QString& sourcePath, const QString& subdirectory, QString* error) -> std::optional<QString>
    {
        if (sourcePath.isEmpty())
        {
            return QString{};
        }

        const QFileInfo sourceInfo(sourcePath);
        if (!sourceInfo.exists() || !sourceInfo.isFile())
        {
            if (error)
            {
                *error = QStringLiteral("Missing project media: %1").arg(sourcePath);
            }
            return std::nullopt;
        }

        const auto targetDirectoryPath = QDir(targetRootPath).filePath(subdirectory);
        const auto targetPath = uniqueTargetFilePath(targetDirectoryPath, sourceInfo.absoluteFilePath());
        if (!QFile::copy(sourceInfo.absoluteFilePath(), targetPath))
        {
            if (error)
            {
                *error = QStringLiteral("Failed to copy %1 to the new project.").arg(sourceInfo.fileName());
            }
            return std::nullopt;
        }
        return QDir::cleanPath(targetPath);
    };

    if (!controllerState.videoPath.isEmpty())
    {
        const auto copiedVideoPath = copyIntoTargetProject(controllerState.videoPath, QStringLiteral("video"), &errorMessage);
        if (!copiedVideoPath.has_value())
        {
            QMessageBox::warning(this, QStringLiteral("Save Project As"), errorMessage);
            return false;
        }
        controllerState.videoPath = *copiedVideoPath;
    }

    for (auto& assetPath : controllerState.audioPoolAssetPaths)
    {
        const auto existingIt = copiedAudioPaths.constFind(assetPath);
        if (existingIt != copiedAudioPaths.cend())
        {
            assetPath = existingIt.value();
            continue;
        }

        const auto copiedAudioPath = copyIntoTargetProject(assetPath, QStringLiteral("audio"), &errorMessage);
        if (!copiedAudioPath.has_value())
        {
            QMessageBox::warning(this, QStringLiteral("Save Project As"), errorMessage);
            return false;
        }
        copiedAudioPaths.insert(assetPath, *copiedAudioPath);
        assetPath = *copiedAudioPath;
    }

    for (auto& track : controllerState.trackerState.tracks)
    {
        if (!track.attachedAudio.has_value())
        {
            continue;
        }

        const auto existingIt = copiedAudioPaths.constFind(track.attachedAudio->assetPath);
        if (existingIt != copiedAudioPaths.cend())
        {
            track.attachedAudio->assetPath = existingIt.value();
            continue;
        }

        const auto copiedAudioPath = copyIntoTargetProject(track.attachedAudio->assetPath, QStringLiteral("audio"), &errorMessage);
        if (!copiedAudioPath.has_value())
        {
            QMessageBox::warning(this, QStringLiteral("Save Project As"), errorMessage);
            return false;
        }
        copiedAudioPaths.insert(track.attachedAudio->assetPath, *copiedAudioPath);
        track.attachedAudio->assetPath = *copiedAudioPath;
    }

    const auto targetProjectFilePath = targetRoot.filePath(dawg::project::projectFileNameForName(sanitizedProjectName));
    const auto currentUiState = snapshotProjectUiState();
    const auto relativeRoot = QDir(targetRootPath);
    auto relativeControllerState = controllerState;
    relativeControllerState.videoPath = controllerState.videoPath.isEmpty()
        ? QString{}
        : QDir::cleanPath(relativeRoot.relativeFilePath(controllerState.videoPath));
    for (auto& assetPath : relativeControllerState.audioPoolAssetPaths)
    {
        assetPath = QDir::cleanPath(relativeRoot.relativeFilePath(assetPath));
    }
    for (auto& track : relativeControllerState.trackerState.tracks)
    {
        if (track.attachedAudio.has_value())
        {
            track.attachedAudio->assetPath = QDir::cleanPath(relativeRoot.relativeFilePath(track.attachedAudio->assetPath));
        }
    }

    const dawg::project::Document document{
        .name = sanitizedProjectName,
        .controller = relativeControllerState,
        .ui = currentUiState
    };
    if (!dawg::project::saveDocument(targetProjectFilePath, document, &errorMessage))
    {
        QMessageBox::warning(this, QStringLiteral("Save Project As"), errorMessage);
        return false;
    }

    m_projectStateChangeInProgress = true;
    const auto restored = m_controller->restoreProjectState(controllerState, &errorMessage);
    m_projectStateChangeInProgress = false;
    if (!restored)
    {
        QMessageBox::warning(this, QStringLiteral("Save Project As"), errorMessage);
        return false;
    }

    setCurrentProject(targetProjectFilePath, sanitizedProjectName);
    addRecentProjectPath(targetProjectFilePath);
    setProjectDirty(false);
    showStatus(QStringLiteral("Saved project copy as %1.").arg(sanitizedProjectName));
    return true;
}

void MainWindow::restoreLastProjectOnStartup()
{
    QSettings settings;
    const auto lastProjectPath = settings.value(QString::fromLatin1(kLastProjectPathSettingsKey)).toString();
    if (lastProjectPath.isEmpty())
    {
        updateWindowTitle();
        return;
    }

    if (!QFileInfo::exists(lastProjectPath) || !loadProjectFile(lastProjectPath))
    {
        settings.remove(QString::fromLatin1(kLastProjectPathSettingsKey));
        removeRecentProjectPath(lastProjectPath);
        clearCurrentProject();
    }
}

void MainWindow::newProject()
{
    if (!promptToSaveIfDirty(QStringLiteral("create a new project")))
    {
        return;
    }

    if (hasOpenProject() && !m_projectDirty && !saveProjectToCurrentPath())
    {
        return;
    }

    bool ok = false;
    const auto projectName = QInputDialog::getText(
        this,
        QStringLiteral("New Project"),
        QStringLiteral("Project name:"),
        QLineEdit::Normal,
        QStringLiteral("Untitled Project"),
        &ok);
    if (!ok)
    {
        return;
    }

    const auto parentDirectory = chooseExistingDirectory(QStringLiteral("Choose Project Location"));
    if (parentDirectory.isEmpty())
    {
        return;
    }

    static_cast<void>(createProjectAt(projectName, parentDirectory));
}

void MainWindow::openProject()
{
    if (!promptToSaveIfDirty(QStringLiteral("open another project")))
    {
        return;
    }

    if (hasOpenProject() && !m_projectDirty && !saveProjectToCurrentPath())
    {
        return;
    }

    const auto projectFilePath = chooseOpenFileName(
        QStringLiteral("Open Project"),
        {},
        QStringLiteral("DAWG Projects (*%1)").arg(QString::fromLatin1(dawg::project::kProjectFileSuffix)));
    if (projectFilePath.isEmpty())
    {
        return;
    }

    static_cast<void>(openProjectFileWithPrompt(projectFilePath, QStringLiteral("open another project")));
}

void MainWindow::saveProject()
{
    static_cast<void>(saveProjectToCurrentPath());
}

void MainWindow::saveProjectAs()
{
    static_cast<void>(saveProjectAsNewCopy());
}

void MainWindow::openVideo()
{
    if (!ensureProjectForMediaAction(QStringLiteral("import a video")))
    {
        return;
    }

    const auto currentProjectState = m_controller->snapshotProjectState();
    if (m_controller->hasVideoLoaded()
        || !currentProjectState.audioPoolAssetPaths.empty()
        || !currentProjectState.trackerState.tracks.empty())
    {
        const auto choice = QMessageBox::question(
            this,
            QStringLiteral("Replace Project Video"),
            QStringLiteral("Opening a new video will clear the current nodes and audio pool for this project. Continue?"),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No);
        if (choice != QMessageBox::Yes)
        {
            return;
        }
    }

    const auto filePath = chooseOpenFileName(
        QStringLiteral("Import Video"),
        QStandardPaths::writableLocation(QStandardPaths::MoviesLocation),
        QStringLiteral("Video Files (*.mp4 *.mov *.mkv *.avi);;All Files (*.*)"));

    if (filePath.isEmpty())
    {
        return;
    }

    QString errorMessage;
    const auto copiedFilePath = copyMediaIntoProject(filePath, QStringLiteral("video"), &errorMessage);
    if (!copiedFilePath.has_value())
    {
        QMessageBox::warning(this, QStringLiteral("Import Video"), errorMessage);
        return;
    }

    m_projectStateChangeInProgress = true;
    const auto opened = m_controller->openVideo(*copiedFilePath);
    m_projectStateChangeInProgress = false;
    if (opened)
    {
        setProjectDirty(true);
    }
}

void MainWindow::importSound()
{
    if (!ensureProjectForMediaAction(QStringLiteral("import audio")))
    {
        return;
    }

    if (!m_controller->hasSelection())
    {
        showStatus(QStringLiteral("Select a node before importing audio."));
        return;
    }

    const auto filePath = chooseOpenFileName(
        QStringLiteral("Import Audio"),
        QStandardPaths::writableLocation(QStandardPaths::MusicLocation),
        QStringLiteral("Audio Files (*.wav *.mp3 *.flac *.aif *.aiff *.m4a *.aac *.ogg);;All Files (*.*)"));

    if (filePath.isEmpty())
    {
        return;
    }

    QString errorMessage;
    const auto copiedFilePath = copyMediaIntoProject(filePath, QStringLiteral("audio"), &errorMessage);
    if (!copiedFilePath.has_value())
    {
        QMessageBox::warning(this, QStringLiteral("Import Audio"), errorMessage);
        return;
    }

    m_controller->importSoundForSelectedTrack(*copiedFilePath);
}

void MainWindow::importAudioToPool()
{
    if (!ensureProjectForMediaAction(QStringLiteral("import audio")))
    {
        return;
    }

    const auto filePath = chooseOpenFileName(
        QStringLiteral("Import Audio"),
        QStandardPaths::writableLocation(QStandardPaths::MusicLocation),
        QStringLiteral("Audio Files (*.wav *.mp3 *.flac *.aif *.aiff *.m4a *.aac *.ogg);;All Files (*.*)"));

    if (filePath.isEmpty())
    {
        return;
    }

    QString errorMessage;
    const auto copiedFilePath = copyMediaIntoProject(filePath, QStringLiteral("audio"), &errorMessage);
    if (!copiedFilePath.has_value())
    {
        QMessageBox::warning(this, QStringLiteral("Import Audio"), errorMessage);
        return;
    }

    if (m_controller->importAudioToPool(*copiedFilePath))
    {
        showStatus(QStringLiteral("Imported %1 to the audio pool.").arg(QFileInfo(*copiedFilePath).fileName()));
    }
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
    const auto* focused = QApplication::focusWidget();
    const auto clipEditorFocused =
        m_clipEditorView && focused && (focused == m_clipEditorView || m_clipEditorView->isAncestorOf(focused));
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

    if (m_timeline && m_timeline->loopShortcutFrame().has_value())
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
    const auto* focused = QApplication::focusWidget();
    const auto clipEditorFocused =
        m_clipEditorView && focused && (focused == m_clipEditorView || m_clipEditorView->isAncestorOf(focused));
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

    if (m_timeline && m_timeline->loopShortcutFrame().has_value())
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
    m_lastPresentedFrame = image;
    m_canvas->setPresentedFrame(image, m_controller->currentVideoFrame(), m_controller->videoFrameSize());
    if (m_nativeViewportWindow && m_nativeViewportWindow->isVisible() && m_nativeViewport)
    {
        m_nativeViewport->setPresentedFrame(image, m_controller->currentVideoFrame(), m_controller->videoFrameSize());
    }
    const auto displayFrameIndex = std::max(0, frameIndex);
    const auto displayTimestampSeconds = std::max(0.0, timestampSeconds);
    m_timeline->setCurrentFrame(displayFrameIndex);
    m_frameLabel->setText(
        QStringLiteral("Frame %1  |  %2 s")
            .arg(displayFrameIndex)
            .arg(displayTimestampSeconds, 0, 'f', 2));
    if (m_audioPoolPanel && m_audioPoolPanel->isVisible())
    {
        updateAudioPoolPlaybackIndicators();
    }

    if (m_controller->isPlaying())
    {
        if (!m_outputFpsTimer.isValid())
        {
            m_outputFpsTimer.start();
            m_outputFpsFrameCount = 0;
            m_outputFps = 0.0;
        }

        ++m_outputFpsFrameCount;
        const auto elapsedMs = std::max<qint64>(1, m_outputFpsTimer.elapsed());
        if (elapsedMs >= 250)
        {
            const auto measuredFps = static_cast<double>(m_outputFpsFrameCount) * 1000.0 / static_cast<double>(elapsedMs);
            m_outputFps = m_outputFps > 0.0
                ? (m_outputFps * 0.4) + (measuredFps * 0.6)
                : measuredFps;
            m_outputFpsTimer.restart();
            m_outputFpsFrameCount = 0;
        }
    }

    refreshClipEditor();
    updateDebugText();
}

void MainWindow::updateMemoryUsage()
{
    m_memoryUsageText = currentMemoryUsageText();
    m_processorUsageText = currentProcessorUsageText();
    m_videoMemoryUsageText = currentVideoMemoryUsageText();
    updateDebugText();
}

void MainWindow::updateDebugText()
{
    if (!m_debugOverlay || !m_debugVisible)
    {
        return;
    }

    if (m_controller
        && m_controller->isPlaying()
        && m_debugTextTimer.isValid()
        && m_debugTextTimer.elapsed() < 250)
    {
        return;
    }

    const auto totalFrames = std::max(0, m_controller->totalFrames());
    const auto currentFrame = m_controller->hasVideoLoaded() ? m_controller->currentFrameIndex() : 0;
    const auto currentSeconds = m_controller->fps() > 0.0
        ? static_cast<double>(currentFrame) / m_controller->fps()
        : 0.0;
    const auto clipText = m_clipName.isEmpty() ? QStringLiteral("No clip") : m_clipName;
    const auto fpsText = m_controller->fps() > 0.0
        ? QString::number(m_controller->fps(), 'f', 2)
        : QStringLiteral("--");
    const auto outputFpsText = m_controller->isPlaying()
        ? (m_outputFps > 0.0
            ? QString::number(m_outputFps, 'f', 2)
            : (m_outputFpsTimer.isValid() && m_outputFpsTimer.elapsed() > 0
                ? QString::number(
                    static_cast<double>(m_outputFpsFrameCount) * 1000.0
                        / static_cast<double>(std::max<qint64>(1, m_outputFpsTimer.elapsed())),
                    'f',
                    2)
                : QStringLiteral("--")))
        : QStringLiteral("--");
    const auto insertionText = m_controller->isInsertionFollowsPlayback()
        ? QStringLiteral("On")
        : QStringLiteral("Off");
    const auto processorText = m_processorUsageText.isEmpty() ? QStringLiteral("CPU --") : m_processorUsageText;
    const auto memoryText = m_memoryUsageText.isEmpty() ? QStringLiteral("Memory --") : m_memoryUsageText;
    const auto videoMemoryText = m_videoMemoryUsageText.isEmpty() ? QStringLiteral("VRAM --") : m_videoMemoryUsageText;
    const auto decoderText = m_controller->decoderBackendName().isEmpty()
        ? QStringLiteral("Decode --")
        : QStringLiteral("Decode %1").arg(m_controller->decoderBackendName());
    const auto renderText = m_controller->renderBackendName().isEmpty()
        ? QStringLiteral("Render --")
        : QStringLiteral("Render %1").arg(m_controller->renderBackendName());

    m_debugOverlay->setListText(
        QStringLiteral(
            "Clip: %1\n"
            "Motion: %2\n"
            "Insert Follow: %3\n"
            "Frame: %4 / %5\n"
            "Time: %6 s\n"
            "Video FPS: %7\n"
            "FPS Output: %8\n"
            "Nodes: %9\n"
            "Selected: %10\n"
            "%11\n"
            "%12\n"
            "%13\n"
            "%14\n"
            "%15")
            .arg(clipText)
            .arg(m_controller->isMotionTrackingEnabled() ? QStringLiteral("On") : QStringLiteral("Off"))
            .arg(insertionText)
            .arg(currentFrame)
            .arg(totalFrames)
            .arg(currentSeconds, 0, 'f', 2)
            .arg(fpsText)
            .arg(outputFpsText)
            .arg(m_controller->trackCount())
            .arg(m_controller->hasSelection() ? QStringLiteral("Yes") : QStringLiteral("No"))
            .arg(processorText)
            .arg(memoryText)
            .arg(videoMemoryText)
            .arg(decoderText)
            .arg(renderText));
    m_debugTextTimer.restart();
}

void MainWindow::refreshOverlays()
{
    m_canvas->setOverlays(m_controller->currentOverlays());
    if (m_nativeViewportWindow && m_nativeViewportWindow->isVisible() && m_nativeViewport)
    {
        m_nativeViewport->setOverlays(m_controller->currentOverlays());
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
    if (m_audioPoolPanel && m_audioPoolPanel->isVisible())
    {
        updateAudioPoolPlaybackIndicators();
    }
    updateDebugText();
}

void MainWindow::updateMotionTrackingState(const bool enabled)
{
    syncMotionTrackingUi(enabled);
    m_motionTrackingAction->setChecked(enabled);
    updateDebugText();
}

void MainWindow::updateSelectionState(const bool hasSelection)
{
    m_unselectAllAction->setEnabled(hasSelection);
    m_setNodeStartAction->setEnabled(hasSelection);
    m_setNodeEndAction->setEnabled(hasSelection);
    m_selectNextNodeAction->setEnabled(hasSelection || m_controller->hasTracks());
    m_moveNodeUpAction->setEnabled(hasSelection);
    m_moveNodeDownAction->setEnabled(hasSelection);
    m_moveNodeLeftAction->setEnabled(hasSelection);
    m_moveNodeRightAction->setEnabled(hasSelection);
    m_trimNodeAction->setEnabled(hasSelection);
    m_autoPanAction->setEnabled(hasSelection);
    if (m_autoPanAction)
    {
        const QSignalBlocker blocker{m_autoPanAction};
        m_autoPanAction->setChecked(hasSelection && m_controller->selectedTracksAutoPanEnabled());
    }
    m_toggleNodeNameAction->setEnabled(hasSelection);
    m_importSoundAction->setEnabled(hasSelection);
    m_deleteNodeAction->setEnabled(hasSelection);
    refreshClipEditor();
    updateEditActionState();
    updateDebugText();
}

void MainWindow::updateTrackAvailabilityState(const bool hasTracks)
{
    m_selectAllAction->setEnabled(hasTracks);
    m_clearAllAction->setEnabled(hasTracks);
    if (m_selectNextNodeAction)
    {
        m_selectNextNodeAction->setEnabled(hasTracks);
    }
    if (!hasTracks)
    {
        clearPendingClearAllShortcut();
    }
    updateEditActionState();
    updateDebugText();
}

void MainWindow::handleVideoLoaded(const QString& filePath, const int totalFrames, const double fps)
{
    resetOutputFpsTracking();
    m_debugTextTimer.invalidate();
    const QFileInfo fileInfo{filePath};
    m_clipName = fileInfo.fileName();
    if (filePath.isEmpty())
    {
        m_timeline->clear();
    }
    else
    {
        m_timeline->setTimeline(totalFrames, fps);
    }
    if (m_nativeViewportWindow)
    {
        m_nativeViewportWindow->setWindowTitle(QStringLiteral("Native Video Viewport Test - %1").arg(m_clipName));
    }
    if (m_nativeViewport)
    {
        m_nativeViewport->setPresentedFrame(m_lastPresentedFrame, m_controller->currentVideoFrame(), m_controller->videoFrameSize());
        m_nativeViewport->setOverlays(m_controller->currentOverlays());
    }
    refreshTimeline();
    refreshClipEditor();
    if (!filePath.isEmpty())
    {
        showCanvasTipsOverlay();
    }
    updateDebugText();
    updateWindowTitle();
}

void MainWindow::updateDebugVisibility(const bool enabled)
{
    m_debugVisible = enabled;
    m_debugTextTimer.invalidate();
    if (m_debugOverlay)
    {
        m_debugOverlay->setVisible(enabled);
    }
    if (m_toggleDebugAction && m_toggleDebugAction->isChecked() != enabled)
    {
        m_toggleDebugAction->setChecked(enabled);
    }
}

void MainWindow::updateNativeViewportVisibility(const bool visible)
{
    if (!m_nativeViewportWindow)
    {
        return;
    }

    if (visible)
    {
        if (m_nativeViewport)
        {
            m_nativeViewport->setPresentedFrame(m_lastPresentedFrame, m_controller->currentVideoFrame(), m_controller->videoFrameSize());
            m_nativeViewport->setOverlays(m_controller->currentOverlays());
            m_nativeViewport->setShowAllLabels(m_showAllNodeNamesAction && m_showAllNodeNamesAction->isChecked());
        }

        m_nativeViewportWindow->show();
        m_nativeViewportWindow->raise();
        m_nativeViewportWindow->activateWindow();
    }
    else
    {
        m_nativeViewportWindow->hide();
    }

    if (m_showNativeViewportAction && m_showNativeViewportAction->isChecked() != visible)
    {
        const QSignalBlocker blocker{m_showNativeViewportAction};
        m_showNativeViewportAction->setChecked(visible);
    }
}

void MainWindow::updateAudioPoolVisibility(const bool visible)
{
    if (m_audioPoolPanel)
    {
        if (!visible)
        {
            m_audioPoolPreferredWidth = std::max(240, m_audioPoolPanel->width());
        }
        m_audioPoolPanel->setVisible(visible);
    }

    if (visible && m_contentSplitter && m_audioPoolPanel)
    {
        const auto totalWidth = std::max(800, m_contentSplitter->width());
        const auto poolWidth = std::clamp(m_audioPoolPreferredWidth, 240, std::max(240, totalWidth / 2));
        m_contentSplitter->setSizes({std::max(400, totalWidth - poolWidth), poolWidth});
    }

    if (m_audioPoolAction && m_audioPoolAction->isChecked() != visible)
    {
        const QSignalBlocker blocker{m_audioPoolAction};
        m_audioPoolAction->setChecked(visible);
    }
}

void MainWindow::updateTimelineVisibility(const bool visible)
{
    if (m_timelinePanel)
    {
        if (!visible)
        {
            m_timelinePreferredHeight = std::max(96, m_timelinePanel->height());
        }
        m_timelinePanel->setVisible(visible);
    }

    syncMainVerticalPanelSizes();

    if (m_showTimelineAction && m_showTimelineAction->isChecked() != visible)
    {
        const QSignalBlocker blocker{m_showTimelineAction};
        m_showTimelineAction->setChecked(visible);
    }
}

void MainWindow::updateClipEditorVisibility(const bool visible)
{
    if (m_clipEditorPanel)
    {
        if (!visible)
        {
            m_clipEditorPreferredHeight = std::max(148, m_clipEditorPanel->height());
        }
        m_clipEditorPanel->setVisible(visible);
    }

    if (visible)
    {
        refreshClipEditor();
    }
    else
    {
        m_clipEditorPreviewTimer.stop();
    }

    syncMainVerticalPanelSizes();

    if (m_showClipEditorAction && m_showClipEditorAction->isChecked() != visible)
    {
        const QSignalBlocker blocker{m_showClipEditorAction};
        m_showClipEditorAction->setChecked(visible);
    }
}

void MainWindow::updateMixVisibility(const bool visible)
{
    if (m_mixPanel)
    {
        if (!visible)
        {
            m_mixPreferredHeight = std::max(132, m_mixPanel->height());
        }
        m_mixPanel->setVisible(visible);
    }

    if (visible)
    {
        if (!m_mixMeterTimer.isActive())
        {
            m_mixMeterTimer.start();
        }
        refreshMixView();
    }
    else
    {
        m_mixMeterTimer.stop();
    }

    syncMainVerticalPanelSizes();

    if (m_showMixAction && m_showMixAction->isChecked() != visible)
    {
        const QSignalBlocker blocker{m_showMixAction};
        m_showMixAction->setChecked(visible);
    }
}

void MainWindow::detachVideo()
{
    if (m_videoDetached || !m_canvas || !m_canvasPanel || !m_detachedVideoWindow)
    {
        return;
    }

    if (auto* panelLayout = qobject_cast<QVBoxLayout*>(m_canvasPanel->layout()))
    {
        panelLayout->removeWidget(m_canvas);
    }
    if (auto* detachedLayout = qobject_cast<QVBoxLayout*>(m_detachedVideoWindow->layout()))
    {
        detachedLayout->addWidget(m_canvas, 1);
    }

    if (!m_detachedVideoWindowGeometry.isEmpty())
    {
        m_detachedVideoWindow->restoreGeometry(m_detachedVideoWindowGeometry);
    }
    else
    {
        const auto canvasSize = m_canvasPanel->size().isValid() ? m_canvasPanel->size() : QSize{960, 540};
        m_detachedVideoWindow->resize(canvasSize.expandedTo(QSize{640, 360}));
    }
    m_canvasPanel->hide();
    m_videoDetached = true;
    updateDetachedVideoUiState();
    syncMainVerticalPanelSizes();
    m_detachedVideoWindow->show();
    m_detachedVideoWindow->raise();
    m_detachedVideoWindow->activateWindow();
    if (!m_projectStateChangeInProgress && hasOpenProject())
    {
        setProjectDirty(true);
    }
    if (!m_projectStateChangeInProgress)
    {
        showStatus(QStringLiteral("Video detached."));
    }
}

void MainWindow::attachVideo()
{
    if (!m_videoDetached || !m_canvas || !m_canvasPanel || !m_detachedVideoWindow)
    {
        return;
    }

    m_detachedVideoWindowGeometry = m_detachedVideoWindow->saveGeometry();

    if (auto* detachedLayout = qobject_cast<QVBoxLayout*>(m_detachedVideoWindow->layout()))
    {
        detachedLayout->removeWidget(m_canvas);
    }
    if (auto* panelLayout = qobject_cast<QVBoxLayout*>(m_canvasPanel->layout()))
    {
        panelLayout->addWidget(m_canvas, 1);
    }

    m_detachedVideoWindow->hide();
    m_canvasPanel->show();
    m_videoDetached = false;
    updateDetachedVideoUiState();
    syncMainVerticalPanelSizes();
    m_canvas->setFocus(Qt::OtherFocusReason);
    if (!m_projectStateChangeInProgress && hasOpenProject())
    {
        setProjectDirty(true);
    }
    if (!m_projectStateChangeInProgress)
    {
        showStatus(QStringLiteral("Video attached."));
    }
}

void MainWindow::updateDetachedVideoUiState()
{
    if (m_detachVideoAction)
    {
        m_detachVideoAction->setText(m_videoDetached
                                        ? QStringLiteral("Attach Video")
                                        : QStringLiteral("Detach Video"));
        m_detachVideoAction->setEnabled(m_canvas != nullptr);
    }
}

void MainWindow::resetOutputFpsTracking()
{
    m_outputFpsTimer.invalidate();
    m_outputFpsFrameCount = 0;
    m_outputFps = 0.0;
}

void MainWindow::syncMainVerticalPanelSizes()
{
    if (!m_mainVerticalSplitter)
    {
        return;
    }

    const auto totalHeight = std::max(320, m_mainVerticalSplitter->height());
    const auto timelineVisible = m_timelinePanel && m_timelinePanel->isVisible();
    const auto clipEditorVisible = m_clipEditorPanel && m_clipEditorPanel->isVisible();
    const auto mixVisible = m_mixPanel && m_mixPanel->isVisible();
    struct PanelTarget
    {
        bool visible = false;
        int preferred = 0;
        int minimum = 0;
        int assigned = 0;
    };

    std::array<PanelTarget, 3> panels{{
        PanelTarget{timelineVisible, m_timelinePreferredHeight, 96, 0},
        PanelTarget{clipEditorVisible, m_clipEditorPreferredHeight, 148, 0},
        PanelTarget{mixVisible, m_mixPreferredHeight, 132, 0}
    }};

    const auto videoAttached = m_canvasPanel && m_canvasPanel->isVisible() && !m_videoDetached;
    const auto canvasMinimum = videoAttached ? 220 : 0;
    const auto availableForPanels = std::max(0, totalHeight - canvasMinimum);
    int assignedTotal = 0;
    for (auto& panel : panels)
    {
        if (!panel.visible)
        {
            continue;
        }

        panel.assigned = std::max(panel.minimum, panel.preferred);
        assignedTotal += panel.assigned;
    }

    int overflow = assignedTotal - availableForPanels;
    while (overflow > 0)
    {
        bool reducedAny = false;
        for (auto& panel : panels)
        {
            if (!panel.visible || panel.assigned <= panel.minimum)
            {
                continue;
            }

            --panel.assigned;
            --overflow;
            reducedAny = true;
            if (overflow <= 0)
            {
                break;
            }
        }

        if (!reducedAny)
        {
            break;
        }
    }

    const auto canvasHeight = videoAttached
        ? std::max(200, totalHeight - panels[0].assigned - panels[1].assigned - panels[2].assigned)
        : 0;
    m_mainVerticalSplitter->setSizes({canvasHeight, panels[0].assigned, panels[1].assigned, panels[2].assigned});
}

void MainWindow::refreshAudioPool()
{
    if (!m_audioPoolListLayout)
    {
        return;
    }

    while (auto* item = m_audioPoolListLayout->takeAt(0))
    {
        if (auto* widget = item->widget())
        {
            widget->deleteLater();
        }
        delete item;
    }

    const auto items = m_controller->audioPoolItems();
    if (items.empty())
    {
        auto* emptyLabel = new QLabel(QStringLiteral("No imported sounds yet."), m_audioPoolListContainer);
        emptyLabel->setStyleSheet(QStringLiteral("color: #8d9aae; font-size: 9pt;"));
        m_audioPoolListLayout->addWidget(emptyLabel);
        m_audioPoolListLayout->addStretch(1);
        return;
    }

    for (const auto& item : items)
    {
        auto* row = new AudioPoolRow(
            item.assetPath,
            [this](const QString& assetPath)
            {
                return m_controller->startAudioPoolPreview(assetPath);
            },
            [this]()
            {
                m_controller->stopAudioPoolPreview();
            },
            [this, trackId = item.trackId]()
            {
                if (!trackId.isNull())
                {
                    m_controller->selectTrackAndJumpToStart(trackId);
                }
            },
            [this, assetPath = item.assetPath]()
            {
                m_controller->createTrackWithAudioAtCurrentFrame(assetPath);
            },
            m_audioPoolListContainer);
        row->setProperty("audioPoolItemKey", item.key);
        row->setProperty("assetPath", item.assetPath);
        row->setProperty("trackId", item.trackId);
        row->setToolTip(item.connectionSummary);
        row->setStyleSheet(QStringLiteral(
            "QWidget { background: transparent; border: none; }"
            "QWidget:hover { background: rgba(255, 255, 255, 0.03); border-radius: 4px; }"));

        auto* rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(8, 0, 8, 0);
        rowLayout->setSpacing(6);

        auto* statusDot = new QLabel(row);
        statusDot->setObjectName(QStringLiteral("audioPoolStatusDot"));
        statusDot->setFixedSize(8, 8);
        statusDot->setAttribute(Qt::WA_TransparentForMouseEvents);
        statusDot->setStyleSheet(
            item.isPlaying
                ? QStringLiteral("background: #63c987; border-radius: 4px;")
                : (item.connectedNodeCount > 0
                    ? QStringLiteral("background: #d88932; border-radius: 4px;")
                    : QStringLiteral("background: #cf5f5f; border-radius: 4px;")));

        auto* nameLabel = new QLabel(item.displayName, row);
        nameLabel->setObjectName(QStringLiteral("audioPoolNameLabel"));
        nameLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
        nameLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        nameLabel->setStyleSheet(
            item.connectedNodeCount > 0
                ? QStringLiteral("color: #d8e0ea; font-size: 8.5pt; padding: 0; margin: 0;")
                : QStringLiteral("color: #9ea9b7; font-size: 8.5pt; padding: 0; margin: 0;"));
        nameLabel->setToolTip(item.connectionSummary);

        auto* lengthLabel = new QLabel(formatAudioPoolDuration(item.durationMs), row);
        lengthLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
        lengthLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        lengthLabel->setFixedWidth(56);
        lengthLabel->setVisible(m_audioPoolShowLength);
        lengthLabel->setStyleSheet(QStringLiteral("color: #91a0b1; font-size: 8pt; padding: 0; margin: 0;"));

        auto* sizeLabel = new QLabel(formatAudioPoolSize(item.fileSizeBytes), row);
        sizeLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
        sizeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        sizeLabel->setFixedWidth(64);
        sizeLabel->setVisible(m_audioPoolShowSize);
        sizeLabel->setStyleSheet(QStringLiteral("color: #91a0b1; font-size: 8pt; padding: 0; margin: 0;"));

        auto* editButton = new QToolButton(row);
        editButton->setCursor(Qt::PointingHandCursor);
        editButton->setText(QStringLiteral("\u2630"));
        editButton->setFixedWidth(28);
        editButton->setToolTip(QStringLiteral("Edit %1").arg(item.displayName));
        editButton->setStyleSheet(QStringLiteral(
            "QToolButton { background: transparent; color: #d7dee8; border: none; border-radius: 4px; padding: 1px 4px; font-size: 8.5pt; }"
            "QToolButton::menu-indicator { image: none; width: 0px; }"
            "QToolButton:hover { background: rgba(255, 255, 255, 0.08); }"
            "QToolButton:pressed { background: #111821; }"));

        auto* editMenu = new QMenu(editButton);
        auto menuFont = editMenu->font();
        menuFont.setPointSizeF(8.5);
        editMenu->setFont(menuFont);
        editMenu->setStyleSheet(QStringLiteral(
            "QMenu { background: rgba(8, 10, 13, 204); color: #eef2f6; border: 1px solid #1f2935; border-radius: 8px; padding: 4px 0; font-size: 8.5pt; }"
            "QMenu::item { padding: 6px 14px; }"
            "QMenu::item:selected { background: #1a2028; }"));
        auto* addAction = editMenu->addAction(QStringLiteral("Add to Frame"));
        auto* deleteAudioAction = editMenu->addAction(QStringLiteral("Delete Audio"));
        auto* deleteAudioAndNodesAction = editMenu->addAction(QStringLiteral("Delete Audio + Nodes"));

        connect(addAction, &QAction::triggered, this, [this, assetPath = item.assetPath]()
        {
            m_controller->createTrackWithAudioAtCurrentFrame(assetPath);
        });
        connect(deleteAudioAction, &QAction::triggered, this, [this, assetPath = item.assetPath]()
        {
            m_controller->removeAudioFromPool(assetPath);
        });
        connect(deleteAudioAndNodesAction, &QAction::triggered, this, [this, assetPath = item.assetPath]()
        {
            m_controller->removeAudioAndConnectedNodesFromPool(assetPath);
        });
        connect(editButton, &QToolButton::clicked, this, [editButton, editMenu]()
        {
            const auto menuSize = editMenu->sizeHint();
            auto popupPosition = editButton->mapToGlobal(QPoint(editButton->width() - menuSize.width(), editButton->height()));

            if (auto* screen = QGuiApplication::screenAt(popupPosition))
            {
                const auto available = screen->availableGeometry();
                popupPosition.setX(std::clamp(popupPosition.x(), available.left(), available.right() - menuSize.width()));
                popupPosition.setY(std::clamp(popupPosition.y(), available.top(), available.bottom() - menuSize.height()));
            }

            editMenu->exec(popupPosition);
        });

        rowLayout->addWidget(statusDot, 0, Qt::AlignVCenter);
        rowLayout->addWidget(nameLabel, 1);
        rowLayout->addWidget(lengthLabel, 0, Qt::AlignVCenter);
        rowLayout->addWidget(sizeLabel, 0, Qt::AlignVCenter);
        rowLayout->addWidget(editButton, 0, Qt::AlignVCenter);
        m_audioPoolListLayout->addWidget(row);
    }

    m_audioPoolListLayout->addStretch(1);
}

void MainWindow::updateAudioPoolPlaybackIndicators()
{
    if (!m_audioPoolListContainer)
    {
        return;
    }

    const auto items = m_controller->audioPoolItems();
    const auto rowObjects = m_audioPoolListContainer->children();

    for (QObject* child : rowObjects)
    {
        auto* row = qobject_cast<QWidget*>(child);
        if (!row || !row->property("assetPath").isValid())
        {
            continue;
        }

        const auto itemKey = row->property("audioPoolItemKey").toString();
        const auto itemIt = std::find_if(
            items.begin(),
            items.end(),
            [&itemKey](const AudioPoolItem& item)
            {
                return item.key == itemKey;
            });
        if (itemIt == items.end())
        {
            continue;
        }

        row->setToolTip(itemIt->connectionSummary);

        if (auto* statusDot = row->findChild<QLabel*>(QStringLiteral("audioPoolStatusDot"), Qt::FindDirectChildrenOnly))
        {
            statusDot->setStyleSheet(
                itemIt->isPlaying
                    ? QStringLiteral("background: #63c987; border-radius: 4px;")
                    : (itemIt->connectedNodeCount > 0
                        ? QStringLiteral("background: #d88932; border-radius: 4px;")
                        : QStringLiteral("background: #cf5f5f; border-radius: 4px;")));
        }

        if (auto* nameLabel = row->findChild<QLabel*>(QStringLiteral("audioPoolNameLabel"), Qt::FindDirectChildrenOnly))
        {
            nameLabel->setToolTip(itemIt->connectionSummary);
        }
    }
}

void MainWindow::updateVideoAudioRow()
{
    if (!m_videoAudioRow || !m_videoAudioLabel || !m_videoAudioMuteButton)
    {
        return;
    }

    const auto hasVideoAudio = m_controller->hasEmbeddedVideoAudio();
    m_videoAudioRow->setVisible(hasVideoAudio);
    if (!hasVideoAudio)
    {
        return;
    }

    const auto displayName = m_controller->embeddedVideoAudioDisplayName();
    m_videoAudioLabel->setText(displayName);
    m_videoAudioLabel->setToolTip(
        QStringLiteral("Embedded audio from %1%2")
            .arg(displayName)
            .arg(m_controller->isFastPlaybackEnabled() ? QStringLiteral("\nFast Playback enabled") : QString{}));

    const auto muted = m_controller->isEmbeddedVideoAudioMuted();
    m_videoAudioMuteButton->setText(QStringLiteral("\u2630"));
    m_videoAudioMuteButton->setToolTip(
        muted
            ? QStringLiteral("Video audio options (currently muted)")
            : QStringLiteral("Video audio options"));
}

void MainWindow::showStatus(const QString& message)
{
    if (!m_statusToast || message.trimmed().isEmpty())
    {
        return;
    }

    m_statusToast->setText(message);
    m_statusToast->adjustSize();
    const auto maxWidth = std::min(560, std::max(220, width() - 64));
    m_statusToast->setMaximumWidth(maxWidth);
    m_statusToast->adjustSize();
    updateOverlayPositions();
    m_statusToast->show();
    m_statusToast->raise();
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

    const auto* focused = QApplication::focusWidget();
    if (!focused)
    {
        return false;
    }

    return focused->inherits("QLineEdit")
        || focused->inherits("QTextEdit")
        || focused->inherits("QPlainTextEdit")
        || focused->inherits("QAbstractSpinBox")
        || focused->inherits("QComboBox");
}

void MainWindow::updateOverlayPositions()
{
    auto* root = centralWidget();
    if (!root)
    {
        return;
    }

    if (m_statusToast)
    {
        const auto margin = 16;
        const auto x = margin;
        const auto y = std::max(margin, root->height() - m_statusToast->height() - margin);
        m_statusToast->move(x, y);
    }

    if (m_canvasTipsOverlay && m_canvas)
    {
        m_canvasTipsOverlay->move(16, 16);
    }

    if (m_statusToast && m_statusToast->isVisible())
    {
        m_statusToast->raise();
    }
    if (m_canvasTipsOverlay && m_canvasTipsOverlay->isVisible())
    {
        m_canvasTipsOverlay->raise();
    }
}

void MainWindow::showCanvasTipsOverlay()
{
    if (!m_canvasTipsOverlay)
    {
        return;
    }

    m_canvasTipsOverlay->setText(
        QStringLiteral("Left-click to add or select nodes\n"
                       "Right-click a node for options\n"
                       "Drag audio from Audio Pool onto the video\n"
                       "Space plays, , and . step frames"));
    m_canvasTipsOverlay->adjustSize();
    updateOverlayPositions();
    m_canvasTipsOverlay->show();
    m_canvasTipsOverlay->raise();
    m_canvasTipsTimer.start();
}

void MainWindow::hideCanvasTipsOverlay()
{
    m_canvasTipsTimer.stop();
    if (m_canvasTipsOverlay)
    {
        m_canvasTipsOverlay->hide();
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

    QMenu menu(this);
    auto* renameEditor = new QLineEdit(nodeLabel, &menu);
    renameEditor->setPlaceholderText(QStringLiteral("Node name"));
    renameEditor->setFrame(false);
    renameEditor->setFocusPolicy(Qt::ClickFocus);
    renameEditor->setCursorPosition(0);
    renameEditor->deselect();
    auto renameFont = renameEditor->font();
    renameFont.setPointSizeF(8.5);
    renameFont.setBold(true);
    renameEditor->setFont(renameFont);
    auto menuFont = menu.font();
    menuFont.setPointSizeF(8.5);
    menu.setFont(menuFont);
    menu.setStyleSheet(QStringLiteral(
        "QMenu { background: rgba(18, 23, 32, 204); border: 1px solid #324155; border-radius: 8px; font-size: 8.5pt; }"
        "QMenu::item { padding: 6px 14px; }"
        "QMenu::item:selected { background: #223146; }"));
    renameEditor->setStyleSheet(R"(
        QLineEdit {
            background: rgba(255, 255, 255, 0.03);
            color: #f3f5f7;
            border: 1px solid transparent;
            border-radius: 6px;
            padding: 4px 8px;
            selection-background-color: #223146;
            selection-color: #f3f5f7;
        }
        QLineEdit:focus {
            background: #1a2330;
            border: 1px solid #324155;
            border-radius: 6px;
            padding: 3px 7px;
        }
    )");
    auto* renameAction = new QWidgetAction(&menu);
    renameAction->setDefaultWidget(renameEditor);
    menu.addAction(renameAction);
    menu.addSeparator();
    auto* importAudioAction = menu.addAction(QStringLiteral("Import Audio..."));
    QAction* trimAction = nullptr;
    QAction* autoPanAction = nullptr;
    if (hasAttachedAudio)
    {
        trimAction = menu.addAction(QStringLiteral("Trim Node (Shift+T)"));
    }
    if (includeSoundActions)
    {
        autoPanAction = menu.addAction(QStringLiteral("Auto Pan (R)"));
        autoPanAction->setCheckable(true);
        autoPanAction->setChecked(m_controller->trackAutoPanEnabled(trackId));
    }

    const QFontMetrics metrics{renameEditor->font()};
    const auto labelWidth = metrics.horizontalAdvance(nodeLabel);
    const auto menuContentWidth = std::clamp(labelWidth + 40, 140, 720);
    renameEditor->setMinimumWidth(menuContentWidth);
    renameEditor->setMaximumWidth(menuContentWidth + 80);
    menu.setActiveAction(importAudioAction);

    connect(renameEditor, &QLineEdit::returnPressed, &menu, [&menu, this, trackId, renameEditor, nodeLabel]()
    {
        const auto updatedLabel = renameEditor->text().trimmed();
        if (!updatedLabel.isEmpty() && updatedLabel != nodeLabel)
        {
            m_controller->renameTrack(trackId, updatedLabel);
        }
        menu.close();
    });

    const auto* chosenAction = menu.exec(globalPosition);
    if (chosenAction == importAudioAction)
    {
        importSound();
        return;
    }

    if (chosenAction == trimAction)
    {
        trimSelectedNodeToSound();
        return;
    }

    if (chosenAction == autoPanAction)
    {
        toggleSelectedNodeAutoPan();
    }
}

void MainWindow::refreshTimeline()
{
    if (!m_timeline && !m_mixView)
    {
        return;
    }

    const auto trackSpans = m_controller->timelineTrackSpans();
    if (m_timeline)
    {
        m_timeline->setTrackSpans(trackSpans);
        m_timeline->setLoopRange(m_controller->loopStartFrame(), m_controller->loopEndFrame());
    }
    if (m_clearLoopRangeAction)
    {
        m_clearLoopRangeAction->setEnabled(
            m_controller->loopStartFrame().has_value() || m_controller->loopEndFrame().has_value());
    }
    refreshMixView();
}

void MainWindow::refreshClipEditor()
{
    if (!m_clipEditorView || !m_clipEditorPanel || !m_clipEditorPanel->isVisible())
    {
        m_clipEditorPreviewTimer.stop();
        return;
    }

    m_clipEditorView->setState(m_controller->selectedClipEditorState());
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

void MainWindow::refreshMixView()
{
    if (m_mixView && m_mixPanel && m_mixPanel->isVisible())
    {
        const auto laneStrips = m_controller->mixLaneStrips();
        m_mixView->setMixState(
            m_controller->masterMixGainDb(),
            m_controller->masterMixMuted(),
            laneStrips);
        m_mixView->setMeterLevels(m_controller->masterMixLevel(), laneStrips);
    }
}

void MainWindow::updateEditActionState()
{
    if (m_loopSoundAction)
    {
        const auto selectedTrackId = m_controller->selectedTrackId();
        const auto hasSelectedTrackAudio = !selectedTrackId.isNull() && m_controller->trackHasAttachedAudio(selectedTrackId);
        const QSignalBlocker blocker{m_loopSoundAction};
        m_loopSoundAction->setEnabled(hasSelectedTrackAudio);
        m_loopSoundAction->setChecked(hasSelectedTrackAudio && m_controller->selectedTrackLoopEnabled());
    }
    if (m_copyAction)
    {
        m_copyAction->setEnabled(m_controller->hasSelection());
    }
    if (m_cutAction)
    {
        m_cutAction->setEnabled(m_controller->hasSelection());
    }
    if (m_pasteAction)
    {
        m_pasteAction->setEnabled(m_controller->canPasteTracks());
    }
    if (m_undoAction)
    {
        m_undoAction->setEnabled(m_controller->canUndoTrackEdit());
    }
    if (m_redoAction)
    {
        m_redoAction->setEnabled(m_controller->canRedoTrackEdit());
    }
}

bool MainWindow::shouldApplyNodeShortcutToAll() const
{
    return m_timeline
        && m_timeline->hasFocus()
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

    QToolTip::showText(globalPosition, mixGainDisplayText(*nextGainDb), this, {}, 900);
    if (m_trackGainPopup && m_trackGainPopup->isVisible() && m_trackGainPopupTrackId == trackId && m_trackGainPopupSlider)
    {
        const QSignalBlocker blocker{m_trackGainPopupSlider};
        m_trackGainPopupSlider->setValue(mixGainDbToSliderValue(*nextGainDb));
        updateTrackMixGainPopupValue(*nextGainDb);
    }
}

void MainWindow::showTrackMixGainPopup(const QUuid& trackId, const QPoint& globalPosition)
{
    if (trackId.isNull() || !m_trackGainPopup || !m_trackGainPopupSlider || !m_trackGainPopupValueLabel)
    {
        return;
    }

    const auto gainDb = m_controller->mixLaneGainForTrack(trackId);
    if (!gainDb.has_value())
    {
        return;
    }

    m_trackGainPopupTrackId = trackId;
    {
        const QSignalBlocker blocker{m_trackGainPopupSlider};
        m_trackGainPopupSlider->setValue(mixGainDbToSliderValue(*gainDb));
    }
    updateTrackMixGainPopupValue(*gainDb);
    m_trackGainPopup->adjustSize();

    auto popupPosition = globalPosition + QPoint(14, -(m_trackGainPopup->height() / 2));
    if (const auto* screen = QApplication::screenAt(globalPosition))
    {
        const auto available = screen->availableGeometry();
        const auto maxX = std::max(available.left(), available.right() - m_trackGainPopup->width() + 1);
        const auto maxY = std::max(available.top(), available.bottom() - m_trackGainPopup->height() + 1);
        popupPosition.setX(std::clamp(
            popupPosition.x(),
            available.left(),
            maxX));
        popupPosition.setY(std::clamp(
            popupPosition.y(),
            available.top(),
            maxY));
    }

    m_trackGainPopup->move(popupPosition);
    m_trackGainPopup->show();
    m_trackGainPopup->raise();
    m_trackGainPopup->activateWindow();
    m_trackGainPopupSlider->setFocus(Qt::PopupFocusReason);
}

void MainWindow::updateTrackMixGainPopupValue(const float gainDb)
{
    if (m_trackGainPopupValueLabel)
    {
        m_trackGainPopupValueLabel->setText(mixGainDisplayText(gainDb));
    }
}

std::optional<int> MainWindow::timelineLoopTargetFrame() const
{
    if (!m_controller || !m_controller->hasVideoLoaded())
    {
        return std::nullopt;
    }

    if (m_timeline && m_timeline->loopShortcutFrame().has_value())
    {
        return m_timeline->loopShortcutFrame();
    }

    return m_controller->currentFrameIndex();
}

void MainWindow::buildMenus()
{
    m_newProjectAction = new QAction(QStringLiteral("New Project..."), this);
    m_newProjectAction->setShortcut(QKeySequence::New);
    m_openProjectAction = new QAction(QStringLiteral("Open Project..."), this);
    m_openProjectAction->setShortcut(QKeySequence::Open);
    m_saveProjectAction = new QAction(QStringLiteral("Save Project"), this);
    m_saveProjectAction->setShortcut(QKeySequence::Save);
    m_saveProjectAsAction = new QAction(QStringLiteral("Save Project As..."), this);
    m_saveProjectAsAction->setShortcut(QKeySequence::SaveAs);
    m_openAction = new QAction(QStringLiteral("Import Video..."), this);
    m_openAction->setShortcut(QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_I));
    m_quitAction = new QAction(QStringLiteral("Quit (Ctrl+Q)"), this);
    m_quitAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Q")));
    m_quitAction->setShortcutContext(Qt::ApplicationShortcut);

    m_goToStartAction = new QAction(QStringLiteral("Jump to Start (Enter)"), this);
    m_playAction = new QAction(QStringLiteral("Play (Space)"), this);
    m_stepForwardAction = new QAction(QStringLiteral("Step Forward (.)"), this);
    m_stepBackAction = new QAction(QStringLiteral("Step Back (,)"), this);
    m_stepFastForwardAction = new QAction(QStringLiteral("Step Fast Forward (-)"), this);
    m_stepFastBackAction = new QAction(QStringLiteral("Step Fast Backward (M)"), this);
    m_insertionFollowsPlaybackAction = new QAction(QStringLiteral("Insertion Follows Playback (N)"), this);
    m_copyAction = new QAction(QStringLiteral("Copy (Ctrl+C)"), this);
    m_pasteAction = new QAction(QStringLiteral("Paste (Ctrl+V)"), this);
    m_cutAction = new QAction(QStringLiteral("Cut (Ctrl+X)"), this);
    m_undoAction = new QAction(QStringLiteral("Undo (Ctrl+Z)"), this);
    m_redoAction = new QAction(QStringLiteral("Redo (Ctrl+Y)"), this);
    m_selectAllAction = new QAction(QStringLiteral("Select All (Ctrl+A)"), this);
    m_unselectAllAction = new QAction(QStringLiteral("Unselect All (Esc)"), this);

    m_setNodeStartAction = new QAction(QStringLiteral("Set Start (A)"), this);
    m_setNodeEndAction = new QAction(QStringLiteral("Set End (S)"), this);
    m_setLoopStartAction = new QAction(QStringLiteral("Set Loop Start (A)"), this);
    m_setLoopEndAction = new QAction(QStringLiteral("Set Loop End (S)"), this);
    m_clearLoopRangeAction = new QAction(QStringLiteral("Clear Loop Range"), this);
    m_selectNextNodeAction = new QAction(QStringLiteral("Select Next (Tab)"), this);
    m_moveNodeUpAction = new QAction(QStringLiteral("Move Up (Up)"), this);
    m_moveNodeDownAction = new QAction(QStringLiteral("Move Down (Down)"), this);
    m_moveNodeLeftAction = new QAction(QStringLiteral("Move Left (Left)"), this);
    m_moveNodeRightAction = new QAction(QStringLiteral("Move Right (Right)"), this);
    m_trimNodeAction = new QAction(QStringLiteral("Trim Node (Shift+T)"), this);
    m_autoPanAction = new QAction(QStringLiteral("Auto Pan (R)"), this);
    m_loopSoundAction = new QAction(QStringLiteral("Loop Sound"), this);
    m_toggleNodeNameAction = new QAction(QStringLiteral("Toggle Node Name (E)"), this);
    m_showAllNodeNamesAction = new QAction(QStringLiteral("Node Name Always On"), this);
    m_importSoundAction = new QAction(QStringLiteral("Import Audio..."), this);
    m_detachVideoAction = new QAction(QStringLiteral("Detach Video"), this);
    m_showClipEditorAction = new QAction(QStringLiteral("Toggle Clip Editor (Ctrl+-)"), this);
    m_showTimelineAction = new QAction(QStringLiteral("Show Timeline (T)"), this);
    m_showMixAction = new QAction(QStringLiteral("Toggle Mix Window (Ctrl++)"), this);
    m_timelineClickSeeksAction = new QAction(QStringLiteral("Click Seeks Playhead"), this);
    m_audioPoolAction = new QAction(QStringLiteral("Audio Pool (P)"), this);
    m_deleteNodeAction = new QAction(QStringLiteral("Delete (Backspace)"), this);
    m_clearAllAction = new QAction(QStringLiteral("Clear All (Ctrl+Shift+A, Backspace)"), this);

    m_motionTrackingAction = new QAction(QStringLiteral("Motion Tracking"), this);
    m_motionTrackingAction->setCheckable(true);
    m_autoPanAction->setCheckable(true);
    m_loopSoundAction->setCheckable(true);
    m_insertionFollowsPlaybackAction->setCheckable(true);
    m_insertionFollowsPlaybackAction->setChecked(false);
    m_showAllNodeNamesAction->setCheckable(true);
    m_showAllNodeNamesAction->setChecked(true);
    m_showClipEditorAction->setCheckable(true);
    m_showClipEditorAction->setChecked(false);
    m_showTimelineAction->setCheckable(true);
    m_showTimelineAction->setChecked(true);
    m_showMixAction->setCheckable(true);
    m_showMixAction->setChecked(false);
    m_timelineClickSeeksAction->setCheckable(true);
    m_timelineClickSeeksAction->setChecked(true);
    m_audioPoolAction->setCheckable(true);
    m_audioPoolAction->setChecked(false);
    m_importSoundAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_I));
    m_toggleDebugAction = new QAction(QStringLiteral("Toggle Debug"), this);
    m_toggleDebugAction->setCheckable(true);
    m_toggleDebugAction->setChecked(true);
    m_showNativeViewportAction = new QAction(QStringLiteral("Native Video Viewport Test"), this);
    m_showNativeViewportAction->setCheckable(true);
    m_showNativeViewportAction->setChecked(false);

    m_setNodeStartAction->setEnabled(false);
    m_setNodeEndAction->setEnabled(false);
    m_clearLoopRangeAction->setEnabled(false);
    m_selectNextNodeAction->setEnabled(false);
    m_moveNodeUpAction->setEnabled(false);
    m_moveNodeDownAction->setEnabled(false);
    m_moveNodeLeftAction->setEnabled(false);
    m_moveNodeRightAction->setEnabled(false);
    m_trimNodeAction->setEnabled(false);
    m_autoPanAction->setEnabled(false);
    m_loopSoundAction->setEnabled(false);
    m_toggleNodeNameAction->setEnabled(false);
    m_importSoundAction->setEnabled(false);
    m_copyAction->setEnabled(false);
    m_pasteAction->setEnabled(false);
    m_cutAction->setEnabled(false);
    m_undoAction->setEnabled(false);
    m_redoAction->setEnabled(false);
    m_deleteNodeAction->setEnabled(false);
    m_selectAllAction->setEnabled(false);
    m_unselectAllAction->setEnabled(false);
    m_clearAllAction->setEnabled(false);
    m_saveProjectAction->setEnabled(false);
    m_saveProjectAsAction->setEnabled(false);

    auto* fileMenu = menuBar()->addMenu(QStringLiteral("&File"));
    fileMenu->addAction(m_newProjectAction);
    fileMenu->addAction(m_openProjectAction);
    m_openRecentMenu = fileMenu->addMenu(QStringLiteral("Open Recent..."));
    connect(m_openRecentMenu, &QMenu::aboutToShow, this, &MainWindow::rebuildRecentProjectsMenu);
    fileMenu->addAction(m_saveProjectAction);
    fileMenu->addAction(m_saveProjectAsAction);
    fileMenu->addSeparator();
    fileMenu->addAction(m_openAction);
    fileMenu->addSeparator();
    fileMenu->addAction(m_quitAction);
    addAction(m_quitAction);

    auto* editMenu = menuBar()->addMenu(QStringLiteral("&Edit"));
    editMenu->addAction(m_copyAction);
    editMenu->addAction(m_pasteAction);
    editMenu->addAction(m_cutAction);
    editMenu->addAction(m_undoAction);
    editMenu->addAction(m_redoAction);
    editMenu->addSeparator();
    editMenu->addAction(m_insertionFollowsPlaybackAction);
    editMenu->addAction(m_selectAllAction);
    editMenu->addAction(m_unselectAllAction);

    auto* nodeMenu = menuBar()->addMenu(QStringLiteral("&Node"));
    nodeMenu->addAction(m_selectNextNodeAction);
    nodeMenu->addSeparator();
    nodeMenu->addAction(m_moveNodeUpAction);
    nodeMenu->addAction(m_moveNodeDownAction);
    nodeMenu->addAction(m_moveNodeLeftAction);
    nodeMenu->addAction(m_moveNodeRightAction);
    nodeMenu->addSeparator();
    nodeMenu->addAction(m_setNodeStartAction);
    nodeMenu->addAction(m_setNodeEndAction);
    nodeMenu->addAction(m_trimNodeAction);
    nodeMenu->addSeparator();
    nodeMenu->addAction(m_deleteNodeAction);
    nodeMenu->addAction(m_clearAllAction);

    auto* motionMenu = menuBar()->addMenu(QStringLiteral("&Motion"));
    motionMenu->addAction(m_motionTrackingAction);

    auto* audioMenu = menuBar()->addMenu(QStringLiteral("&Audio"));
    audioMenu->addAction(m_importSoundAction);
    audioMenu->addAction(m_loopSoundAction);
    audioMenu->addAction(m_autoPanAction);

    auto* timelineMenu = menuBar()->addMenu(QStringLiteral("&Timeline"));
    timelineMenu->addAction(m_goToStartAction);
    timelineMenu->addAction(m_playAction);
    timelineMenu->addAction(m_stepForwardAction);
    timelineMenu->addAction(m_stepBackAction);
    timelineMenu->addAction(m_stepFastForwardAction);
    timelineMenu->addAction(m_stepFastBackAction);
    timelineMenu->addAction(m_insertionFollowsPlaybackAction);
    timelineMenu->addSeparator();
    timelineMenu->addAction(m_setLoopStartAction);
    timelineMenu->addAction(m_setLoopEndAction);
    timelineMenu->addAction(m_clearLoopRangeAction);
    timelineMenu->addSeparator();
    timelineMenu->addAction(m_timelineClickSeeksAction);

    auto* viewMenu = menuBar()->addMenu(QStringLiteral("&View"));
    viewMenu->addAction(m_detachVideoAction);
    viewMenu->addSeparator();
    viewMenu->addAction(m_showTimelineAction);
    viewMenu->addAction(m_showClipEditorAction);
    viewMenu->addAction(m_showMixAction);
    viewMenu->addAction(m_audioPoolAction);
    viewMenu->addSeparator();
    viewMenu->addAction(m_toggleNodeNameAction);
    viewMenu->addAction(m_showAllNodeNamesAction);

    auto* shortcutsMenu = menuBar()->addMenu(QStringLiteral("&Shortcuts"));
    const auto addShortcutEntry = [](QMenu* menu, const QString& label, const QString& shortcutText)
    {
        menu->addAction(QStringLiteral("%1\t%2").arg(label, shortcutText));
    };
    auto* fileShortcutsMenu = shortcutsMenu->addMenu(QStringLiteral("File"));
    addShortcutEntry(fileShortcutsMenu, QStringLiteral("New Project"), m_newProjectAction->shortcut().toString(QKeySequence::NativeText));
    addShortcutEntry(fileShortcutsMenu, QStringLiteral("Open Project"), m_openProjectAction->shortcut().toString(QKeySequence::NativeText));
    addShortcutEntry(fileShortcutsMenu, QStringLiteral("Save Project"), m_saveProjectAction->shortcut().toString(QKeySequence::NativeText));
    addShortcutEntry(fileShortcutsMenu, QStringLiteral("Save Project As"), m_saveProjectAsAction->shortcut().toString(QKeySequence::NativeText));
    addShortcutEntry(fileShortcutsMenu, QStringLiteral("Import Video"), m_openAction->shortcut().toString(QKeySequence::NativeText));
    addShortcutEntry(fileShortcutsMenu, QStringLiteral("Import Audio"), m_importSoundAction->shortcut().toString(QKeySequence::NativeText));
    addShortcutEntry(fileShortcutsMenu, QStringLiteral("Quit"), m_quitAction->shortcut().toString(QKeySequence::NativeText));

    auto* playbackShortcutsMenu = shortcutsMenu->addMenu(QStringLiteral("Playback"));
    addShortcutEntry(playbackShortcutsMenu, QStringLiteral("Play / Pause"), m_playPauseShortcut->key().toString(QKeySequence::NativeText));
    addShortcutEntry(
        playbackShortcutsMenu,
        QStringLiteral("Jump to Start"),
        QStringLiteral("%1 / %2")
            .arg(m_startShortcut->key().toString(QKeySequence::NativeText))
            .arg(m_numpadStartShortcut->key().toString(QKeySequence::NativeText)));
    addShortcutEntry(playbackShortcutsMenu, QStringLiteral("Step Back"), m_stepBackShortcut->key().toString(QKeySequence::NativeText));
    addShortcutEntry(playbackShortcutsMenu, QStringLiteral("Step Forward"), m_stepForwardShortcut->key().toString(QKeySequence::NativeText));
    addShortcutEntry(playbackShortcutsMenu, QStringLiteral("Step Fast Forward"), m_stepFastForwardShortcut->key().toString(QKeySequence::NativeText));
    addShortcutEntry(playbackShortcutsMenu, QStringLiteral("Step Fast Backward"), m_stepFastBackShortcut->key().toString(QKeySequence::NativeText));
    addShortcutEntry(
        playbackShortcutsMenu,
        QStringLiteral("Insertion Follows Playback"),
        m_insertionFollowsPlaybackShortcut->key().toString(QKeySequence::NativeText));

    auto* editShortcutsMenu = shortcutsMenu->addMenu(QStringLiteral("Edit"));
    addShortcutEntry(editShortcutsMenu, QStringLiteral("Copy"), m_copyShortcut->key().toString(QKeySequence::NativeText));
    addShortcutEntry(editShortcutsMenu, QStringLiteral("Paste"), m_pasteShortcut->key().toString(QKeySequence::NativeText));
    addShortcutEntry(editShortcutsMenu, QStringLiteral("Cut"), m_cutShortcut->key().toString(QKeySequence::NativeText));
    addShortcutEntry(editShortcutsMenu, QStringLiteral("Undo"), m_undoShortcut->key().toString(QKeySequence::NativeText));
    addShortcutEntry(editShortcutsMenu, QStringLiteral("Redo"), m_redoShortcut->key().toString(QKeySequence::NativeText));
    addShortcutEntry(editShortcutsMenu, QStringLiteral("Select All Visible Nodes"), m_selectAllShortcut->key().toString(QKeySequence::NativeText));
    addShortcutEntry(editShortcutsMenu, QStringLiteral("Clear Selection"), m_unselectAllShortcut->key().toString(QKeySequence::NativeText));
    addShortcutEntry(editShortcutsMenu, QStringLiteral("Delete Selected Node"), m_deleteShortcut->key().toString(QKeySequence::NativeText));
    addShortcutEntry(editShortcutsMenu, QStringLiteral("Clear All Nodes"), QStringLiteral("Ctrl+Shift+A, then Backspace"));

    auto* nodeShortcutsMenu = shortcutsMenu->addMenu(QStringLiteral("Node And Timeline"));
    addShortcutEntry(nodeShortcutsMenu, QStringLiteral("Set Start / Loop Start"), m_nodeStartShortcut->key().toString(QKeySequence::NativeText));
    addShortcutEntry(nodeShortcutsMenu, QStringLiteral("Set End / Loop End"), m_nodeEndShortcut->key().toString(QKeySequence::NativeText));
    addShortcutEntry(nodeShortcutsMenu, QStringLiteral("Select Next Node"), m_selectNextNodeShortcut->key().toString(QKeySequence::NativeText));
    addShortcutEntry(nodeShortcutsMenu, QStringLiteral("Move Selected Node"), QStringLiteral("Arrow Keys"));
    addShortcutEntry(nodeShortcutsMenu, QStringLiteral("Trim Node To Sound"), m_trimNodeShortcut->key().toString(QKeySequence::NativeText));
    addShortcutEntry(nodeShortcutsMenu, QStringLiteral("Toggle Auto Pan"), m_autoPanShortcut->key().toString(QKeySequence::NativeText));
    addShortcutEntry(nodeShortcutsMenu, QStringLiteral("Toggle Node Name"), m_toggleNodeNameShortcut->key().toString(QKeySequence::NativeText));
    addShortcutEntry(nodeShortcutsMenu, QStringLiteral("Show Timeline"), m_showTimelineShortcut->key().toString(QKeySequence::NativeText));
    addShortcutEntry(nodeShortcutsMenu, QStringLiteral("Toggle Clip Editor"), m_showClipEditorShortcut->key().toString(QKeySequence::NativeText));
    addShortcutEntry(nodeShortcutsMenu, QStringLiteral("Toggle Mix Window"), m_showMixShortcut->key().toString(QKeySequence::NativeText));
    addShortcutEntry(nodeShortcutsMenu, QStringLiteral("Toggle Audio Pool"), m_audioPoolShortcut->key().toString(QKeySequence::NativeText));

    auto* clipShortcutsMenu = shortcutsMenu->addMenu(QStringLiteral("Clip Editor"));
    addShortcutEntry(clipShortcutsMenu, QStringLiteral("Play / Stop Clip Preview"), QStringLiteral("Space"));
    addShortcutEntry(clipShortcutsMenu, QStringLiteral("Set Clip In"), QStringLiteral("A"));
    addShortcutEntry(clipShortcutsMenu, QStringLiteral("Set Clip Out"), QStringLiteral("S"));
    addShortcutEntry(clipShortcutsMenu, QStringLiteral("Waveform Zoom Horizontal"), QStringLiteral("Mouse Wheel"));
    addShortcutEntry(clipShortcutsMenu, QStringLiteral("Waveform Zoom Vertical"), QStringLiteral("Shift+Mouse Wheel"));

    auto* mouseShortcutsMenu = shortcutsMenu->addMenu(QStringLiteral("Mouse"));
    addShortcutEntry(mouseShortcutsMenu, QStringLiteral("Adjust Selected Node Mixer Volume"), QStringLiteral("Ctrl+Mouse Wheel"));
    addShortcutEntry(mouseShortcutsMenu, QStringLiteral("Open Node Mixer Volume Fader"), QStringLiteral("Ctrl+Click"));
    addShortcutEntry(mouseShortcutsMenu, QStringLiteral("Reset Mixer Or Clip Fader To 0 dB"), QStringLiteral("Ctrl+Click"));
    addShortcutEntry(mouseShortcutsMenu, QStringLiteral("Preview Audio Pool Item"), QStringLiteral("Ctrl+Left Hold"));
    addShortcutEntry(mouseShortcutsMenu, QStringLiteral("Add Audio Pool Item To Stage Center"), QStringLiteral("Double-Click"));
    addShortcutEntry(mouseShortcutsMenu, QStringLiteral("Open Clip Editor For Node"), QStringLiteral("Double-Click Node"));
    addShortcutEntry(mouseShortcutsMenu, QStringLiteral("Create Loop Range"), QStringLiteral("Drag In Loop Bar"));
    addShortcutEntry(mouseShortcutsMenu, QStringLiteral("Timeline Zoom Horizontal"), QStringLiteral("Mouse Wheel"));

    auto* debugMenu = menuBar()->addMenu(QStringLiteral("&Debug"));
    debugMenu->addAction(m_toggleDebugAction);
    debugMenu->addAction(m_showNativeViewportAction);
}

void MainWindow::buildUi()
{
    setWindowTitle(QStringLiteral("dawg"));
    resize(1400, 900);

    auto* root = new QWidget(this);
    auto* outerLayout = new QHBoxLayout(root);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    m_contentSplitter = new QSplitter(Qt::Horizontal, root);
    m_contentSplitter->setChildrenCollapsible(false);
    m_contentSplitter->setHandleWidth(6);

    m_mainContent = new QWidget(root);
    auto* layout = new QVBoxLayout(m_mainContent);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    m_frameLabel = new QLabel(QStringLiteral("Frame 0  |  0.00 s"), menuBar());
    m_frameLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_frameLabel->setMinimumWidth(180);
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

    m_mainVerticalSplitter = new QSplitter(Qt::Vertical, m_mainContent);
    m_mainVerticalSplitter->setChildrenCollapsible(false);
    m_mainVerticalSplitter->setHandleWidth(6);

    m_canvasPanel = new QFrame(m_mainVerticalSplitter);
    m_canvasPanel->setObjectName(QStringLiteral("canvasPanel"));
    m_canvasPanel->setFrameShape(QFrame::NoFrame);
    auto* canvasPanelLayout = new QVBoxLayout(m_canvasPanel);
    canvasPanelLayout->setContentsMargins(0, 0, 0, 0);
    canvasPanelLayout->setSpacing(0);

    m_canvas = new VideoCanvas(m_canvasPanel);
    m_canvas->setRenderService(m_controller->renderService());
    canvasPanelLayout->addWidget(m_canvas, 1);
    m_nativeViewport = new NativeVideoViewport(nullptr);
    m_nativeViewport->setWindowTitle(QStringLiteral("Native Video Viewport Test"));
    m_nativeViewport->resize(960, 540);
    m_nativeViewport->hide();
    m_nativeViewport->installEventFilter(this);
    m_nativeViewport->setRenderService(nullptr);
    m_nativeViewportWindow = m_nativeViewport;
    connect(this, &QObject::destroyed, m_nativeViewportWindow, &QObject::deleteLater);
    m_detachedVideoWindow = new QWidget(this, Qt::Window);
    m_detachedVideoWindow->setWindowTitle(QStringLiteral("Detached Video"));
    m_detachedVideoWindow->setWindowIcon(windowIcon());
    m_detachedVideoWindow->setAttribute(Qt::WA_DeleteOnClose, false);
    m_detachedVideoWindow->hide();
    m_detachedVideoWindow->installEventFilter(this);
    auto* detachedVideoLayout = new QVBoxLayout(m_detachedVideoWindow);
    detachedVideoLayout->setContentsMargins(0, 0, 0, 0);
    detachedVideoLayout->setSpacing(0);
    m_timelinePanel = new QFrame(m_mainVerticalSplitter);
    m_timelinePanel->setObjectName(QStringLiteral("timelinePanel"));
    m_timelinePanel->setFrameShape(QFrame::NoFrame);
    m_timelinePanel->setMinimumHeight(72);
    auto* timelinePanelLayout = new QVBoxLayout(m_timelinePanel);
    timelinePanelLayout->setContentsMargins(0, 0, 0, 0);
    timelinePanelLayout->setSpacing(0);

    m_timeline = new TimelineView(m_timelinePanel);
    timelinePanelLayout->addWidget(m_timeline, 1);

    m_clipEditorPanel = new QFrame(m_mainVerticalSplitter);
    m_clipEditorPanel->setObjectName(QStringLiteral("clipEditorPanel"));
    m_clipEditorPanel->setVisible(false);
    m_clipEditorPanel->setFrameShape(QFrame::NoFrame);
    m_clipEditorPanel->setMinimumHeight(148);
    auto* clipEditorPanelLayout = new QVBoxLayout(m_clipEditorPanel);
    clipEditorPanelLayout->setContentsMargins(0, 0, 0, 0);
    clipEditorPanelLayout->setSpacing(0);

    m_clipEditorView = new ClipEditorView(m_clipEditorPanel);
    clipEditorPanelLayout->addWidget(m_clipEditorView, 1);

    m_mixPanel = new QFrame(m_mainVerticalSplitter);
    m_mixPanel->setObjectName(QStringLiteral("mixPanel"));
    m_mixPanel->setVisible(false);
    m_mixPanel->setFrameShape(QFrame::NoFrame);
    m_mixPanel->setMinimumHeight(220);
    auto* mixPanelLayout = new QVBoxLayout(m_mixPanel);
    mixPanelLayout->setContentsMargins(0, 0, 0, 0);
    mixPanelLayout->setSpacing(0);

    m_mixView = new MixView(m_mixPanel);
    mixPanelLayout->addWidget(m_mixView, 1);

    m_audioPoolPanel = new QFrame(m_contentSplitter);
    m_audioPoolPanel->setObjectName(QStringLiteral("audioPoolPanel"));
    m_audioPoolPanel->setVisible(false);
    m_audioPoolPanel->setFrameShape(QFrame::NoFrame);
    m_audioPoolPanel->setMinimumWidth(240);
    m_audioPoolPanel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    auto* audioPoolLayout = new QVBoxLayout(m_audioPoolPanel);
    audioPoolLayout->setContentsMargins(0, 0, 0, 0);
    audioPoolLayout->setSpacing(0);

    auto* audioPoolHeader = new QHBoxLayout();
    audioPoolHeader->setContentsMargins(8, 0, 8, 0);
    auto* audioPoolTitle = new QLabel(QStringLiteral("Audio Pool"), m_audioPoolPanel);
    m_audioPoolMenuButton = new QToolButton(m_audioPoolPanel);
    m_audioPoolMenuButton->setCursor(Qt::PointingHandCursor);
    m_audioPoolMenuButton->setText(QStringLiteral("\u2630"));
    m_audioPoolMenuButton->setFixedWidth(28);
    m_audioPoolMenuButton->setToolTip(QStringLiteral("Audio Pool options"));
    m_audioPoolMenuButton->setStyleSheet(QStringLiteral(
        "QToolButton { background: transparent; color: #d7dee8; border: none; border-radius: 4px; padding: 1px 4px; font-size: 8.5pt; }"
        "QToolButton:hover { background: rgba(255, 255, 255, 0.08); }"
        "QToolButton:pressed { background: #111821; }"));
    audioPoolHeader->addWidget(audioPoolTitle);
    audioPoolHeader->addStretch(1);
    audioPoolHeader->addWidget(m_audioPoolMenuButton);
    audioPoolLayout->addLayout(audioPoolHeader);

    m_videoAudioRow = new QWidget(m_audioPoolPanel);
    auto* videoAudioLayout = new QHBoxLayout(m_videoAudioRow);
    videoAudioLayout->setContentsMargins(8, 0, 8, 0);
    videoAudioLayout->setSpacing(6);

    auto* videoAudioIcon = new QLabel(QStringLiteral("\u266B"), m_videoAudioRow);
    videoAudioIcon->setStyleSheet(QStringLiteral("color: #c7d0da; font-size: 9pt;"));

    m_videoAudioLabel = new QLabel(m_videoAudioRow);
    m_videoAudioLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_videoAudioLabel->setStyleSheet(QStringLiteral("color: #c7d0da; font-size: 8.5pt;"));

    m_videoAudioMuteButton = new QToolButton(m_videoAudioRow);
    m_videoAudioMuteButton->setCursor(Qt::PointingHandCursor);
    m_videoAudioMuteButton->setFixedWidth(28);
    m_videoAudioMuteButton->setStyleSheet(QStringLiteral(
        "QToolButton {"
        "  background: transparent;"
        "  color: #d7dee8;"
        "  border: none;"
        "  border-radius: 4px;"
        "  padding: 1px 4px;"
        "  font-size: 8.5pt;"
        "}"
        "QToolButton:hover { background: rgba(255, 255, 255, 0.08); }"
        "QToolButton:pressed { background: #111821; }"));
    connect(m_videoAudioMuteButton, &QToolButton::clicked, this, [this]()
    {
        if (!m_videoAudioMuteButton)
        {
            return;
        }

        QMenu menu(m_videoAudioMuteButton);
        auto menuFont = menu.font();
        menuFont.setPointSizeF(8.5);
        menu.setFont(menuFont);
        menu.setStyleSheet(QStringLiteral(
            "QMenu { background: rgba(8, 10, 13, 204); color: #eef2f6; border: 1px solid #1f2935; border-radius: 8px; padding: 4px 0; font-size: 8.5pt; }"
            "QMenu::item { padding: 6px 14px; }"
            "QMenu::item:selected { background: #1a2028; }"));

        const auto muted = m_controller->isEmbeddedVideoAudioMuted();
        const auto fastPlaybackEnabled = m_controller->isFastPlaybackEnabled();
        auto* toggleMuteAction = menu.addAction(muted ? QStringLiteral("Unmute") : QStringLiteral("Mute"));
        auto* fastPlaybackAction = menu.addAction(QStringLiteral("Fast Playback"));
        fastPlaybackAction->setCheckable(true);
        fastPlaybackAction->setChecked(fastPlaybackEnabled);

        const auto popupWidth = menu.sizeHint().width();
        auto popupPoint = m_videoAudioMuteButton->mapToGlobal(
            QPoint(m_videoAudioMuteButton->width() - popupWidth, m_videoAudioMuteButton->height()));
        if (const auto* screen = m_videoAudioMuteButton->screen())
        {
            const auto screenRect = screen->availableGeometry();
            if (popupPoint.x() + popupWidth > screenRect.right())
            {
                popupPoint.setX(screenRect.right() - popupWidth);
            }
            popupPoint.setX(std::max(screenRect.left(), popupPoint.x()));
        }

        const auto* chosenAction = menu.exec(popupPoint);
        if (chosenAction == toggleMuteAction)
        {
            m_controller->toggleEmbeddedVideoAudioMuted();
        }
        else if (chosenAction == fastPlaybackAction)
        {
            m_controller->setFastPlaybackEnabled(!fastPlaybackEnabled);
        }
    });

    videoAudioLayout->addWidget(videoAudioIcon, 0, Qt::AlignVCenter);
    videoAudioLayout->addWidget(m_videoAudioLabel, 1);
    videoAudioLayout->addWidget(m_videoAudioMuteButton, 0, Qt::AlignVCenter);
    audioPoolLayout->addWidget(m_videoAudioRow);

    auto* audioPoolScroll = new QScrollArea(m_audioPoolPanel);
    audioPoolScroll->setWidgetResizable(true);
    audioPoolScroll->setFrameShape(QFrame::NoFrame);
    audioPoolScroll->setObjectName(QStringLiteral("audioPoolScroll"));
    audioPoolScroll->setContentsMargins(0, 0, 0, 0);
    m_audioPoolListContainer = new QWidget(audioPoolScroll);
    m_audioPoolListContainer->setObjectName(QStringLiteral("audioPoolListContainer"));
    m_audioPoolListLayout = new QVBoxLayout(m_audioPoolListContainer);
    m_audioPoolListLayout->setContentsMargins(0, 0, 0, 0);
    m_audioPoolListLayout->setSpacing(1);
    audioPoolScroll->setWidget(m_audioPoolListContainer);
    audioPoolLayout->addWidget(audioPoolScroll, 1);
    connect(m_audioPoolMenuButton, &QToolButton::clicked, this, [this]()
    {
        if (!m_audioPoolMenuButton)
        {
            return;
        }

        QMenu menu(m_audioPoolMenuButton);
        auto menuFont = menu.font();
        menuFont.setPointSizeF(8.5);
        menu.setFont(menuFont);
        menu.setStyleSheet(QStringLiteral(
            "QMenu { background: rgba(8, 10, 13, 204); color: #eef2f6; border: 1px solid #1f2935; border-radius: 8px; padding: 4px 0; font-size: 8.5pt; }"
            "QMenu::item { padding: 6px 14px; }"
            "QMenu::item:selected { background: #1a2028; }"));

        auto* importAction = menu.addAction(QStringLiteral("Import Audio... (Ctrl+Shift+I)"));
        menu.addSeparator();
        auto* showLengthAction = menu.addAction(QStringLiteral("Show Length"));
        showLengthAction->setCheckable(true);
        showLengthAction->setChecked(m_audioPoolShowLength);
        auto* showSizeAction = menu.addAction(QStringLiteral("Show Size"));
        showSizeAction->setCheckable(true);
        showSizeAction->setChecked(m_audioPoolShowSize);
        menu.addSeparator();
        auto* closeAction = menu.addAction(QStringLiteral("Close (P)"));

        const auto popupWidth = menu.sizeHint().width();
        auto popupPoint = m_audioPoolMenuButton->mapToGlobal(
            QPoint(m_audioPoolMenuButton->width() - popupWidth, m_audioPoolMenuButton->height()));
        if (const auto* screen = m_audioPoolMenuButton->screen())
        {
            const auto screenRect = screen->availableGeometry();
            if (popupPoint.x() + popupWidth > screenRect.right())
            {
                popupPoint.setX(screenRect.right() - popupWidth);
            }
            popupPoint.setX(std::max(screenRect.left(), popupPoint.x()));
        }

        const auto* chosenAction = menu.exec(popupPoint);
        if (chosenAction == importAction)
        {
            importAudioToPool();
        }
        else if (chosenAction == showLengthAction)
        {
            m_audioPoolShowLength = showLengthAction->isChecked();
            refreshAudioPool();
            if (!m_projectStateChangeInProgress && hasOpenProject())
            {
                setProjectDirty(true);
            }
        }
        else if (chosenAction == showSizeAction)
        {
            m_audioPoolShowSize = showSizeAction->isChecked();
            refreshAudioPool();
            if (!m_projectStateChangeInProgress && hasOpenProject())
            {
                setProjectDirty(true);
            }
        }
        else if (chosenAction == closeAction)
        {
            updateAudioPoolVisibility(false);
            showStatus(QStringLiteral("Audio Pool hidden."));
        }
    });

    m_mainVerticalSplitter->addWidget(m_canvasPanel);
    m_mainVerticalSplitter->addWidget(m_timelinePanel);
    m_mainVerticalSplitter->addWidget(m_clipEditorPanel);
    m_mainVerticalSplitter->addWidget(m_mixPanel);
    m_mainVerticalSplitter->setStretchFactor(0, 1);
    m_mainVerticalSplitter->setStretchFactor(1, 0);
    m_mainVerticalSplitter->setStretchFactor(2, 0);
    m_mainVerticalSplitter->setStretchFactor(3, 0);
    m_mainVerticalSplitter->setSizes({700, m_timelinePreferredHeight, 0, 0});
    connect(m_mainVerticalSplitter, &QSplitter::splitterMoved, this, [this]()
    {
        if (m_timelinePanel && m_timelinePanel->isVisible())
        {
            m_timelinePreferredHeight = std::max(96, m_timelinePanel->height());
        }
        if (m_clipEditorPanel && m_clipEditorPanel->isVisible())
        {
            m_clipEditorPreferredHeight = std::max(148, m_clipEditorPanel->height());
        }
        if (m_mixPanel && m_mixPanel->isVisible())
        {
            m_mixPreferredHeight = std::max(132, m_mixPanel->height());
        }
        if (!m_projectStateChangeInProgress && hasOpenProject())
        {
            setProjectDirty(true);
        }
    });

    layout->addWidget(m_mainVerticalSplitter, 1);

    m_contentSplitter->addWidget(m_mainContent);
    m_contentSplitter->addWidget(m_audioPoolPanel);
    m_contentSplitter->setStretchFactor(0, 1);
    m_contentSplitter->setStretchFactor(1, 0);
    m_contentSplitter->setSizes({1040, m_audioPoolPreferredWidth});
    connect(m_contentSplitter, &QSplitter::splitterMoved, this, [this]()
    {
        if (m_audioPoolPanel && m_audioPoolPanel->isVisible())
        {
            m_audioPoolPreferredWidth = std::max(240, m_audioPoolPanel->width());
        }
        if (!m_projectStateChangeInProgress && hasOpenProject())
        {
            setProjectDirty(true);
        }
    });

    outerLayout->addWidget(m_contentSplitter, 1);

    setCentralWidget(root);
    menuBar()->setCornerWidget(m_frameLabel, Qt::TopRightCorner);
    statusBar()->hide();

    auto* debugOverlay = new DebugOverlayWindow();
    m_debugOverlay = debugOverlay;
    connect(this, &QObject::destroyed, debugOverlay, &QObject::deleteLater);
    m_debugOverlay->move(mapToGlobal(QPoint(16, 48)));
    m_debugOverlay->setVisible(m_debugVisible);
    connect(debugOverlay, &DebugOverlayWindow::closeRequested, this, [this]()
    {
        updateDebugVisibility(false);
        showStatus(QStringLiteral("Debug window hidden."));
    });

    m_statusToast = new QLabel(root);
    m_statusToast->setObjectName(QStringLiteral("statusToast"));
    m_statusToast->setWordWrap(true);
    m_statusToast->hide();

    auto* trackGainPopup = new TrackGainPopup(this);
    trackGainPopup->hide();
    m_trackGainPopup = trackGainPopup;
    m_trackGainPopupSlider = trackGainPopup->slider();
    m_trackGainPopupValueLabel = trackGainPopup->valueLabel();
    connect(m_trackGainPopupSlider, &QSlider::valueChanged, this, [this](const int sliderValue)
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

    m_canvasTipsOverlay = new QLabel(m_canvas);
    m_canvasTipsOverlay->setObjectName(QStringLiteral("canvasTipsOverlay"));
    m_canvasTipsOverlay->setWordWrap(true);
    m_canvasTipsOverlay->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_canvasTipsOverlay->setMaximumWidth(300);
    m_canvasTipsOverlay->hide();
    updateOverlayPositions();

    qApp->setStyleSheet(R"(
        QMainWindow {
            background: #0d1014;
        }
        QDialog, QMessageBox, QInputDialog, QFileDialog {
            background: #0f141b;
        }
        QSplitter::handle {
            background: #050608;
        }
        QSplitter::handle:hover {
            background: #0c1015;
        }
        QWidget {
            color: #f3f5f7;
            font-family: "Segoe UI";
            font-size: 10.5pt;
        }
        QMenuBar {
            background: #121720;
            color: #f3f5f7;
        }
        QMenuBar::item {
            background: transparent;
            padding: 6px 10px;
        }
        QMenuBar::item:disabled {
            color: #6b7683;
            background: transparent;
        }
        QMenuBar::item:selected {
            background: #223146;
        }
        QMenuBar QLabel {
            color: #9fb4c8;
            font-size: 9pt;
            padding-right: 4px;
        }
        QMenu {
            background: rgba(18, 23, 32, 204);
            color: #f3f5f7;
            border: 1px solid #324155;
            border-radius: 8px;
            font-size: 8.5pt;
        }
        QMenu::item:disabled {
            color: #687380;
            background: transparent;
        }
        QMenu::item:selected {
            background: #223146;
        }
        QLabel {
            color: #d8dde4;
        }
        QMessageBox QLabel, QInputDialog QLabel, QFileDialog QLabel {
            color: #d8dde4;
            background: transparent;
        }
        QDialogButtonBox {
            background: transparent;
        }
        QDialogButtonBox QPushButton, QMessageBox QPushButton, QInputDialog QPushButton, QFileDialog QPushButton {
            background: #1a232d;
            color: #eef2f6;
            border: 1px solid #324155;
            border-radius: 6px;
            padding: 5px 12px;
            min-height: 26px;
        }
        QDialogButtonBox QPushButton:disabled, QMessageBox QPushButton:disabled, QInputDialog QPushButton:disabled, QFileDialog QPushButton:disabled {
            background: #11161d;
            color: #728090;
            border: 1px solid #26313e;
        }
        QDialogButtonBox QPushButton:hover, QMessageBox QPushButton:hover, QInputDialog QPushButton:hover, QFileDialog QPushButton:hover {
            background: #223146;
        }
        QLineEdit, QFileDialog QLineEdit, QInputDialog QLineEdit {
            background: #0b0f14;
            color: #eef2f6;
            border: 1px solid #324155;
            border-radius: 6px;
            padding: 4px 8px;
        }
        QFileDialog QListView, QFileDialog QTreeView, QFileDialog QComboBox, QFileDialog QSplitter, QFileDialog QWidget {
            background: #0f141b;
        }
        QStatusBar {
            background: #121720;
        }
        QStatusBar QLabel {
            color: #b8c6d4;
            font-size: 9pt;
            padding-right: 10px;
        }
        QLabel#statusToast {
            color: #f2f5f8;
            font-size: 9pt;
            background: rgba(11, 15, 20, 210);
            border: 1px solid #324155;
            border-radius: 8px;
            padding: 8px 12px;
        }
        QLabel#canvasTipsOverlay {
            color: #dce4ec;
            font-size: 9pt;
            background: rgba(11, 15, 20, 156);
            border: 1px solid rgba(80, 98, 123, 180);
            border-radius: 8px;
            padding: 8px 10px;
        }
        QFrame {
            background: #0f141b;
        }
        QFrame#audioPoolPanel {
            background: #07090c;
        }
        QFrame#mixPanel {
            background: #080b10;
            border-top: 1px solid #131a23;
        }
        QScrollArea#audioPoolScroll {
            background: #07090c;
            border: none;
        }
        QWidget#audioPoolListContainer {
            background: #07090c;
        }
        QFrame#debugOverlay {
            background: transparent;
            border: none;
        }
        QWidget#debugOverlayTitleBar {
            background: rgba(17, 24, 33, 128);
            border: 1px solid #253142;
            border-radius: 8px;
        }
        QLabel#debugOverlayTitle {
            color: #f3f5f7;
            font-weight: 600;
        }
        QLabel#debugOverlayText {
            color: #d8dde4;
            font-size: 9pt;
            padding: 10px;
            background: rgba(11, 15, 20, 128);
            border: 1px solid #253142;
            border-radius: 8px;
        }
        QPushButton#debugOverlayCloseButton {
            background: #18202b;
            color: #ecf1f6;
            border: 1px solid #324155;
            border-radius: 4px;
            padding: 0px;
        }
        QPushButton#debugOverlayCloseButton:hover {
            background: #223146;
        }
        QPushButton {
            background: #18202b;
            color: #ecf1f6;
            border: 1px solid #324155;
            border-radius: 6px;
            padding: 4px 10px;
        }
        QPushButton:disabled {
            background: #10151c;
            color: #728090;
            border: 1px solid #24303d;
        }
        QPushButton:hover {
            background: #223146;
        }
        QToolButton:disabled {
            color: #6e7a88;
            background: rgba(255, 255, 255, 0.02);
            border-color: rgba(255, 255, 255, 0.04);
        }
        QLineEdit {
            background: #0a0d12;
            color: #eef2f6;
            border: 1px solid #324155;
            border-radius: 6px;
            padding: 4px 8px;
            selection-background-color: #29415d;
        }
        QLineEdit:focus {
            border: 1px solid #4a698c;
        }
    )");
}

void MainWindow::syncMotionTrackingUi(const bool enabled)
{
    Q_UNUSED(enabled);
}

void MainWindow::tryOpenLocalDevVideo()
{
#ifdef QT_DEBUG
    const QDir appDir{QCoreApplication::applicationDirPath()};
    const QStringList candidateRoots{
        QDir::cleanPath(appDir.filePath("../../src")),
        QDir::currentPath()
    };

    for (const auto& rootPath : candidateRoots)
    {
        QDir devDir{QDir(rootPath).filePath(".dev")};
        if (!devDir.exists())
        {
            continue;
        }

        const QStringList preferredVideoNames{
            QStringLiteral("test-video-2.mp4"),
            QStringLiteral("test-video-2.MP4"),
            QStringLiteral("test-video.mov"),
            QStringLiteral("test-video.MOV")
        };

        QString selectedVideoPath;
        for (const auto& fileName : preferredVideoNames)
        {
            const auto candidatePath = devDir.filePath(fileName);
            if (QFileInfo::exists(candidatePath))
            {
                selectedVideoPath = candidatePath;
                break;
            }
        }

        if (selectedVideoPath.isEmpty())
        {
            continue;
        }

        if (m_controller->openVideo(selectedVideoPath))
        {
            populateAudioPoolFromLocalDevDirectory();
        }
        return;
    }
#endif
}

void MainWindow::populateAudioPoolFromLocalDevDirectory()
{
#ifdef QT_DEBUG
    const QDir appDir{QCoreApplication::applicationDirPath()};
    const QStringList candidateRoots{
        QDir::cleanPath(appDir.filePath("../../src")),
        QDir::currentPath()
    };

    for (const auto& rootPath : candidateRoots)
    {
        QDir devDir{QDir(rootPath).filePath(".dev")};
        if (!devDir.exists())
        {
            continue;
        }

        const auto matches = devDir.entryInfoList(
            QStringList{
                QStringLiteral("*.wav"),
                QStringLiteral("*.mp3"),
                QStringLiteral("*.flac"),
                QStringLiteral("*.ogg"),
                QStringLiteral("*.m4a"),
                QStringLiteral("*.aac"),
                QStringLiteral("*.aif"),
                QStringLiteral("*.aiff"),
                QStringLiteral("*.WAV"),
                QStringLiteral("*.MP3"),
                QStringLiteral("*.FLAC"),
                QStringLiteral("*.OGG"),
                QStringLiteral("*.M4A"),
                QStringLiteral("*.AAC"),
                QStringLiteral("*.AIF"),
                QStringLiteral("*.AIFF")
            },
            QDir::Files,
            QDir::Name);

        for (const auto& match : matches)
        {
            m_controller->importAudioToPool(match.absoluteFilePath());
        }
        return;
    }
#endif
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
