#pragma once

#include <optional>

#include <QEventLoop>
#include <QObject>
#include <QVariantList>

class DialogController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool visible READ visible NOTIFY changed)
    Q_PROPERTY(QString title READ title NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(QString informativeText READ informativeText NOTIFY changed)
    Q_PROPERTY(bool inputMode READ inputMode NOTIFY changed)
    Q_PROPERTY(QString inputLabel READ inputLabel NOTIFY changed)
    Q_PROPERTY(QString inputText READ inputText WRITE setInputText NOTIFY changed)
    Q_PROPERTY(QVariantList buttons READ buttons NOTIFY changed)

public:
    enum class Button
    {
        None,
        Ok,
        Cancel,
        Yes,
        No,
        Save,
        Discard,
        NewProject,
        OpenProject,
    };
    Q_ENUM(Button)

    explicit DialogController(QObject* parent = nullptr);

    [[nodiscard]] bool visible() const;
    [[nodiscard]] QString title() const;
    [[nodiscard]] QString message() const;
    [[nodiscard]] QString informativeText() const;
    [[nodiscard]] bool inputMode() const;
    [[nodiscard]] QString inputLabel() const;
    [[nodiscard]] QString inputText() const;
    void setInputText(const QString& inputText);
    [[nodiscard]] QVariantList buttons() const;

    [[nodiscard]] Button execMessage(
        const QString& title,
        const QString& message,
        const QString& informativeText,
        const QList<Button>& buttons,
        Button defaultButton = Button::Ok);
    [[nodiscard]] std::optional<QString> execTextInput(
        const QString& title,
        const QString& label,
        const QString& initialText = {});
    void hide();

    [[nodiscard]] static QString buttonKey(Button button);
    [[nodiscard]] static QString buttonText(Button button);

    Q_INVOKABLE void respond(const QString& buttonKey, const QString& inputText = {});

signals:
    void changed();

private:
    void showRequest(
        const QString& title,
        const QString& message,
        const QString& informativeText,
        bool inputMode,
        const QString& inputLabel,
        const QString& inputText,
        const QList<Button>& buttons,
        Button defaultButton);
    void finish(Button button);

    bool m_visible = false;
    QString m_title;
    QString m_message;
    QString m_informativeText;
    bool m_inputMode = false;
    QString m_inputLabel;
    QString m_inputText;
    QVariantList m_buttons;
    Button m_defaultButton = Button::Ok;
    Button m_result = Button::None;
    std::optional<QString> m_textResult;
    QEventLoop* m_eventLoop = nullptr;
};
