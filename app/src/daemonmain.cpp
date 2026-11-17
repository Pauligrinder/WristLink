#include <QCoreApplication>

#include "controlserver.h"

int main(int argc, char *argv[])
{
    QCoreApplication::setOrganizationName(QStringLiteral("org.wristlink"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("org.wristlink"));
    QCoreApplication::setApplicationName(QStringLiteral("harbour-wristlink"));

    QCoreApplication app(argc, argv);
    WristlinkCore core;
    ControlServer server(&core);
    Q_UNUSED(server)
    return app.exec();
}
