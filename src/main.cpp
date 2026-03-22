#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QMutex>
#include <QMutexLocker>
#include <QPalette>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QRect>
#include <QSettings>
#include <QSize>
#include <QStyleFactory>
#include <QTextStream>

#ifdef Q_OS_WIN
#include <windows.h>
#include <shlobj.h>
#endif

#include "app/MainWindow.h"
#include "app/ProjectDocument.h"

namespace
{
QString& runtimeLogPath()
{
    static QString path;
    return path;
}

QMutex& runtimeLogMutex()
{
    static QMutex mutex;
    return mutex;
}

void runtimeMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    const auto path = runtimeLogPath();
    if (!path.isEmpty())
    {
        QMutexLocker locker(&runtimeLogMutex());
        QFile file(path);
        if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        {
            QTextStream stream(&file);
            QString level;
            switch (type)
            {
            case QtDebugMsg:
                level = QStringLiteral("DEBUG");
                break;
            case QtInfoMsg:
                level = QStringLiteral("INFO");
                break;
            case QtWarningMsg:
                level = QStringLiteral("WARN");
                break;
            case QtCriticalMsg:
                level = QStringLiteral("ERROR");
                break;
            case QtFatalMsg:
                level = QStringLiteral("FATAL");
                break;
            }

            stream << QDateTime::currentDateTime().toString(Qt::ISODateWithMs)
                   << ' ' << level;
            if (context.file && *context.file)
            {
                stream << ' ' << context.file;
                if (context.line > 0)
                {
                    stream << ':' << context.line;
                }
            }
            stream << " | " << message << '\n';
        }
    }

    const auto local = message.toLocal8Bit();
    fprintf(stderr, "%s\n", local.constData());
    fflush(stderr);

    if (type == QtFatalMsg)
    {
        abort();
    }
}

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
#ifdef Q_OS_WIN
    QQuickWindow::setGraphicsApi(QSGRendererInterface::Direct3D11);
#endif
    QQuickStyle::setStyle(QStringLiteral("FluentWinUI3"));
    QApplication app(argc, argv);
    runtimeLogPath() = QDir(app.applicationDirPath()).filePath(QStringLiteral("dawg-runtime.log"));
    QFile::remove(runtimeLogPath());
    qInstallMessageHandler(runtimeMessageHandler);
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

    const auto arguments = QCoreApplication::arguments();
    const bool skipStartupRestore = arguments.contains(QStringLiteral("--no-startup-restore"));
    QString startupProjectPath;
    if (arguments.size() > 1)
    {
        for (int index = 1; index < arguments.size(); ++index)
        {
            if (arguments.at(index) == QStringLiteral("--no-startup-restore"))
            {
                continue;
            }

            const QFileInfo candidatePath(arguments.at(index));
            if (candidatePath.exists()
                && candidatePath.isFile()
                && candidatePath.suffix().compare(
                    QString::fromLatin1(dawg::project::kProjectFileSuffix).mid(1),
                    Qt::CaseInsensitive) == 0)
            {
                startupProjectPath = candidatePath.absoluteFilePath();
            }
            break;
        }
    }

    MainWindow window;
    QObject::connect(&app, &QGuiApplication::lastWindowClosed, []()
    {
        qInfo().noquote() << "QGuiApplication::lastWindowClosed";
    });
    QObject::connect(&app, &QCoreApplication::aboutToQuit, []()
    {
        qInfo().noquote() << "QCoreApplication::aboutToQuit";
    });
    QObject::connect(&window, &QWindow::visibleChanged, [&window]()
    {
        qInfo().noquote()
            << "Main window visibleChanged:"
            << "visible=" << window.isVisible()
            << "visibility=" << window.visibility()
            << "geometry=" << window.geometry();
    });
    QObject::connect(&window, &QWindow::visibilityChanged, [&window](const QWindow::Visibility visibility)
    {
        qInfo().noquote()
            << "Main window visibilityChanged:"
            << "visible=" << window.isVisible()
            << "visibility=" << visibility
            << "geometry=" << window.geometry();
    });
    window.create();
    window.show();
    window.raise();
    qInfo().noquote()
        << "Main window shown:"
        << "visible=" << window.isVisible()
        << "geometry=" << window.geometry()
        << "visibility=" << window.visibility();
    if (!window.isMaximized()
        && (window.width() < std::max(1000, window.minimumWidth())
            || window.height() < std::max(700, window.minimumHeight())))
    {
        const QSize targetSize{
            std::max(1400, window.minimumWidth()),
            std::max(900, window.minimumHeight())
        };
        window.resize(targetSize);
        const QRect availableGeometry = window.screen()
            ? window.screen()->availableGeometry()
            : QRect{0, 0, 1600, 900};
        QRect nextGeometry(window.position(), targetSize);
        nextGeometry.moveCenter(availableGeometry.center());
        window.setGeometry(nextGeometry);
    }
    window.requestActivate();
    qInfo().noquote()
        << "Main window activated:"
        << "visible=" << window.isVisible()
        << "geometry=" << window.geometry()
        << "visibility=" << window.visibility();

    if (!startupProjectPath.isEmpty())
    {
        qInfo().noquote() << "Startup main: opening explicit project path:" << startupProjectPath;
        static_cast<void>(window.openProjectFilePath(startupProjectPath));
        qInfo().noquote() << "Startup main: explicit project open returned.";
    }
    else if (!skipStartupRestore)
    {
        qInfo().noquote() << "Startup main: restoring last project.";
        window.restoreLastProjectOnStartup();
        qInfo().noquote() << "Startup main: last project restore returned.";
    }

    return app.exec();
}
