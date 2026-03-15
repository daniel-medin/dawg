#include "ui/MixView.h"

#include <cmath>
#include <functional>

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QProgressBar>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>

namespace
{
constexpr int kMixFaderMinValue = -1000;
constexpr int kMixFaderMaxValue = 120;
constexpr float kMixSilentGainDb = static_cast<float>(kMixFaderMinValue) / 10.0F;
constexpr float kMeterDisplayFloorDb = kMixSilentGainDb;
constexpr float kMeterDisplayCeilingDb = static_cast<float>(kMixFaderMaxValue) / 10.0F;
constexpr float kMeterYellowThresholdDb = -18.0F;
constexpr float kMeterOrangeThresholdDb = -6.0F;

float sliderValueToGainDb(const int sliderValue)
{
    return sliderValue <= kMixFaderMinValue ? kMixSilentGainDb : static_cast<float>(sliderValue) / 10.0F;
}

int gainDbToSliderValue(const float gainDb)
{
    if (gainDb <= kMixSilentGainDb + 0.001F)
    {
        return kMixFaderMinValue;
    }

    return static_cast<int>(std::lround(std::clamp(gainDb, kMixSilentGainDb, 12.0F) * 10.0F));
}

QString gainLabelText(const float gainDb)
{
    if (gainDb <= kMixSilentGainDb + 0.001F)
    {
        return QStringLiteral("-inf");
    }

    return QStringLiteral("%1 dB").arg(gainDb, 0, 'f', 1);
}

float meterLevelToDb(const float level)
{
    if (level <= 0.0F)
    {
        return kMeterDisplayFloorDb;
    }

    return std::clamp(20.0F * std::log10(level), kMeterDisplayFloorDb, 0.0F);
}

int meterValueForLevel(const float level)
{
    const auto meterDb = meterLevelToDb(level);
    const auto normalized = (meterDb - kMeterDisplayFloorDb) / (kMeterDisplayCeilingDb - kMeterDisplayFloorDb);
    return static_cast<int>(std::lround(std::clamp(normalized, 0.0F, 1.0F) * 1000.0F));
}

QString meterStyleSheet()
{
    const auto yellowStop =
        (kMeterYellowThresholdDb - kMeterDisplayFloorDb) / (kMeterDisplayCeilingDb - kMeterDisplayFloorDb);
    const auto orangeStop =
        (kMeterOrangeThresholdDb - kMeterDisplayFloorDb) / (kMeterDisplayCeilingDb - kMeterDisplayFloorDb);

    return QStringLiteral(
        "QProgressBar {"
        "  background: #0b1016;"
        "  border: 1px solid #1d2733;"
        "  border-radius: 4px;"
        "}"
        "QProgressBar::chunk {"
        "  background: qlineargradient(x1:0, y1:1, x2:0, y2:0,"
        "    stop:0 #2fe06d,"
        "    stop:%1 #2fe06d,"
        "    stop:%2 #ffb33b,"
        "    stop:1 #ff5b4d);"
        "  border-radius: 3px;"
        "}")
        .arg(yellowStop, 0, 'f', 3)
        .arg(orangeStop, 0, 'f', 3);
}

void applyMixButtonStyle(QToolButton* button, const bool checked, const bool soloButton, const bool enabled)
{
    if (!button)
    {
        return;
    }

    if (!enabled)
    {
        button->setStyleSheet(QStringLiteral(
            "QToolButton {"
            "  background: rgba(255, 255, 255, 0.05);"
            "  color: rgba(214, 223, 233, 0.28);"
            "  border: 1px solid rgba(255, 255, 255, 0.06);"
            "  border-radius: 4px;"
            "  font-size: 7pt;"
            "  font-weight: 700;"
            "}"));
        return;
    }

    if (soloButton)
    {
        button->setStyleSheet(checked
            ? QStringLiteral(
                "QToolButton {"
                "  background: rgba(219, 126, 38, 0.92);"
                "  color: #fff4e9;"
                "  border: 1px solid rgba(255, 215, 180, 0.45);"
                "  border-radius: 4px;"
                "  font-size: 7pt;"
                "  font-weight: 700;"
                "}")
            : QStringLiteral(
                "QToolButton {"
                "  background: rgba(255, 255, 255, 0.06);"
                "  color: #d6dfe9;"
                "  border: 1px solid rgba(255, 255, 255, 0.08);"
                "  border-radius: 4px;"
                "  font-size: 7pt;"
                "  font-weight: 700;"
                "}"
                "QToolButton:hover { background: rgba(255, 255, 255, 0.10); }"));
        return;
    }

    button->setStyleSheet(checked
        ? QStringLiteral(
            "QToolButton {"
            "  background: rgba(214, 223, 233, 0.12);"
            "  color: rgba(214, 223, 233, 0.45);"
            "  border: 1px solid rgba(214, 223, 233, 0.06);"
            "  border-radius: 4px;"
            "  font-size: 7pt;"
            "  font-weight: 700;"
            "}")
        : QStringLiteral(
            "QToolButton {"
            "  background: rgba(255, 255, 255, 0.06);"
            "  color: #d6dfe9;"
            "  border: 1px solid rgba(255, 255, 255, 0.08);"
            "  border-radius: 4px;"
            "  font-size: 7pt;"
            "  font-weight: 700;"
            "}"
            "QToolButton:hover { background: rgba(255, 255, 255, 0.10); }"));
}

class MixFaderSlider final : public QSlider
{
public:
    explicit MixFaderSlider(QWidget* parent = nullptr)
        : QSlider(Qt::Vertical, parent)
    {
    }

protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton && (event->modifiers() & Qt::ControlModifier))
        {
            setValue(0);
            event->accept();
            return;
        }

        QSlider::mousePressEvent(event);
    }
};

class MixStripWidget final : public QFrame
{
public:
    MixStripWidget(
        const QString& title,
        const QString& detail,
        const QString& footer,
        const QColor& accentColor,
        const float gainDb,
        const bool masterStrip,
        const bool muted,
        const bool soloed,
        std::function<void(float)> gainChanged,
        std::function<void(bool)> muteChanged,
        std::function<void(bool)> soloChanged,
        QWidget* parent = nullptr)
        : QFrame(parent)
    {
        setObjectName(masterStrip ? QStringLiteral("mixMasterStrip") : QStringLiteral("mixStrip"));
        setFrameShape(QFrame::NoFrame);
        setMinimumWidth(88);
        setMaximumWidth(108);
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        setStyleSheet(masterStrip
            ? QStringLiteral(
                "QFrame#mixMasterStrip { background: #141b24; border: 1px solid #43566f; border-radius: 10px; }")
            : QStringLiteral(
                "QFrame#mixStrip { background: #0f141b; border: 1px solid #1b2430; border-radius: 10px; }"));

        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(8, 10, 8, 10);
        layout->setSpacing(8);

        auto* contentLayout = new QVBoxLayout();
        contentLayout->setContentsMargins(0, 0, 0, 0);
        contentLayout->setSpacing(6);

        auto* buttonRow = new QHBoxLayout();
        buttonRow->setContentsMargins(0, 0, 0, 0);
        buttonRow->setSpacing(4);

        m_soloButton = new QToolButton(this);
        m_soloButton->setText(QStringLiteral("S"));
        m_soloButton->setCheckable(true);
        m_soloButton->setChecked(soloed);
        m_soloButton->setCursor(masterStrip ? Qt::ArrowCursor : Qt::PointingHandCursor);
        m_soloButton->setEnabled(!masterStrip);
        m_soloButton->setFixedSize(22, 18);
        applyMixButtonStyle(m_soloButton, soloed, true, !masterStrip);
        buttonRow->addWidget(m_soloButton, 1);

        m_muteButton = new QToolButton(this);
        m_muteButton->setText(QStringLiteral("M"));
        m_muteButton->setCheckable(true);
        m_muteButton->setChecked(muted);
        m_muteButton->setCursor(Qt::PointingHandCursor);
        m_muteButton->setFixedSize(22, 18);
        applyMixButtonStyle(m_muteButton, muted, false, true);
        buttonRow->addWidget(m_muteButton, 1);
        contentLayout->addLayout(buttonRow);

        auto* swatch = new QFrame(this);
        swatch->setFixedHeight(6);
        swatch->setStyleSheet(QStringLiteral("background: %1; border-radius: 3px;")
                                  .arg(accentColor.name(QColor::HexRgb)));
        contentLayout->addWidget(swatch);

        auto* titleLabel = new QLabel(title, this);
        titleLabel->setAlignment(Qt::AlignCenter);
        titleLabel->setStyleSheet(QStringLiteral("color: #eef2f6; font-size: 8.5pt; font-weight: 600;"));
        titleLabel->setWordWrap(true);
        contentLayout->addWidget(titleLabel);

        auto* detailLabel = new QLabel(detail, this);
        detailLabel->setAlignment(Qt::AlignCenter);
        detailLabel->setStyleSheet(QStringLiteral("color: #95a4b5; font-size: 7.5pt;"));
        detailLabel->setWordWrap(true);
        contentLayout->addWidget(detailLabel);

        auto* faderRow = new QHBoxLayout();
        faderRow->setContentsMargins(0, 0, 0, 0);
        faderRow->setSpacing(8);
        faderRow->addStretch(1);

        m_slider = new MixFaderSlider(this);
        m_slider->setRange(kMixFaderMinValue, kMixFaderMaxValue);
        m_slider->setValue(gainDbToSliderValue(gainDb));
        m_slider->setTickInterval(60);
        m_slider->setTickPosition(QSlider::TicksLeft);
        m_slider->setMinimumHeight(96);
        m_slider->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        m_slider->setStyleSheet(QStringLiteral(
            "QSlider::groove:vertical { background: #1a212b; width: 6px; border-radius: 3px; }"
            "QSlider::sub-page:vertical { background: #11161d; border-radius: 3px; }"
            "QSlider::add-page:vertical { background: #2d3948; border-radius: 3px; }"
            "QSlider::handle:vertical { background: #f2f5f8; border: 1px solid #3e4d61; height: 16px; margin: -3px -7px; border-radius: 7px; }"));
        faderRow->addWidget(m_slider, 0);

        m_meter = new QProgressBar(this);
        m_meter->setRange(0, 1000);
        m_meter->setValue(0);
        m_meter->setTextVisible(false);
        m_meter->setOrientation(Qt::Vertical);
        m_meter->setFixedWidth(12);
        m_meter->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        m_meter->setStyleSheet(meterStyleSheet());
        faderRow->addWidget(m_meter, 0);
        faderRow->addStretch(1);

        layout->addLayout(contentLayout, 1);
        contentLayout->addLayout(faderRow, 1);

        m_gainLabel = new QLabel(gainLabelText(gainDb), this);
        m_gainLabel->setAlignment(Qt::AlignCenter);
        m_gainLabel->setStyleSheet(QStringLiteral("color: #cad3dc; font-size: 7.5pt;"));
        contentLayout->addWidget(m_gainLabel);

        auto* footerLabel = new QLabel(footer, this);
        footerLabel->setAlignment(Qt::AlignCenter);
        footerLabel->setStyleSheet(QStringLiteral("color: #6e8094; font-size: 7pt; letter-spacing: 0.08em;"));
        contentLayout->addWidget(footerLabel);

        connect(m_slider, &QSlider::valueChanged, this, [this](const int value)
        {
            if (m_gainLabel)
            {
                m_gainLabel->setText(gainLabelText(sliderValueToGainDb(value)));
            }
        });
        connect(m_slider, &QSlider::valueChanged, this, [gainChanged = std::move(gainChanged)](const int value)
        {
            if (gainChanged)
            {
                gainChanged(sliderValueToGainDb(value));
            }
        });
        connect(m_muteButton, &QToolButton::toggled, this, [this, muteChanged](const bool checked)
        {
            applyMixButtonStyle(m_muteButton, checked, false, true);
            if (muteChanged)
            {
                muteChanged(checked);
            }
        });
        connect(m_soloButton, &QToolButton::toggled, this, [this, soloChanged](const bool checked)
        {
            applyMixButtonStyle(m_soloButton, checked, true, m_soloButton->isEnabled());
            if (soloChanged)
            {
                soloChanged(checked);
            }
        });
    }

    void setGainDb(const float gainDb)
    {
        if (!m_slider)
        {
            return;
        }

        if (!m_slider->isSliderDown())
        {
            const auto sliderValue = gainDbToSliderValue(gainDb);
            if (m_slider->value() != sliderValue)
            {
                const QSignalBlocker blocker{m_slider};
                m_slider->setValue(sliderValue);
            }
        }

        if (m_gainLabel)
        {
            m_gainLabel->setText(gainLabelText(gainDb));
        }
    }

    void setMuted(const bool muted)
    {
        if (!m_muteButton)
        {
            return;
        }

        if (m_muteButton->isChecked() != muted)
        {
            const QSignalBlocker blocker{m_muteButton};
            m_muteButton->setChecked(muted);
        }
        applyMixButtonStyle(m_muteButton, muted, false, true);
    }

    void setSoloed(const bool soloed)
    {
        if (!m_soloButton)
        {
            return;
        }

        if (m_soloButton->isChecked() != soloed)
        {
            const QSignalBlocker blocker{m_soloButton};
            m_soloButton->setChecked(soloed);
        }
        applyMixButtonStyle(m_soloButton, soloed, true, m_soloButton->isEnabled());
    }

    void setMeterLevel(const float level)
    {
        if (!m_meter)
        {
            return;
        }

        m_meter->setValue(meterValueForLevel(level));
    }

    [[nodiscard]] QProgressBar* meter() const
    {
        return m_meter;
    }

private:
    MixFaderSlider* m_slider = nullptr;
    QLabel* m_gainLabel = nullptr;
    QToolButton* m_muteButton = nullptr;
    QToolButton* m_soloButton = nullptr;
    QProgressBar* m_meter = nullptr;
};
}

MixView::MixView(QWidget* parent)
    : QWidget(parent)
    , m_layout(new QHBoxLayout(this))
    , m_emptyLabel(new QLabel(QStringLiteral("No mix lanes yet."), this))
{
    setObjectName(QStringLiteral("mixView"));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_layout->setContentsMargins(12, 10, 12, 10);
    m_layout->setSpacing(10);

    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setStyleSheet(QStringLiteral("color: #8d9aae; font-size: 9pt;"));
    m_layout->addWidget(m_emptyLabel, 1);
}

void MixView::setMixState(const float masterGainDb, const bool masterMuted, const std::vector<MixLaneStrip>& laneStrips)
{
    const auto shouldRebuild = needsRebuild(laneStrips);
    m_masterGainDb = masterGainDb;
    m_masterMuted = masterMuted;
    m_laneStrips = laneStrips;

    if (shouldRebuild)
    {
        rebuildStrips();
        return;
    }

    syncStripStates();
}

void MixView::setMeterLevels(const float masterMeterLevel, const std::vector<MixLaneStrip>& laneStrips)
{
    if (m_masterMeter)
    {
        m_masterMeter->setValue(meterValueForLevel(masterMeterLevel));
    }

    for (const auto& strip : laneStrips)
    {
        const auto meterIt = m_laneMeters.find(strip.laneIndex);
        if (meterIt == m_laneMeters.end() || !meterIt->second)
        {
            continue;
        }

        meterIt->second->setValue(meterValueForLevel(strip.meterLevel));
    }
}

QSize MixView::sizeHint() const
{
    return QSize{640, 184};
}

QSize MixView::minimumSizeHint() const
{
    return QSize{320, 132};
}

bool MixView::needsRebuild(const std::vector<MixLaneStrip>& laneStrips) const
{
    if (!m_masterStripWidget)
    {
        return true;
    }

    if (laneStrips.size() != m_laneStrips.size())
    {
        return true;
    }

    for (std::size_t index = 0; index < laneStrips.size(); ++index)
    {
        const auto& nextStrip = laneStrips[index];
        const auto& currentStrip = m_laneStrips[index];
        if (nextStrip.laneIndex != currentStrip.laneIndex
            || nextStrip.label != currentStrip.label
            || nextStrip.color != currentStrip.color
            || nextStrip.clipCount != currentStrip.clipCount)
        {
            return true;
        }
    }

    return false;
}

void MixView::syncStripStates()
{
    auto* masterStrip = dynamic_cast<MixStripWidget*>(m_masterStripWidget);
    if (masterStrip)
    {
        masterStrip->setGainDb(m_masterGainDb);
        masterStrip->setMuted(m_masterMuted);
        masterStrip->setSoloed(false);
    }

    for (const auto& strip : m_laneStrips)
    {
        const auto stripIt = m_laneStripWidgets.find(strip.laneIndex);
        if (stripIt == m_laneStripWidgets.end())
        {
            continue;
        }

        auto* stripWidget = dynamic_cast<MixStripWidget*>(stripIt->second);
        if (!stripWidget)
        {
            continue;
        }

        stripWidget->setGainDb(strip.gainDb);
        stripWidget->setMuted(strip.muted);
        stripWidget->setSoloed(strip.soloed);
    }
}

void MixView::rebuildStrips()
{
    m_masterStripWidget = nullptr;
    m_masterMeter = nullptr;
    m_laneStripWidgets.clear();
    m_laneMeters.clear();
    while (auto* item = m_layout->takeAt(0))
    {
        if (auto* widget = item->widget())
        {
            if (widget != m_emptyLabel)
            {
                widget->deleteLater();
            }
        }
        delete item;
    }

    auto* masterStrip = new MixStripWidget(
            QStringLiteral("Master"),
            QStringLiteral("Main Out"),
            QStringLiteral("MASTER"),
            QColor(QStringLiteral("#f0f4f8")),
            m_masterGainDb,
            true,
            m_masterMuted,
            false,
            [this](const float gainDb)
            {
                emit masterGainChanged(gainDb);
            },
            [this](const bool muted)
            {
                emit masterMutedChanged(muted);
            },
            {},
            this);
    m_masterStripWidget = masterStrip;
    m_masterMeter = masterStrip->meter();
    m_layout->addWidget(masterStrip, 0);

    if (m_laneStrips.empty())
    {
        m_layout->addSpacing(6);
        m_layout->addWidget(m_emptyLabel, 1);
        m_emptyLabel->show();
        return;
    }

    m_emptyLabel->hide();
    for (const auto& strip : m_laneStrips)
    {
        auto* stripWidget = new MixStripWidget(
                strip.label,
                strip.clipCount == 1
                    ? QStringLiteral("1 clip")
                    : QStringLiteral("%1 clips").arg(strip.clipCount),
                QStringLiteral("Lane %1").arg(strip.laneIndex + 1),
                strip.color,
                strip.gainDb,
                false,
                strip.muted,
                strip.soloed,
                [this, laneIndex = strip.laneIndex](const float gainDb)
                {
                    emit laneGainChanged(laneIndex, gainDb);
                },
                [this, laneIndex = strip.laneIndex](const bool muted)
                {
                    emit laneMutedChanged(laneIndex, muted);
                },
                [this, laneIndex = strip.laneIndex](const bool soloed)
                {
                    emit laneSoloChanged(laneIndex, soloed);
                },
                this);
        m_laneStripWidgets[strip.laneIndex] = stripWidget;
        m_laneMeters[strip.laneIndex] = stripWidget->meter();
        m_layout->addWidget(stripWidget, 0);
    }
    m_layout->addStretch(1);
    syncStripStates();
}
