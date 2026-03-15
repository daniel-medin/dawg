#include "app/SelectionController.h"

#include <algorithm>

void SelectionController::reset()
{
    m_selectedTrackIds.clear();
    m_selectedTrackId = {};
    m_fadingDeselectedTrackId = {};
    m_fadingDeselectedTrackOpacity = 0.0F;
}

bool SelectionController::hasSelection() const
{
    return !m_selectedTrackIds.empty();
}

const std::vector<QUuid>& SelectionController::selectedTrackIds() const
{
    return m_selectedTrackIds;
}

QUuid SelectionController::selectedTrackId() const
{
    return m_selectedTrackId;
}

QUuid SelectionController::fadingDeselectedTrackId() const
{
    return m_fadingDeselectedTrackId;
}

float SelectionController::fadingDeselectedTrackOpacity() const
{
    return m_fadingDeselectedTrackOpacity;
}

bool SelectionController::isTrackSelected(const QUuid& trackId) const
{
    return std::find(m_selectedTrackIds.begin(), m_selectedTrackIds.end(), trackId) != m_selectedTrackIds.end();
}

bool SelectionController::setSelectedTrackIds(const std::vector<QUuid>& trackIds)
{
    const auto nextSelectedTrackId = trackIds.empty() ? QUuid{} : trackIds.front();
    if (m_selectedTrackIds == trackIds
        && m_selectedTrackId == nextSelectedTrackId
        && m_fadingDeselectedTrackId.isNull()
        && m_fadingDeselectedTrackOpacity <= 0.0F)
    {
        return false;
    }

    m_selectedTrackIds = trackIds;
    m_selectedTrackId = nextSelectedTrackId;
    m_fadingDeselectedTrackId = {};
    m_fadingDeselectedTrackOpacity = 0.0F;
    return true;
}

bool SelectionController::setSelectedTrackId(const QUuid& trackId, const bool fadePreviousSelection)
{
    if (m_selectedTrackId == trackId)
    {
        if (trackId.isNull() && m_selectedTrackIds.empty())
        {
            return false;
        }
        if (!trackId.isNull() && m_selectedTrackIds.size() == 1 && m_selectedTrackIds.front() == trackId)
        {
            return false;
        }
    }

    if (trackId.isNull() && m_selectedTrackIds.empty())
    {
        return false;
    }

    if (fadePreviousSelection && !m_selectedTrackId.isNull())
    {
        m_fadingDeselectedTrackId = m_selectedTrackId;
        m_fadingDeselectedTrackOpacity = 1.0F;
    }
    else
    {
        m_fadingDeselectedTrackId = {};
        m_fadingDeselectedTrackOpacity = 0.0F;
    }

    m_selectedTrackId = trackId;
    m_selectedTrackIds.clear();
    if (!trackId.isNull())
    {
        m_selectedTrackIds.push_back(trackId);
    }
    return true;
}

bool SelectionController::clearSelection(const bool fadePreviousSelection)
{
    return setSelectedTrackId({}, fadePreviousSelection);
}

bool SelectionController::selectNextVisibleTrack(const std::vector<QUuid>& visibleTrackIds)
{
    if (visibleTrackIds.empty())
    {
        return false;
    }

    auto nextIndex = 0;
    if (!m_selectedTrackId.isNull())
    {
        const auto currentIt = std::find(visibleTrackIds.begin(), visibleTrackIds.end(), m_selectedTrackId);
        if (currentIt != visibleTrackIds.end())
        {
            nextIndex = static_cast<int>(std::distance(visibleTrackIds.begin(), currentIt) + 1)
                % static_cast<int>(visibleTrackIds.size());
        }
    }

    return setSelectedTrackId(visibleTrackIds[static_cast<std::size_t>(nextIndex)]);
}

bool SelectionController::advanceFade(const float step)
{
    if (m_fadingDeselectedTrackId.isNull())
    {
        return false;
    }

    const auto nextOpacity = m_fadingDeselectedTrackOpacity - step;
    m_fadingDeselectedTrackOpacity = nextOpacity > 0.0F ? nextOpacity : 0.0F;
    if (m_fadingDeselectedTrackOpacity <= 0.0F)
    {
        m_fadingDeselectedTrackId = {};
        return false;
    }

    return true;
}
