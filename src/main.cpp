#include <QApplication>
#include <QIcon>

#include "app/MainWindow.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("dawg");
    app.setOrganizationName("Daniel Medin");
    app.setWindowIcon(QIcon(QStringLiteral(":/branding/logo.png")));

    MainWindow window;
    window.show();

    return app.exec();
}
