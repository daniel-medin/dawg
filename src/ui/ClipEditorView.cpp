#include "ui/ClipEditorView.h"

#include <algorithm>

#include <QDebug>
#include <QQmlContext>
#include <QQuickView>
#include <QVBoxLayout>

#include "ui/ClipEditorQuickController.h"
#include "ui/ClipWaveformQuickItem.h"

namespace
{
QUrl clipEditorSceneUrl()
{
    return QUrl(QStringLiteral("qrc:/qml/ClipEditorScene.qml"));
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
}

ClipEditorView::ClipEditorView(QWidget* parent)
    : QWidget(parent)
    , m_quickView(new QQuickView())
    , m_quickContainer(nullptr)
    , m_controller(new ClipEditorQuickController(this))
{
    ensureClipEditorQuickTypesRegistered();

    setFocusPolicy(Qt::StrongFocus);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_quickView->setColor(Qt::transparent);
    m_quickView->setResizeMode(QQuickView::SizeRootObjectToView);
    m_quickView->rootContext()->setContextProperty(QStringLiteral("clipEditorController"), m_controller);
    connect(m_quickView, &QQuickView::statusChanged, this, [this]()
    {
        handleStatusChanged();
    });

    m_quickContainer = QWidget::createWindowContainer(m_quickView, this);
    m_quickContainer->setFocusPolicy(Qt::StrongFocus);
    m_quickContainer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    layout->addWidget(m_quickContainer, 1);

    connect(m_controller, &ClipEditorQuickController::gainChanged, this, &ClipEditorView::gainChanged);
    connect(m_controller, &ClipEditorQuickController::loopSoundChanged, this, &ClipEditorView::loopSoundChanged);
    connect(m_controller, &ClipEditorQuickController::attachAudioRequested, this, &ClipEditorView::attachAudioRequested);

    m_quickView->setSource(clipEditorSceneUrl());
}

void ClipEditorView::setState(const std::optional<ClipEditorState>& state)
{
    m_state = state;
    if (m_controller)
    {
        m_controller->setState(state);
    }
    syncWaveformItem();
}

void ClipEditorView::handleStatusChanged()
{
    if (!m_quickView)
    {
        return;
    }

    if (m_quickView->status() == QQuickView::Error)
    {
        for (const auto& error : m_quickView->errors())
        {
            qWarning() << "Clip editor QML error:" << error.toString();
        }
        return;
    }

    if (m_quickView->status() != QQuickView::Ready)
    {
        return;
    }

    auto* root = m_quickView->rootObject();
    if (!root)
    {
        return;
    }

    if (!m_waveformItem)
    {
        m_waveformItem = root->findChild<ClipWaveformQuickItem*>(QStringLiteral("clipWaveform"));
        if (m_waveformItem)
        {
            connect(m_waveformItem, &ClipWaveformQuickItem::clipRangeChanged, this, &ClipEditorView::clipRangeChanged);
            connect(m_waveformItem, &ClipWaveformQuickItem::playheadChanged, this, &ClipEditorView::playheadChanged);
        }
    }

    syncWaveformItem();
}

void ClipEditorView::syncWaveformItem()
{
    if (!m_waveformItem)
    {
        return;
    }

    m_waveformItem->setState(m_state);
}
