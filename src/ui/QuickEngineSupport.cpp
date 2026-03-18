#include "ui/QuickEngineSupport.h"

#include <QCoreApplication>
#include <QDir>
#include <QQmlEngine>

void configureQuickEngine(QQmlEngine& engine)
{
    const auto deployedQmlPath = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("qml"));
    if (!engine.importPathList().contains(deployedQmlPath))
    {
        engine.addImportPath(deployedQmlPath);
    }
}
