#pragma once

#include <QObject>

class ShellOverlayController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool visible READ visible NOTIFY changed)
    Q_PROPERTY(bool statusVisible READ statusVisible NOTIFY changed)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY changed)
    Q_PROPERTY(int statusMaxWidth READ statusMaxWidth NOTIFY changed)
    Q_PROPERTY(bool canvasTipsVisible READ canvasTipsVisible NOTIFY changed)
    Q_PROPERTY(QString canvasTipsMessage READ canvasTipsMessage NOTIFY changed)
    Q_PROPERTY(int canvasTipsX READ canvasTipsX NOTIFY changed)
    Q_PROPERTY(int canvasTipsY READ canvasTipsY NOTIFY changed)
    Q_PROPERTY(int canvasTipsMaxWidth READ canvasTipsMaxWidth NOTIFY changed)
    Q_PROPERTY(bool trackGainPopupVisible READ trackGainPopupVisible NOTIFY changed)
    Q_PROPERTY(QString trackGainPopupValue READ trackGainPopupValue NOTIFY changed)
    Q_PROPERTY(int trackGainPopupAnchorX READ trackGainPopupAnchorX NOTIFY changed)
    Q_PROPERTY(int trackGainPopupAnchorY READ trackGainPopupAnchorY NOTIFY changed)
    Q_PROPERTY(int trackGainPopupSliderValue READ trackGainPopupSliderValue NOTIFY changed)
    Q_PROPERTY(int trackGainPopupMinimum READ trackGainPopupMinimum CONSTANT)
    Q_PROPERTY(int trackGainPopupMaximum READ trackGainPopupMaximum CONSTANT)

public:
    explicit ShellOverlayController(QObject* parent = nullptr);

    [[nodiscard]] bool visible() const;
    [[nodiscard]] bool statusVisible() const;
    [[nodiscard]] QString statusMessage() const;
    [[nodiscard]] int statusMaxWidth() const;
    [[nodiscard]] bool canvasTipsVisible() const;
    [[nodiscard]] QString canvasTipsMessage() const;
    [[nodiscard]] int canvasTipsX() const;
    [[nodiscard]] int canvasTipsY() const;
    [[nodiscard]] int canvasTipsMaxWidth() const;
    [[nodiscard]] bool trackGainPopupVisible() const;
    [[nodiscard]] QString trackGainPopupValue() const;
    [[nodiscard]] int trackGainPopupAnchorX() const;
    [[nodiscard]] int trackGainPopupAnchorY() const;
    [[nodiscard]] int trackGainPopupSliderValue() const;
    [[nodiscard]] int trackGainPopupMinimum() const;
    [[nodiscard]] int trackGainPopupMaximum() const;

    void showStatus(const QString& message);
    void hideStatus();
    void setStatusMaxWidth(int width);

    void showCanvasTips(const QString& message);
    void hideCanvasTips();
    void setCanvasTipsPosition(int x, int y);
    void setCanvasTipsMaxWidth(int width);

    void showTrackGainPopup(const QString& valueText, int anchorX, int anchorY, int sliderValue);
    void hideTrackGainPopup();
    void setTrackGainPopupValue(const QString& valueText, int sliderValue);

    Q_INVOKABLE void setTrackGainPopupSliderValueFromUi(int sliderValue);
    Q_INVOKABLE void dismissTrackGainPopup();

signals:
    void changed();
    void trackGainSliderValueChanged(int sliderValue);

private:
    void emitIfChanged(bool changed);

    bool m_statusVisible = false;
    QString m_statusMessage;
    int m_statusMaxWidth = 360;

    bool m_canvasTipsVisible = false;
    QString m_canvasTipsMessage;
    int m_canvasTipsX = 16;
    int m_canvasTipsY = 16;
    int m_canvasTipsMaxWidth = 300;

    bool m_trackGainPopupVisible = false;
    QString m_trackGainPopupValue;
    int m_trackGainPopupAnchorX = 0;
    int m_trackGainPopupAnchorY = 0;
    int m_trackGainPopupSliderValue = 0;
};
