#include "app/DialogController.h"

#include <QVariantMap>

DialogController::DialogController(QObject* parent)
    : QObject(parent)
{
}

bool DialogController::visible() const
{
    return m_visible;
}

QString DialogController::title() const
{
    return m_title;
}

QString DialogController::message() const
{
    return m_message;
}

QString DialogController::informativeText() const
{
    return m_informativeText;
}

bool DialogController::inputMode() const
{
    return m_inputMode;
}

QString DialogController::inputLabel() const
{
    return m_inputLabel;
}

QString DialogController::inputText() const
{
    return m_inputText;
}

void DialogController::setInputText(const QString& inputText)
{
    if (m_inputText == inputText)
    {
        return;
    }

    m_inputText = inputText;
    emit changed();
}

QVariantList DialogController::buttons() const
{
    return m_buttons;
}

DialogController::Button DialogController::execMessage(
    const QString& title,
    const QString& message,
    const QString& informativeText,
    const QList<Button>& buttons,
    const Button defaultButton)
{
    showRequest(title, message, informativeText, false, {}, {}, buttons, defaultButton);

    QEventLoop eventLoop;
    m_eventLoop = &eventLoop;
    eventLoop.exec();
    m_eventLoop = nullptr;
    return m_result;
}

std::optional<QString> DialogController::execTextInput(
    const QString& title,
    const QString& label,
    const QString& initialText)
{
    showRequest(
        title,
        {},
        {},
        true,
        label,
        initialText,
        {Button::Ok, Button::Cancel},
        Button::Ok);

    QEventLoop eventLoop;
    m_eventLoop = &eventLoop;
    eventLoop.exec();
    m_eventLoop = nullptr;
    return m_textResult;
}

void DialogController::hide()
{
    finish(Button::Cancel);
}

QString DialogController::buttonKey(const Button button)
{
    switch (button)
    {
    case Button::Ok:
        return QStringLiteral("ok");
    case Button::Cancel:
        return QStringLiteral("cancel");
    case Button::Yes:
        return QStringLiteral("yes");
    case Button::No:
        return QStringLiteral("no");
    case Button::Save:
        return QStringLiteral("save");
    case Button::Discard:
        return QStringLiteral("discard");
    case Button::NewProject:
        return QStringLiteral("newProject");
    case Button::OpenProject:
        return QStringLiteral("openProject");
    case Button::None:
        break;
    }

    return {};
}

QString DialogController::buttonText(const Button button)
{
    switch (button)
    {
    case Button::Ok:
        return QStringLiteral("OK");
    case Button::Cancel:
        return QStringLiteral("Cancel");
    case Button::Yes:
        return QStringLiteral("Yes");
    case Button::No:
        return QStringLiteral("No");
    case Button::Save:
        return QStringLiteral("Save");
    case Button::Discard:
        return QStringLiteral("Discard");
    case Button::NewProject:
        return QStringLiteral("New Project");
    case Button::OpenProject:
        return QStringLiteral("Open Project");
    case Button::None:
        break;
    }

    return {};
}

void DialogController::respond(const QString& buttonKeyValue, const QString& inputText)
{
    m_inputText = inputText;
    Button matchedButton = Button::None;
    for (const auto button : {Button::Ok, Button::Cancel, Button::Yes, Button::No, Button::Save, Button::Discard, Button::NewProject, Button::OpenProject})
    {
        if (buttonKey(button) == buttonKeyValue)
        {
            matchedButton = button;
            break;
        }
    }

    if (m_inputMode && matchedButton == Button::Ok)
    {
        m_textResult = m_inputText;
    }
    else if (m_inputMode)
    {
        m_textResult = std::nullopt;
    }

    finish(matchedButton == Button::None ? Button::Cancel : matchedButton);
}

void DialogController::showRequest(
    const QString& title,
    const QString& message,
    const QString& informativeText,
    const bool inputMode,
    const QString& inputLabel,
    const QString& inputText,
    const QList<Button>& buttons,
    const Button defaultButton)
{
    m_title = title;
    m_message = message;
    m_informativeText = informativeText;
    m_inputMode = inputMode;
    m_inputLabel = inputLabel;
    m_inputText = inputText;
    m_defaultButton = defaultButton;
    m_result = Button::None;
    m_textResult = std::nullopt;
    m_buttons.clear();
    for (const auto button : buttons)
    {
        QVariantMap descriptor;
        descriptor.insert(QStringLiteral("key"), buttonKey(button));
        descriptor.insert(QStringLiteral("text"), buttonText(button));
        descriptor.insert(QStringLiteral("default"), button == defaultButton);
        descriptor.insert(QStringLiteral("accent"), button == Button::Save || button == Button::Ok || button == Button::Yes || button == Button::NewProject);
        descriptor.insert(QStringLiteral("destructive"), button == Button::Discard);
        m_buttons.push_back(descriptor);
    }

    m_visible = true;
    emit changed();
}

void DialogController::finish(const Button button)
{
    if (!m_visible && button == Button::None)
    {
        return;
    }

    m_result = button;
    m_visible = false;
    emit changed();

    if (m_eventLoop)
    {
        m_eventLoop->quit();
    }
}
