#ifndef SOCKETPATH_H
#define SOCKETPATH_H

#include <QDir>
#include <QString>

inline QString wristlinkSocketPath()
{
    // Fixed path so Settings and the background service share one socket.
    // QStandardPaths follows the host process name, which is jolla-settings
    // when this page is open inside Settings.
    const QString dir = QDir::homePath()
            + QStringLiteral("/.local/share/org.wristlink/harbour-wristlink");
    QDir().mkpath(dir);
    return QDir(dir).filePath(QStringLiteral("wristlink.sock"));
}

#endif
