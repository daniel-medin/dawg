#include "ui/ClipEditorQuickController.h"

#include <algorithm>

#include <QFileInfo>

namespace
{
QString formatTimeMs(const int timeMs)
{
    const auto safeMs = std::max(0, timeMs);
    const auto totalSeconds = safeMs / 1000;
    const auto minutes = totalSeconds / 60;
    const auto seconds = totalSeconds % 60;
    const auto milliseconds = safeMs % 1000;
    return QStringLiteral("%1:%2.%3")
        .arg(minutes)
        .arg(seconds, 2, 10, QChar{'0'})
        .arg(milliseconds / 10, 2, 10, QChar{'0'});
}
}

ClipEditorQuickController::ClipEditorQuickController(QObject* parent)
    : QObject(parent)
{
}

void ClipEditorQuickController::setState(const std::optional<ClipEditorState>& state)
{
    m_state = state;
    emit stateChanged();
}

QString ClipEditorQuickController::titleText() const
{
    if (!m_state.has_value() || m_state->label.isEmpty())
    {
        return QStringLiteral("Clip Editor");
    }

    return m_state->label;
}

QString ClipEditorQuickController::sourceText() const
{
    if (!showInfoBar())
    {
        return {};
    }

    return QStringLiteral("Source  %1").arg(QFileInfo(m_state->assetPath).fileName());
}

QString ClipEditorQuickController::rangeText() const
{
    if (!showInfoBar())
    {
        return {};
    }

    return QStringLiteral("In/Out  %1 - %2")
        .arg(formatTimeMs(m_state->clipStartMs))
        .arg(formatTimeMs(m_state->clipEndMs));
}

QString ClipEditorQuickController::durationText() const
{
    if (!showInfoBar())
    {
        return {};
    }

    return QStringLiteral("Clip  %1")
        .arg(formatTimeMs(std::max(0, m_state->clipEndMs - m_state->clipStartMs)));
}

QString ClipEditorQuickController::positionText() const
{
    if (!showInfoBar())
    {
        return {};
    }

    return m_state->playheadMs.has_value()
        ? QStringLiteral("Playhead  %1").arg(formatTimeMs(*m_state->playheadMs))
        : QStringLiteral("Playhead  --");
}

QString ClipEditorQuickController::emptyTitleText() const
{
    if (!m_state.has_value())
    {
        return QStringLiteral("Select a node");
    }

    if (!m_state->hasAttachedAudio)
    {
        return m_state->label.isEmpty() ? QStringLiteral("Node") : m_state->label;
    }

    return QStringLiteral("Select a node");
}

QString ClipEditorQuickController::emptyBodyText() const
{
    if (!m_state.has_value())
    {
        return QStringLiteral("Select a node with attached audio to edit its clip.");
    }

    if (!m_state->hasAttachedAudio)
    {
        return QStringLiteral("No audio attached");
    }

    return {};
}

QString ClipEditorQuickController::emptyActionText() const
{
    return showEmptyAction() ? QStringLiteral("Attach Audio...") : QString{};
}

bool ClipEditorQuickController::showInfoBar() const
{
    return m_state.has_value() && m_state->hasAttachedAudio;
}

bool ClipEditorQuickController::showEditorContent() const
{
    return m_state.has_value() && m_state->hasAttachedAudio;
}

bool ClipEditorQuickController::showEmptyState() const
{
    return !showEditorContent();
}

bool ClipEditorQuickController::showLoopButton() const
{
    return showEditorContent();
}

bool ClipEditorQuickController::showEmptyAction() const
{
    return m_state.has_value() && !m_state->hasAttachedAudio;
}

bool ClipEditorQuickController::loopEnabled() const
{
    return m_state.has_value() && m_state->loopEnabled;
}

float ClipEditorQuickController::gainDb() const
{
    return showEditorContent() ? m_state->gainDb : 0.0F;
}

float ClipEditorQuickController::meterLevel() const
{
    return showEditorContent() ? m_state->level : 0.0F;
}

void ClipEditorQuickController::handleGainDragged(const double gainDb)
{
    emit gainChanged(static_cast<float>(gainDb));
}

void ClipEditorQuickController::handleLoopToggled(const bool enabled)
{
    emit loopSoundChanged(enabled);
}

void ClipEditorQuickController::requestAttachAudio()
{
    emit attachAudioRequested();
}
