#include <QCoreApplication>
#include <QSettings>

#include "controlserver.h"
#include "socketpath.h"

int main(int argc, char *argv[])
{
    QCoreApplication::setOrganizationName(QStringLiteral("org.wristlink"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("org.wristlink"));
    QCoreApplication::setApplicationName(QStringLiteral("harbour-wristlink"));

    QSettings settings(wristlinkSettingsPath(), QSettings::IniFormat);
    if (!settings.value(QStringLiteral("service/enabled"), true).toBool())
        return 0;

    QCoreApplication app(argc, argv);
    WristlinkCore core;
    ControlServer server(&core);
    Q_UNUSED(server)
    return app.exec();
}
