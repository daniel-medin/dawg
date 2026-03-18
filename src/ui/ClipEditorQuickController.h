#pragma once

#include <optional>

#include <QObject>
#include <QString>

#include "ui/ClipEditorTypes.h"

class ClipEditorQuickController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString titleText READ titleText NOTIFY stateChanged)
    Q_PROPERTY(QString sourceText READ sourceText NOTIFY stateChanged)
    Q_PROPERTY(QString rangeText READ rangeText NOTIFY stateChanged)
    Q_PROPERTY(QString durationText READ durationText NOTIFY stateChanged)
    Q_PROPERTY(QString positionText READ positionText NOTIFY stateChanged)
    Q_PROPERTY(QString emptyTitleText READ emptyTitleText NOTIFY stateChanged)
    Q_PROPERTY(QString emptyBodyText READ emptyBodyText NOTIFY stateChanged)
    Q_PROPERTY(QString emptyActionText READ emptyActionText NOTIFY stateChanged)
    Q_PROPERTY(bool showInfoBar READ showInfoBar NOTIFY stateChanged)
    Q_PROPERTY(bool showEditorContent READ showEditorContent NOTIFY stateChanged)
    Q_PROPERTY(bool showEmptyState READ showEmptyState NOTIFY stateChanged)
    Q_PROPERTY(bool showLoopButton READ showLoopButton NOTIFY stateChanged)
    Q_PROPERTY(bool showEmptyAction READ showEmptyAction NOTIFY stateChanged)
    Q_PROPERTY(bool loopEnabled READ loopEnabled NOTIFY stateChanged)
    Q_PROPERTY(float gainDb READ gainDb NOTIFY stateChanged)
    Q_PROPERTY(float meterLevel READ meterLevel NOTIFY stateChanged)

public:
    explicit ClipEditorQuickController(QObject* parent = nullptr);

    void setState(const std::optional<ClipEditorState>& state);

    [[nodiscard]] QString titleText() const;
    [[nodiscard]] QString sourceText() const;
    [[nodiscard]] QString rangeText() const;
    [[nodiscard]] QString durationText() const;
    [[nodiscard]] QString positionText() const;
    [[nodiscard]] QString emptyTitleText() const;
    [[nodiscard]] QString emptyBodyText() const;
    [[nodiscard]] QString emptyActionText() const;
    [[nodiscard]] bool showInfoBar() const;
    [[nodiscard]] bool showEditorContent() const;
    [[nodiscard]] bool showEmptyState() const;
    [[nodiscard]] bool showLoopButton() const;
    [[nodiscard]] bool showEmptyAction() const;
    [[nodiscard]] bool loopEnabled() const;
    [[nodiscard]] float gainDb() const;
    [[nodiscard]] float meterLevel() const;

    Q_INVOKABLE void handleGainDragged(double gainDb);
    Q_INVOKABLE void handleLoopToggled(bool enabled);
    Q_INVOKABLE void requestAttachAudio();

signals:
    void stateChanged();
    void gainChanged(float gainDb);
    void loopSoundChanged(bool enabled);
    void attachAudioRequested();

private:
    std::optional<ClipEditorState> m_state;
};
