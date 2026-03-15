#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QPalette>
#include <QSettings>
#include <QStyleFactory>

#ifdef Q_OS_WIN
#include <windows.h>
#include <shlobj.h>
#endif

#include "app/MainWindow.h"
#include "app/ProjectDocument.h"

namespace
{
#ifdef Q_OS_WIN
void registerDawgFileAssociation()
{
    const auto executablePath = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    if (executablePath.isEmpty())
    {
        return;
    }

    constexpr auto kProgId = "dawg.project";
    const auto progId = QString::fromLatin1(kProgId);
    const auto commandValue = QStringLiteral("\"%1\" \"%2\"").arg(executablePath, QStringLiteral("%1"));
    const auto iconValue = QStringLiteral("\"%1\",0").arg(executablePath);

    QSettings extensionKey(QStringLiteral("HKEY_CURRENT_USER\\Software\\Classes\\.dawg"), QSettings::NativeFormat);
    extensionKey.setValue(QStringLiteral("."), progId);

    QSettings progIdKey(
        QStringLiteral("HKEY_CURRENT_USER\\Software\\Classes\\%1").arg(progId),
        QSettings::NativeFormat);
    progIdKey.setValue(QStringLiteral("."), QStringLiteral("DAWG Project"));

    QSettings defaultIconKey(
        QStringLiteral("HKEY_CURRENT_USER\\Software\\Classes\\%1\\DefaultIcon").arg(progId),
        QSettings::NativeFormat);
    defaultIconKey.setValue(QStringLiteral("."), iconValue);

    QSettings openCommandKey(
        QStringLiteral("HKEY_CURRENT_USER\\Software\\Classes\\%1\\shell\\open\\command").arg(progId),
        QSettings::NativeFormat);
    openCommandKey.setValue(QStringLiteral("."), commandValue);

    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}
#endif
}

int main(int argc, char* argv[])
{
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    app.setApplicationName("dawg");
    app.setOrganizationName("Daniel Medin");
    app.setWindowIcon(QIcon(QStringLiteral(":/branding/dawg.png")));
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    QPalette palette;
    palette.setColor(QPalette::Window, QColor{15, 20, 27});
    palette.setColor(QPalette::WindowText, QColor{216, 221, 228});
    palette.setColor(QPalette::Base, QColor{10, 13, 18});
    palette.setColor(QPalette::AlternateBase, QColor{18, 23, 32});
    palette.setColor(QPalette::ToolTipBase, QColor{18, 23, 32});
    palette.setColor(QPalette::ToolTipText, QColor{243, 245, 247});
    palette.setColor(QPalette::Text, QColor{238, 242, 246});
    palette.setColor(QPalette::Button, QColor{24, 32, 43});
    palette.setColor(QPalette::ButtonText, QColor{236, 241, 246});
    palette.setColor(QPalette::BrightText, QColor{255, 255, 255});
    palette.setColor(QPalette::Highlight, QColor{34, 49, 70});
    palette.setColor(QPalette::HighlightedText, QColor{243, 245, 247});
    palette.setColor(QPalette::Link, QColor{110, 174, 255});
    palette.setColor(QPalette::PlaceholderText, QColor{132, 145, 160});
    palette.setColor(QPalette::Disabled, QPalette::WindowText, QColor{124, 132, 142});
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor{124, 132, 142});
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor{124, 132, 142});
    palette.setColor(QPalette::Disabled, QPalette::Highlight, QColor{52, 60, 72});
    palette.setColor(QPalette::Disabled, QPalette::HighlightedText, QColor{180, 186, 194});
    app.setPalette(palette);

#ifdef Q_OS_WIN
    registerDawgFileAssociation();
#endif

    MainWindow window;
    window.show();

    const auto arguments = QCoreApplication::arguments();
    if (arguments.size() > 1)
    {
        const QFileInfo candidatePath(arguments.at(1));
        if (candidatePath.exists()
            && candidatePath.isFile()
            && candidatePath.suffix().compare(
                QString::fromLatin1(dawg::project::kProjectFileSuffix).mid(1),
                Qt::CaseInsensitive) == 0)
        {
            static_cast<void>(window.openProjectFilePath(candidatePath.absoluteFilePath()));
        }
    }

    return app.exec();
}
