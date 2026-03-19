#pragma once

#include <unordered_map>

#include <QColor>
#include <QObject>

class MixStripObject final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int laneIndex READ laneIndex NOTIFY laneIndexChanged)
    Q_PROPERTY(bool masterStrip READ masterStrip NOTIFY masterStripChanged)
    Q_PROPERTY(QString titleText READ titleText NOTIFY titleTextChanged)
    Q_PROPERTY(QString detailText READ detailText NOTIFY detailTextChanged)
    Q_PROPERTY(QString footerText READ footerText NOTIFY footerTextChanged)
    Q_PROPERTY(QColor accentColor READ accentColor NOTIFY accentColorChanged)
    Q_PROPERTY(float gainDb READ gainDb NOTIFY gainDbChanged)
    Q_PROPERTY(bool muted READ muted NOTIFY mutedChanged)
    Q_PROPERTY(bool soloEnabled READ soloEnabled NOTIFY soloEnabledChanged)
    Q_PROPERTY(bool soloed READ soloed NOTIFY soloedChanged)
    Q_PROPERTY(bool useStereoMeter READ useStereoMeter NOTIFY useStereoMeterChanged)
    Q_PROPERTY(float meterLevel READ meterLevel NOTIFY meterLevelChanged)
    Q_PROPERTY(float meterLeftLevel READ meterLeftLevel NOTIFY meterLeftLevelChanged)
    Q_PROPERTY(float meterRightLevel READ meterRightLevel NOTIFY meterRightLevelChanged)

public:
    explicit MixStripObject(QObject* parent = nullptr);

    [[nodiscard]] int laneIndex() const;
    void setLaneIndex(int laneIndex);

    [[nodiscard]] bool masterStrip() const;
    void setMasterStrip(bool masterStrip);

    [[nodiscard]] QString titleText() const;
    void setTitleText(const QString& titleText);

    [[nodiscard]] QString detailText() const;
    void setDetailText(const QString& detailText);

    [[nodiscard]] QString footerText() const;
    void setFooterText(const QString& footerText);

    [[nodiscard]] QColor accentColor() const;
    void setAccentColor(const QColor& accentColor);

    [[nodiscard]] float gainDb() const;
    void setGainDb(float gainDb);

    [[nodiscard]] bool muted() const;
    void setMuted(bool muted);

    [[nodiscard]] bool soloEnabled() const;
    void setSoloEnabled(bool soloEnabled);

    [[nodiscard]] bool soloed() const;
    void setSoloed(bool soloed);

    [[nodiscard]] bool useStereoMeter() const;
    void setUseStereoMeter(bool useStereoMeter);

    [[nodiscard]] float meterLevel() const;
    void setMeterLevel(float meterLevel);

    [[nodiscard]] float meterLeftLevel() const;
    void setMeterLeftLevel(float meterLeftLevel);

    [[nodiscard]] float meterRightLevel() const;
    void setMeterRightLevel(float meterRightLevel);

signals:
    void laneIndexChanged();
    void masterStripChanged();
    void titleTextChanged();
    void detailTextChanged();
    void footerTextChanged();
    void accentColorChanged();
    void gainDbChanged();
    void mutedChanged();
    void soloEnabledChanged();
    void soloedChanged();
    void useStereoMeterChanged();
    void meterLevelChanged();
    void meterLeftLevelChanged();
    void meterRightLevelChanged();

private:
    int m_laneIndex = -1;
    bool m_masterStrip = false;
    QString m_titleText;
    QString m_detailText;
    QString m_footerText;
    QColor m_accentColor = QColor(QStringLiteral("#f0f4f8"));
    float m_gainDb = 0.0F;
    bool m_muted = false;
    bool m_soloEnabled = false;
    bool m_soloed = false;
    bool m_useStereoMeter = false;
    float m_meterLevel = 0.0F;
    float m_meterLeftLevel = 0.0F;
    float m_meterRightLevel = 0.0F;
};

class MixQuickController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QObject* masterStrip READ masterStrip CONSTANT)
    Q_PROPERTY(QObjectList laneStrips READ laneStrips NOTIFY laneStripsChanged)
    Q_PROPERTY(bool playbackActive READ playbackActive NOTIFY playbackActiveChanged)
    Q_PROPERTY(int meterResetToken READ meterResetToken NOTIFY meterResetTokenChanged)

public:
    explicit MixQuickController(QObject* parent = nullptr);

    [[nodiscard]] QObject* masterStrip() const;
    [[nodiscard]] QObjectList laneStrips() const;
    [[nodiscard]] bool playbackActive() const;
    [[nodiscard]] int meterResetToken() const;

    void setMasterState(float gainDb, bool muted);
    void setMasterMeterLevels(float leftLevel, float rightLevel);
    void setLaneStrips(const QVariantList& descriptors);
    void setLaneState(int laneIndex, float gainDb, bool muted, bool soloed);
    void setLaneMeterLevels(int laneIndex, float leftLevel, float rightLevel);
    void setPlaybackActive(bool playbackActive);

    Q_INVOKABLE void setMasterGainDb(double gainDb);
    Q_INVOKABLE void setMasterMuted(bool muted);
    Q_INVOKABLE void setLaneGainDb(int laneIndex, double gainDb);
    Q_INVOKABLE void setLaneMuted(int laneIndex, bool muted);
    Q_INVOKABLE void setLaneSoloed(int laneIndex, bool soloed);

signals:
    void laneStripsChanged();
    void playbackActiveChanged();
    void meterResetTokenChanged();
    void masterGainChanged(float gainDb);
    void masterMutedChanged(bool muted);
    void laneGainChanged(int laneIndex, float gainDb);
    void laneMutedChanged(int laneIndex, bool muted);
    void laneSoloChanged(int laneIndex, bool soloed);

private:
    [[nodiscard]] MixStripObject* laneStrip(int laneIndex) const;

    MixStripObject* m_masterStrip = nullptr;
    QObjectList m_laneStrips;
    std::unordered_map<int, MixStripObject*> m_laneStripsByIndex;
    bool m_playbackActive = false;
    int m_meterResetToken = 0;
};
