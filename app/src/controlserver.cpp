#include "controlserver.h"

#include "socketpath.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusServer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSettings>
#include <QTimer>

WristlinkCore::WristlinkCore(QObject *parent)
    : QObject(parent)
{
    connect(&m_bluez, &BluezManager::deviceDisconnected, &m_health, &WatchHealth::stop);
    connect(&m_bluez, &BluezManager::devicesChanged, this, [this]() {
        m_gatt.noteWatch(m_bluez.pairedWatchPath(), m_bluez.watchIsConnected());
    });
    connect(&m_gatt, &GattServer::connectRequested,
            &m_bluez, &BluezManager::ensureConnected);
    connect(&m_gatt, &GattServer::reconnectRequested,
            &m_bluez, &BluezManager::reconnectDevice);
    connect(&m_gatt, &GattServer::syncDue, &m_health, &WatchHealth::sendPhoneBattery);
    connect(&m_health, &WatchHealth::syncRequested, &m_gatt, &GattServer::readMetrics);
    connect(&m_notifications, &NotificationBridge::notificationPosted,
            &m_health, &WatchHealth::forwardNotification);
    connect(&m_notifications, &NotificationBridge::notificationWithdrawn,
            &m_health, &WatchHealth::withdrawNotification);
    connect(&m_gatt, &GattServer::metricsReceived, &m_health, &WatchHealth::onWatchMetrics);
    connect(&m_gatt, &GattServer::statusChanged, &m_health, [this]() {
        m_health.onGattStatus(m_gatt.status());
    });
    connect(&m_health, &WatchHealth::alertOutgoing, &m_gatt, &GattServer::sendAlert);
    connect(&m_notifications, &NotificationBridge::callRinging,
            &m_calls, &CallMonitor::setIncoming);
    connect(&m_calls, &CallMonitor::callAlert, &m_health, &WatchHealth::forwardCall);
    connect(&m_gatt, &GattServer::alertsSubscribed, &m_health, &WatchHealth::sendPhoneBattery);
    m_health.onGattStatus(m_gatt.status());

    connect(&m_bluez, SIGNAL(devicesChanged()), this, SIGNAL(changed()));
    connect(&m_bluez, SIGNAL(discoveringChanged()), this, SIGNAL(changed()));
    connect(&m_bluez, SIGNAL(bluetoothAvailableChanged()), this, SIGNAL(changed()));
    connect(&m_bluez, SIGNAL(busyChanged()), this, SIGNAL(changed()));
    connect(&m_bluez, SIGNAL(statusMessageChanged()), this, SIGNAL(changed()));
    connect(&m_bluez, SIGNAL(agentStatusChanged()), this, SIGNAL(changed()));
    connect(&m_bluez, SIGNAL(confirmationRequested()), this, SIGNAL(changed()));
    connect(&m_bluez, SIGNAL(deviceDisconnected()), this, SIGNAL(changed()));
    connect(&m_health, SIGNAL(metricsChanged()), this, SIGNAL(changed()));
    connect(&m_health, SIGNAL(syncingChanged()), this, SIGNAL(changed()));
    connect(&m_health, SIGNAL(statusChanged()), this, SIGNAL(changed()));
    connect(&m_health, SIGNAL(lastErrorChanged()), this, SIGNAL(changed()));
    connect(&m_notifications, SIGNAL(statusChanged()), this, SIGNAL(changed()));
    connect(&m_notifications, SIGNAL(appsChanged()), this, SIGNAL(changed()));
    connect(&m_notifications, SIGNAL(preferencesChanged()), this, SIGNAL(changed()));
    connect(&m_gatt, SIGNAL(statusChanged()), this, SIGNAL(changed()));
    connect(&m_gatt, SIGNAL(detailChanged()), this, SIGNAL(changed()));
    connect(&m_gatt, SIGNAL(watchLinkedChanged()), this, SIGNAL(changed()));
    connect(&m_calls, SIGNAL(statusChanged()), this, SIGNAL(changed()));
    connect(&m_calls, SIGNAL(ringingChanged()), this, SIGNAL(changed()));
    m_gatt.noteWatch(m_bluez.pairedWatchPath(), m_bluez.watchIsConnected());
}

QString WristlinkCore::stateJson() const
{
    QJsonObject bluez;
    bluez.insert(QStringLiteral("devices"), QJsonArray::fromVariantList(m_bluez.devices()));
    bluez.insert(QStringLiteral("discovering"), m_bluez.discovering());
    bluez.insert(QStringLiteral("bluetoothAvailable"), m_bluez.bluetoothAvailable());
    bluez.insert(QStringLiteral("busy"), m_bluez.busy());
    bluez.insert(QStringLiteral("statusMessage"), m_bluez.statusMessage());
    bluez.insert(QStringLiteral("confirmationCode"), m_bluez.confirmationCode());
    bluez.insert(QStringLiteral("confirmationDevice"), m_bluez.confirmationDevice());
    bluez.insert(QStringLiteral("agentStatus"), m_bluez.agentStatus());
    bluez.insert(QStringLiteral("agentReady"), m_bluez.agentReady());

    QJsonObject health;
    health.insert(QStringLiteral("steps"), m_health.steps());
    health.insert(QStringLiteral("calories"), m_health.calories());
    health.insert(QStringLiteral("distanceMeters"), m_health.distanceMeters());
    health.insert(QStringLiteral("heartRate"), m_health.heartRate());
    health.insert(QStringLiteral("floors"), m_health.floors());
    health.insert(QStringLiteral("batteryPercent"), m_health.batteryPercent());
    health.insert(QStringLiteral("stepsValid"), m_health.stepsValid());
    health.insert(QStringLiteral("caloriesValid"), m_health.caloriesValid());
    health.insert(QStringLiteral("distanceValid"), m_health.distanceValid());
    health.insert(QStringLiteral("heartRateValid"), m_health.heartRateValid());
    health.insert(QStringLiteral("floorsValid"), m_health.floorsValid());
    health.insert(QStringLiteral("batteryValid"), m_health.batteryValid());
    health.insert(QStringLiteral("syncing"), m_health.syncing());
    health.insert(QStringLiteral("lastUpdated"), m_health.lastUpdated());
    health.insert(QStringLiteral("status"), m_health.status());
    health.insert(QStringLiteral("lastError"), m_health.lastError());

    QJsonObject gatt;
    gatt.insert(QStringLiteral("ready"), m_gatt.ready());
    gatt.insert(QStringLiteral("status"), m_gatt.status());
    gatt.insert(QStringLiteral("detail"), m_gatt.detail());
    gatt.insert(QStringLiteral("watchLinked"), m_gatt.watchLinked());

    QJsonObject calls;
    calls.insert(QStringLiteral("status"), m_calls.status());
    calls.insert(QStringLiteral("ringing"), m_calls.ringing());
    calls.insert(QStringLiteral("caller"), m_calls.caller());

    QJsonObject notifications;
    notifications.insert(QStringLiteral("status"), m_notifications.status());
    notifications.insert(QStringLiteral("ready"), m_notifications.ready());
    notifications.insert(QStringLiteral("apps"), QJsonArray::fromVariantList(m_notifications.apps()));
    notifications.insert(QStringLiteral("groupByApp"), m_notifications.groupByApp());
    notifications.insert(QStringLiteral("skipWhenUnlocked"), m_notifications.skipWhenUnlocked());

    QJsonObject service;
    QSettings settings(wristlinkSettingsPath(), QSettings::IniFormat);
    service.insert(QStringLiteral("enabled"),
                   settings.value(QStringLiteral("service/enabled"), true).toBool());

    QJsonObject root;
    root.insert(QStringLiteral("bluez"), bluez);
    root.insert(QStringLiteral("health"), health);
    root.insert(QStringLiteral("gatt"), gatt);
    root.insert(QStringLiteral("calls"), calls);
    root.insert(QStringLiteral("notifications"), notifications);
    root.insert(QStringLiteral("service"), service);
    return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

void WristlinkCore::restartLink()
{
    m_gatt.restart();
    m_bluez.refresh();
}

ControlApi::ControlApi(WristlinkCore *core, QObject *parent)
    : QObject(parent)
    , m_core(core)
{
}

QString ControlApi::GetState()
{
    return m_core->stateJson();
}

void ControlApi::Refresh()
{
    m_core->bluez()->refresh();
}

void ControlApi::StartDiscovery()
{
    m_core->bluez()->startDiscovery();
}

void ControlApi::StopDiscovery()
{
    m_core->bluez()->stopDiscovery();
}

void ControlApi::PairDevice(const QString &path)
{
    m_core->bluez()->pairDevice(path);
}

void ControlApi::ConnectDevice(const QString &path)
{
    m_core->gatt()->holdLink(false);
    m_core->bluez()->connectDevice(path);
}

void ControlApi::DisconnectDevice(const QString &path)
{
    m_core->gatt()->holdLink(true);
    m_core->bluez()->disconnectDevice(path);
}

void ControlApi::RemoveDevice(const QString &path)
{
    m_core->bluez()->removeDevice(path);
}

void ControlApi::AcceptPairing()
{
    m_core->bluez()->acceptPairing();
}

void ControlApi::RejectPairing()
{
    m_core->bluez()->rejectPairing();
}

void ControlApi::Sync(const QString &path)
{
    m_core->health()->sync(path);
}

void ControlApi::SetAppEnabled(const QString &app, bool enabled)
{
    m_core->notifications()->setAppEnabled(app, enabled);
}

void ControlApi::SetGroupByApp(bool enabled)
{
    m_core->notifications()->setGroupByApp(enabled);
}

void ControlApi::SetSkipWhenUnlocked(bool enabled)
{
    m_core->notifications()->setSkipWhenUnlocked(enabled);
}

void ControlApi::SetServiceEnabled(bool enabled)
{
    QSettings settings(wristlinkSettingsPath(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("service/enabled"), enabled);
    settings.sync();
    if (enabled) {
        QProcess::startDetached(QStringLiteral("systemctl"),
                                QStringList() << QStringLiteral("--user")
                                              << QStringLiteral("enable")
                                              << QStringLiteral("--now")
                                              << QStringLiteral("harbour-wristlink.service"));
        return;
    }
    QProcess::startDetached(QStringLiteral("systemctl"),
                            QStringList() << QStringLiteral("--user")
                                          << QStringLiteral("disable")
                                          << QStringLiteral("--now")
                                          << QStringLiteral("harbour-wristlink.service"));
    QTimer::singleShot(0, qApp, SLOT(quit()));
}

void ControlApi::RestartLink()
{
    m_core->restartLink();
}

ControlServer::ControlServer(WristlinkCore *core, QObject *parent)
    : QObject(parent)
    , m_core(core)
    , m_api(new ControlApi(core, this))
    , m_server(0)
    , m_timer(new QTimer(this))
{
    m_timer->setSingleShot(true);
    m_timer->setInterval(50);
    connect(m_timer, SIGNAL(timeout()), this, SLOT(publish()));
    connect(core, SIGNAL(changed()), this, SLOT(schedule()));

    const QString path = wristlinkSocketPath();
    QFile::remove(path);
    m_server = new QDBusServer(QStringLiteral("unix:path=") + path, this);
    if (!m_server->isConnected())
        return;
    connect(m_server, &QDBusServer::newConnection,
            this, &ControlServer::onNewConnection);
    schedule();
}

void ControlServer::schedule()
{
    if (!m_timer->isActive())
        m_timer->start();
}

void ControlServer::publish()
{
    const QString json = m_core->stateJson();
    if (json == m_lastJson)
        return;
    m_lastJson = json;

    QStringList live;
    foreach (const QString &name, m_connections) {
        QDBusConnection connection(name);
        if (!connection.isConnected())
            continue;
        live.append(name);
        QDBusMessage message = QDBusMessage::createSignal(
                    QStringLiteral("/org/wristlink"),
                    QStringLiteral("org.wristlink.WristLink"),
                    QStringLiteral("StateChanged"));
        message << json;
        connection.send(message);
    }
    m_connections = live;
}

void ControlServer::onNewConnection(const QDBusConnection &connection)
{
    QDBusConnection link(connection);
    link.registerObject(QStringLiteral("/org/wristlink"), m_api,
                        QDBusConnection::ExportAllSlots
                        | QDBusConnection::ExportAllSignals);
    if (!m_connections.contains(link.name()))
        m_connections.append(link.name());
    schedule();
}
