#include <sailfishapp.h>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QQmlContext>
#include <QQuickView>

#include "controlclient.h"

int main(int argc, char *argv[])
{
    QCoreApplication::setOrganizationName(QStringLiteral("org.wristlink"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("org.wristlink"));
    QCoreApplication::setApplicationName(QStringLiteral("harbour-wristlink"));

    QGuiApplication *app = SailfishApp::application(argc, argv);
    ControlClient client;

    QQuickView *view = SailfishApp::createView();
    view->rootContext()->setContextProperty(QStringLiteral("bluez"), client.bluez());
    view->rootContext()->setContextProperty(QStringLiteral("watchHealth"), client.health());
    view->rootContext()->setContextProperty(QStringLiteral("notificationBridge"), client.notifications());
    view->rootContext()->setContextProperty(QStringLiteral("callMonitor"), client.calls());
    view->rootContext()->setContextProperty(QStringLiteral("gattServer"), client.gatt());
    view->rootContext()->setContextProperty(QStringLiteral("wristlink"), &client);
    view->rootContext()->setContextProperty(QStringLiteral("appVersion"),
                                            QStringLiteral(APP_VERSION));
    view->setSource(SailfishApp::pathTo(QStringLiteral("qml/harbour-wristlink.qml")));
    view->show();

    return app->exec();
}
