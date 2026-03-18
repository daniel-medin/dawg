#include "app/ShellOverlayController.h"

#include <QtGlobal>

namespace
{
constexpr int kTrackGainPopupMinValue = -1000;
constexpr int kTrackGainPopupMaxValue = 120;
}

ShellOverlayController::ShellOverlayController(QObject* parent)
    : QObject(parent)
{
}

bool ShellOverlayController::visible() const
{
    return m_statusVisible || m_canvasTipsVisible || m_trackGainPopupVisible;
}

bool ShellOverlayController::statusVisible() const
{
    return m_statusVisible;
}

QString ShellOverlayController::statusMessage() const
{
    return m_statusMessage;
}

int ShellOverlayController::statusMaxWidth() const
{
    return m_statusMaxWidth;
}

bool ShellOverlayController::canvasTipsVisible() const
{
    return m_canvasTipsVisible;
}

QString ShellOverlayController::canvasTipsMessage() const
{
    return m_canvasTipsMessage;
}

int ShellOverlayController::canvasTipsX() const
{
    return m_canvasTipsX;
}

int ShellOverlayController::canvasTipsY() const
{
    return m_canvasTipsY;
}

int ShellOverlayController::canvasTipsMaxWidth() const
{
    return m_canvasTipsMaxWidth;
}

bool ShellOverlayController::trackGainPopupVisible() const
{
    return m_trackGainPopupVisible;
}

QString ShellOverlayController::trackGainPopupValue() const
{
    return m_trackGainPopupValue;
}

int ShellOverlayController::trackGainPopupAnchorX() const
{
    return m_trackGainPopupAnchorX;
}

int ShellOverlayController::trackGainPopupAnchorY() const
{
    return m_trackGainPopupAnchorY;
}

int ShellOverlayController::trackGainPopupSliderValue() const
{
    return m_trackGainPopupSliderValue;
}

int ShellOverlayController::trackGainPopupMinimum() const
{
    return kTrackGainPopupMinValue;
}

int ShellOverlayController::trackGainPopupMaximum() const
{
    return kTrackGainPopupMaxValue;
}

void ShellOverlayController::showStatus(const QString& message)
{
    const bool changed = !m_statusVisible || m_statusMessage != message;
    m_statusVisible = true;
    m_statusMessage = message;
    emitIfChanged(changed);
}

void ShellOverlayController::hideStatus()
{
    const bool changed = m_statusVisible;
    m_statusVisible = false;
    emitIfChanged(changed);
}

void ShellOverlayController::setStatusMaxWidth(const int width)
{
    const int clampedWidth = qMax(160, width);
    const bool changed = m_statusMaxWidth != clampedWidth;
    m_statusMaxWidth = clampedWidth;
    emitIfChanged(changed);
}

void ShellOverlayController::showCanvasTips(const QString& message)
{
    const bool changed = !m_canvasTipsVisible || m_canvasTipsMessage != message;
    m_canvasTipsVisible = true;
    m_canvasTipsMessage = message;
    emitIfChanged(changed);
}

void ShellOverlayController::hideCanvasTips()
{
    const bool changed = m_canvasTipsVisible;
    m_canvasTipsVisible = false;
    emitIfChanged(changed);
}

void ShellOverlayController::setCanvasTipsPosition(const int x, const int y)
{
    const bool changed = m_canvasTipsX != x || m_canvasTipsY != y;
    m_canvasTipsX = x;
    m_canvasTipsY = y;
    emitIfChanged(changed);
}

void ShellOverlayController::setCanvasTipsMaxWidth(const int width)
{
    const int clampedWidth = qMax(180, width);
    const bool changed = m_canvasTipsMaxWidth != clampedWidth;
    m_canvasTipsMaxWidth = clampedWidth;
    emitIfChanged(changed);
}

void ShellOverlayController::showTrackGainPopup(
    const QString& valueText,
    const int anchorX,
    const int anchorY,
    const int sliderValue)
{
    const int clampedSliderValue = qBound(kTrackGainPopupMinValue, sliderValue, kTrackGainPopupMaxValue);
    const bool changed = !m_trackGainPopupVisible
        || m_trackGainPopupValue != valueText
        || m_trackGainPopupAnchorX != anchorX
        || m_trackGainPopupAnchorY != anchorY
        || m_trackGainPopupSliderValue != clampedSliderValue;
    m_trackGainPopupVisible = true;
    m_trackGainPopupValue = valueText;
    m_trackGainPopupAnchorX = anchorX;
    m_trackGainPopupAnchorY = anchorY;
    m_trackGainPopupSliderValue = clampedSliderValue;
    emitIfChanged(changed);
}

void ShellOverlayController::hideTrackGainPopup()
{
    const bool changed = m_trackGainPopupVisible;
    m_trackGainPopupVisible = false;
    emitIfChanged(changed);
}

void ShellOverlayController::setTrackGainPopupValue(const QString& valueText, const int sliderValue)
{
    const int clampedSliderValue = qBound(kTrackGainPopupMinValue, sliderValue, kTrackGainPopupMaxValue);
    const bool changed = m_trackGainPopupValue != valueText || m_trackGainPopupSliderValue != clampedSliderValue;
    m_trackGainPopupValue = valueText;
    m_trackGainPopupSliderValue = clampedSliderValue;
    emitIfChanged(changed);
}

void ShellOverlayController::setTrackGainPopupSliderValueFromUi(const int sliderValue)
{
    const int clampedSliderValue = qBound(kTrackGainPopupMinValue, sliderValue, kTrackGainPopupMaxValue);
    const bool changed = m_trackGainPopupSliderValue != clampedSliderValue;
    m_trackGainPopupSliderValue = clampedSliderValue;
    emitIfChanged(changed);
    emit trackGainSliderValueChanged(clampedSliderValue);
}

void ShellOverlayController::dismissTrackGainPopup()
{
    hideTrackGainPopup();
}

void ShellOverlayController::emitIfChanged(const bool changed)
{
    if (changed)
    {
        emit this->changed();
    }
}
