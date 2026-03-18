#include "app/ActionRegistry.h"

#include <QDir>
#include <QFileInfo>

#include "app/MainWindow.h"

namespace
{
QString displayText(const QString& rawText)
{
    QString text = rawText;
    text.replace(QStringLiteral("&&"), QStringLiteral("\x1"));
    text.remove(QLatin1Char('&'));
    text.replace(QStringLiteral("\x1"), QStringLiteral("&"));
    return text;
}
}

ActionEntry::ActionEntry(const QString& id, QAction* action, QObject* parent)
    : QObject(parent)
    , m_id(id)
    , m_action(action)
    , m_enabled(action != nullptr)
{
    if (m_action)
    {
        connect(m_action, &QAction::changed, this, &ActionEntry::changed);
        connect(m_action, &QObject::destroyed, this, [this]()
        {
            m_action = nullptr;
            emit changed();
        });
    }
}

ActionEntry::ActionEntry(
    const QString& id,
    const QString& text,
    const std::function<void()>& trigger,
    const bool enabled,
    QObject* parent)
    : QObject(parent)
    , m_id(id)
    , m_text(text)
    , m_enabled(enabled)
    , m_trigger(trigger)
{
}

ActionEntry::ActionEntry(QObject* parent)
    : QObject(parent)
    , m_separator(true)
{
}

QString ActionEntry::id() const
{
    return m_id;
}

QString ActionEntry::text() const
{
    if (m_action)
    {
        return displayText(m_action->text());
    }

    return m_text;
}

bool ActionEntry::enabled() const
{
    if (m_action)
    {
        return m_action->isEnabled();
    }

    return m_enabled;
}

bool ActionEntry::checkable() const
{
    return m_action && m_action->isCheckable();
}

bool ActionEntry::checked() const
{
    return m_action && m_action->isChecked();
}

QString ActionEntry::shortcut() const
{
    return m_action ? m_action->shortcut().toString(QKeySequence::NativeText) : QString{};
}

bool ActionEntry::separator() const
{
    return m_separator;
}

void ActionEntry::trigger()
{
    if (m_action && m_action->isEnabled())
    {
        m_action->trigger();
        return;
    }

    if (m_trigger && m_enabled)
    {
        m_trigger();
    }
}

ActionMenu::ActionMenu(const QString& title, QObject* parent)
    : QObject(parent)
    , m_title(title)
{
}

void ActionMenu::addEntry(ActionEntry* entry)
{
    if (!entry)
    {
        return;
    }

    entry->setParent(this);
    m_items.push_back(entry);
}

QString ActionMenu::title() const
{
    return m_title;
}

QObjectList ActionMenu::items() const
{
    return m_items;
}

ActionRegistry::ActionRegistry(MainWindow& window, QObject* parent)
    : QObject(parent)
    , m_window(window)
{
    rebuild();
}

QObjectList ActionRegistry::menus() const
{
    return m_menus;
}

void ActionRegistry::rebuild()
{
    qDeleteAll(m_menus);
    m_menus.clear();

    auto* fileMenu = addMenu(QStringLiteral("File"));
    addAction(fileMenu, QStringLiteral("newProject"), m_window.m_newProjectAction);
    addAction(fileMenu, QStringLiteral("openProject"), m_window.m_openProjectAction);
    addSeparator(fileMenu);
    const auto storedPaths = m_window.recentProjectPaths();
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
        m_window.storeRecentProjectPaths(existingPaths);
    }
    if (existingPaths.isEmpty())
    {
        addCallback(fileMenu, QStringLiteral("openRecent.empty"), QStringLiteral("No Recent Projects"), {}, false);
    }
    else
    {
        for (const auto& projectPath : existingPaths)
        {
            const QFileInfo projectInfo(projectPath);
            const auto displayName = projectInfo.completeBaseName().isEmpty()
                ? projectInfo.fileName()
                : projectInfo.completeBaseName();
            const auto parentPath = QDir::toNativeSeparators(projectInfo.absolutePath());
            addCallback(
                fileMenu,
                QStringLiteral("openRecent.%1").arg(projectPath),
                QStringLiteral("Open Recent: %1  -  %2").arg(displayName, parentPath),
                [&window = m_window, projectPath]()
                {
                    static_cast<void>(window.openProjectFileWithPrompt(projectPath, QStringLiteral("open another project")));
                },
                true);
        }
    }
    addSeparator(fileMenu);
    addAction(fileMenu, QStringLiteral("saveProject"), m_window.m_saveProjectAction);
    addAction(fileMenu, QStringLiteral("saveProjectAs"), m_window.m_saveProjectAsAction);
    addSeparator(fileMenu);
    addAction(fileMenu, QStringLiteral("openVideo"), m_window.m_openAction);
    addAction(fileMenu, QStringLiteral("importSound"), m_window.m_importSoundAction);
    addSeparator(fileMenu);
    addAction(fileMenu, QStringLiteral("quit"), m_window.m_quitAction);

    auto* editMenu = addMenu(QStringLiteral("Edit"));
    addAction(editMenu, QStringLiteral("copy"), m_window.m_copyAction);
    addAction(editMenu, QStringLiteral("paste"), m_window.m_pasteAction);
    addAction(editMenu, QStringLiteral("cut"), m_window.m_cutAction);
    addAction(editMenu, QStringLiteral("undo"), m_window.m_undoAction);
    addAction(editMenu, QStringLiteral("redo"), m_window.m_redoAction);
    addSeparator(editMenu);
    addAction(editMenu, QStringLiteral("insertionFollowsPlayback"), m_window.m_insertionFollowsPlaybackAction);
    addAction(editMenu, QStringLiteral("selectAll"), m_window.m_selectAllAction);
    addAction(editMenu, QStringLiteral("unselectAll"), m_window.m_unselectAllAction);

    auto* nodeMenu = addMenu(QStringLiteral("Node"));
    addAction(nodeMenu, QStringLiteral("selectNextNode"), m_window.m_selectNextNodeAction);
    addSeparator(nodeMenu);
    addAction(nodeMenu, QStringLiteral("moveNodeUp"), m_window.m_moveNodeUpAction);
    addAction(nodeMenu, QStringLiteral("moveNodeDown"), m_window.m_moveNodeDownAction);
    addAction(nodeMenu, QStringLiteral("moveNodeLeft"), m_window.m_moveNodeLeftAction);
    addAction(nodeMenu, QStringLiteral("moveNodeRight"), m_window.m_moveNodeRightAction);
    addSeparator(nodeMenu);
    addAction(nodeMenu, QStringLiteral("setNodeStart"), m_window.m_setNodeStartAction);
    addAction(nodeMenu, QStringLiteral("setNodeEnd"), m_window.m_setNodeEndAction);
    addAction(nodeMenu, QStringLiteral("trimNode"), m_window.m_trimNodeAction);
    addSeparator(nodeMenu);
    addAction(nodeMenu, QStringLiteral("deleteNode"), m_window.m_deleteNodeAction);
    addAction(nodeMenu, QStringLiteral("clearAll"), m_window.m_clearAllAction);

    auto* audioMenu = addMenu(QStringLiteral("Audio"));
    addAction(audioMenu, QStringLiteral("importSoundAudio"), m_window.m_importSoundAction);
    addAction(audioMenu, QStringLiteral("loopSound"), m_window.m_loopSoundAction);
    addAction(audioMenu, QStringLiteral("autoPan"), m_window.m_autoPanAction);
    addAction(audioMenu, QStringLiteral("audioPool"), m_window.m_audioPoolAction);

    auto* timelineMenu = addMenu(QStringLiteral("Timeline"));
    addAction(timelineMenu, QStringLiteral("goToStart"), m_window.m_goToStartAction);
    addAction(timelineMenu, QStringLiteral("play"), m_window.m_playAction);
    addAction(timelineMenu, QStringLiteral("stepForward"), m_window.m_stepForwardAction);
    addAction(timelineMenu, QStringLiteral("stepBack"), m_window.m_stepBackAction);
    addAction(timelineMenu, QStringLiteral("stepFastForward"), m_window.m_stepFastForwardAction);
    addAction(timelineMenu, QStringLiteral("stepFastBack"), m_window.m_stepFastBackAction);
    addSeparator(timelineMenu);
    addAction(timelineMenu, QStringLiteral("setLoopStart"), m_window.m_setLoopStartAction);
    addAction(timelineMenu, QStringLiteral("setLoopEnd"), m_window.m_setLoopEndAction);
    addAction(timelineMenu, QStringLiteral("clearLoop"), m_window.m_clearLoopRangeAction);
    addSeparator(timelineMenu);
    addAction(timelineMenu, QStringLiteral("timelineThumbnails"), m_window.m_showTimelineThumbnailsAction);
    addAction(timelineMenu, QStringLiteral("timelineClickSeeks"), m_window.m_timelineClickSeeksAction);

    auto* viewMenu = addMenu(QStringLiteral("View"));
    addAction(viewMenu, QStringLiteral("detachVideo"), m_window.m_detachVideoAction);
    addSeparator(viewMenu);
    addAction(viewMenu, QStringLiteral("showTimeline"), m_window.m_showTimelineAction);
    addAction(viewMenu, QStringLiteral("showClipEditor"), m_window.m_showClipEditorAction);
    addAction(viewMenu, QStringLiteral("showMix"), m_window.m_showMixAction);
    addAction(viewMenu, QStringLiteral("showAudioPool"), m_window.m_audioPoolAction);
    addSeparator(viewMenu);
    addAction(viewMenu, QStringLiteral("toggleNodeName"), m_window.m_toggleNodeNameAction);
    addAction(viewMenu, QStringLiteral("showAllNodeNames"), m_window.m_showAllNodeNamesAction);

    auto* motionMenu = addMenu(QStringLiteral("Motion"));
    addAction(motionMenu, QStringLiteral("motionTracking"), m_window.m_motionTrackingAction);

    auto* debugMenu = addMenu(QStringLiteral("Debug"));
    addAction(debugMenu, QStringLiteral("toggleDebug"), m_window.m_toggleDebugAction);
    addAction(debugMenu, QStringLiteral("showNativeViewport"), m_window.m_showNativeViewportAction);

    emit menusChanged();
}

ActionMenu* ActionRegistry::addMenu(const QString& title)
{
    auto* menu = new ActionMenu(title, this);
    m_menus.push_back(menu);
    return menu;
}

void ActionRegistry::addAction(ActionMenu* menu, const QString& id, QAction* action)
{
    if (!menu || !action)
    {
        return;
    }

    menu->addEntry(new ActionEntry(id, action, menu));
}

void ActionRegistry::addCallback(
    ActionMenu* menu,
    const QString& id,
    const QString& text,
    const std::function<void()>& trigger,
    const bool enabled)
{
    if (!menu)
    {
        return;
    }

    menu->addEntry(new ActionEntry(id, text, trigger, enabled, menu));
}

void ActionRegistry::addSeparator(ActionMenu* menu)
{
    if (!menu)
    {
        return;
    }

    menu->addEntry(new ActionEntry(menu));
}
