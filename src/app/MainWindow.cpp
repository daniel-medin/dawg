#include "app/MainWindow.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>

#include <QApplication>
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
#include <QRegularExpression>
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
#include "app/MainWindowActions.h"
#include "app/ProjectWindowController.h"
#include "app/PanelLayoutController.h"
#include "app/DebugUiController.h"
#include "app/MediaImportController.h"
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
    m_actionsController = std::make_unique<MainWindowActions>(*this);
    m_projectWindowController = std::make_unique<ProjectWindowController>(*this);
    m_panelLayoutController = std::make_unique<PanelLayoutController>(*this);
    m_debugUiController = std::make_unique<DebugUiController>(*this);
    m_mediaImportController = std::make_unique<MediaImportController>(*this);
    buildMenus();
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
        m_timeline->setSeekOnClickEnabled(enabled || !m_controller->isPlaying());
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

MainWindow::~MainWindow() = default;

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

void MainWindow::applyFileDialogChrome(QFileDialog& dialog) const
{
    m_mediaImportController->applyFileDialogChrome(dialog);
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
    if (m_timeline && m_timelineClickSeeksAction)
    {
        m_timeline->setSeekOnClickEnabled(m_timelineClickSeeksAction->isChecked() || !playing);
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
    m_actionsController->updateEditActionState();
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
    m_actionsController->buildMenus();
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
