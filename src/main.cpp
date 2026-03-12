#include <QApplication>

#include "app/MainWindow.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("dawg");
    app.setOrganizationName("Daniel Medin");

    MainWindow window;
    window.show();

    return app.exec();
}

