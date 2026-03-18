#pragma once

#include <QObject>

class MainWindow;

class WindowChromeController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString windowTitle READ windowTitle NOTIFY windowTitleChanged)
    Q_PROPERTY(QString iconSource READ iconSource CONSTANT)
    Q_PROPERTY(QString frameText READ frameText NOTIFY frameTextChanged)
    Q_PROPERTY(bool maximized READ maximized NOTIFY maximizedChanged)
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    Q_PROPERTY(int titleBarHeight READ titleBarHeight WRITE setTitleBarHeight NOTIFY titleBarHeightChanged)

public:
    explicit WindowChromeController(MainWindow& window, QObject* parent = nullptr);

    [[nodiscard]] QString windowTitle() const;
    [[nodiscard]] QString iconSource() const;
    [[nodiscard]] QString frameText() const;
    [[nodiscard]] bool maximized() const;
    [[nodiscard]] bool active() const;
    [[nodiscard]] int titleBarHeight() const;
    void setTitleBarHeight(int height);
    void setFrameText(const QString& text);

    Q_INVOKABLE void minimize();
    Q_INVOKABLE void toggleMaximized();
    Q_INVOKABLE void closeWindow();
    Q_INVOKABLE void startSystemMove();
    Q_INVOKABLE void startSystemResize(int edge);
    Q_INVOKABLE void showSystemMenu(int globalX, int globalY);

signals:
    void windowTitleChanged();
    void frameTextChanged();
    void maximizedChanged();
    void activeChanged();
    void titleBarHeightChanged();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    MainWindow& m_window;
    int m_titleBarHeight = 44;
    QString m_frameText = QStringLiteral("Frame 0  |  0.00 s");
};
