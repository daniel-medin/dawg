#pragma once

#include <unordered_map>
#include <vector>

#include <QColor>
#include <QString>
#include <QWidget>

class QHBoxLayout;
class QLabel;
class QProgressBar;

struct MixLaneStrip
{
    int laneIndex = 0;
    QString label;
    QColor color;
    float gainDb = 0.0F;
    float meterLevel = 0.0F;
    int clipCount = 0;
    bool muted = false;
    bool soloed = false;

    [[nodiscard]] bool operator==(const MixLaneStrip& other) const
    {
        return laneIndex == other.laneIndex
            && label == other.label
            && color == other.color
            && gainDb == other.gainDb
            && clipCount == other.clipCount
            && muted == other.muted
            && soloed == other.soloed;
    }
};

class MixView final : public QWidget
{
    Q_OBJECT

public:
    explicit MixView(QWidget* parent = nullptr);

    void setMixState(float masterGainDb, bool masterMuted, const std::vector<MixLaneStrip>& laneStrips);
    void setMeterLevels(float masterMeterLevel, const std::vector<MixLaneStrip>& laneStrips);
    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override;

signals:
    void masterGainChanged(float gainDb);
    void masterMutedChanged(bool muted);
    void laneGainChanged(int laneIndex, float gainDb);
    void laneMutedChanged(int laneIndex, bool muted);
    void laneSoloChanged(int laneIndex, bool soloed);

private:
    [[nodiscard]] bool needsRebuild(const std::vector<MixLaneStrip>& laneStrips) const;
    void syncStripStates();
    void rebuildStrips();

    QHBoxLayout* m_layout = nullptr;
    QLabel* m_emptyLabel = nullptr;
    QWidget* m_masterStripWidget = nullptr;
    QProgressBar* m_masterMeter = nullptr;
    std::unordered_map<int, QWidget*> m_laneStripWidgets;
    std::unordered_map<int, QProgressBar*> m_laneMeters;
    float m_masterGainDb = 0.0F;
    bool m_masterMuted = false;
    std::vector<MixLaneStrip> m_laneStrips;
};
