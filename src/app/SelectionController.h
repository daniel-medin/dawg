#pragma once

#include <vector>

#include <QUuid>

class SelectionController
{
public:
    void reset();

    [[nodiscard]] bool hasSelection() const;
    [[nodiscard]] const std::vector<QUuid>& selectedTrackIds() const;
    [[nodiscard]] QUuid selectedTrackId() const;
    [[nodiscard]] QUuid fadingDeselectedTrackId() const;
    [[nodiscard]] float fadingDeselectedTrackOpacity() const;
    [[nodiscard]] bool isTrackSelected(const QUuid& trackId) const;

    [[nodiscard]] bool setSelectedTrackIds(const std::vector<QUuid>& trackIds);
    [[nodiscard]] bool setSelectedTrackId(const QUuid& trackId, bool fadePreviousSelection = true);
    [[nodiscard]] bool clearSelection(bool fadePreviousSelection = true);
    [[nodiscard]] bool selectNextVisibleTrack(const std::vector<QUuid>& visibleTrackIds);
    [[nodiscard]] bool advanceFade(float step = 0.18F);

private:
    std::vector<QUuid> m_selectedTrackIds;
    QUuid m_selectedTrackId;
    QUuid m_fadingDeselectedTrackId;
    float m_fadingDeselectedTrackOpacity = 0.0F;
};
