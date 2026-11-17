#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlExtensionPlugin>
#include <QtQml>

#include "controlclient.h"

class WristlinkPlugin : public QQmlExtensionPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QQmlExtensionInterface_iid)

public:
    void registerTypes(const char *uri)
    {
        Q_ASSERT(QLatin1String(uri) == QLatin1String("org.wristlink"));
        qmlRegisterUncreatableType<ControlClient>(
                    uri, 1, 0, "WristLinkService",
                    QStringLiteral("WristLinkService is created by the module"));
    }

    void initializeEngine(QQmlEngine *engine, const char *uri)
    {
        Q_UNUSED(uri)
        QQmlContext *context = engine->rootContext();
        if (context->contextProperty(QStringLiteral("wristlink")).isValid())
            return;

        ControlClient *client = new ControlClient(engine);
        context->setContextProperty(QStringLiteral("bluez"), client->bluez());
        context->setContextProperty(QStringLiteral("watchHealth"), client->health());
        context->setContextProperty(QStringLiteral("notificationBridge"), client->notifications());
        context->setContextProperty(QStringLiteral("callMonitor"), client->calls());
        context->setContextProperty(QStringLiteral("gattServer"), client->gatt());
        context->setContextProperty(QStringLiteral("wristlink"), client);
        context->setContextProperty(QStringLiteral("appVersion"), QStringLiteral(APP_VERSION));
    }
};

#include "wristlinkplugin.moc"
