#ifndef SOCKETPATH_H
#define SOCKETPATH_H

#include <QDir>
#include <QString>

inline QString wristlinkDataDir()
{
    // Fixed path so Settings and the background service share one location.
    // QStandardPaths follows the host process name, which is jolla-settings
    // when this page is open inside Settings.
    const QString dir = QDir::homePath()
            + QStringLiteral("/.local/share/org.wristlink/harbour-wristlink");
    QDir().mkpath(dir);
    return dir;
}

inline QString wristlinkConfigDir()
{
    const QString dir = QDir::homePath()
            + QStringLiteral("/.config/org.wristlink/harbour-wristlink");
    QDir().mkpath(dir);
    return dir;
}

inline QString wristlinkSocketPath()
{
    return QDir(wristlinkDataDir()).filePath(QStringLiteral("wristlink.sock"));
}

inline QString wristlinkSettingsPath()
{
    return QDir(wristlinkConfigDir()).filePath(QStringLiteral("settings.ini"));
}

#endif
