#include "AppPaths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace AppPaths {

QString rootDir()
{
    return QDir::cleanPath(QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral("../..")));
}

bool isPortable()
{
    return QFileInfo::exists(rootDir() + QStringLiteral("/portable.flag"));
}

QString configDir()
{
    const QString dir = isPortable() ? rootDir() + QStringLiteral("/UserData/config")
                                     : QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    QDir().mkpath(dir);
    return dir;
}

QString logDir()
{
    const QString dir = isPortable() ? rootDir() + QStringLiteral("/UserData/logs")
                                     : QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/logs");
    QDir().mkpath(dir);
    return dir;
}

QString skinsDir()
{
    const QString dir = isPortable() ? rootDir() + QStringLiteral("/UserData/skins")
                                     : configDir() + QStringLiteral("/skins");
    QDir().mkpath(dir);
    return dir;
}

} // namespace AppPaths
