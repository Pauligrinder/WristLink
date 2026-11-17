#include "controlclient.h"

#include "socketpath.h"

#include <QDBusMessage>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QSettings>
#include <QTimer>

static bool setString(QString *target, const QString &value)
{
    if (*target == value)
        return false;
    *target = value;
    return true;
}

static bool setBool(bool *target, bool value)
{
    if (*target == value)
        return false;
    *target = value;
    return true;
}

static bool setInt(int *target, int value)
{
    if (*target == value)
        return false;
    *target = value;
    return true;
}

BluezFacade::BluezFacade(ControlClient *client, QObject *parent)
    : QObject(parent)
    , m_client(client)
    , m_discovering(false)
    , m_bluetoothAvailable(false)
    , m_busy(false)
    , m_agentReady(false)
{
}

void BluezFacade::refresh() { m_client->call(QStringLiteral("Refresh")); }
void BluezFacade::startDiscovery() { m_client->call(QStringLiteral("StartDiscovery")); }
void BluezFacade::stopDiscovery() { m_client->call(QStringLiteral("StopDiscovery")); }
void BluezFacade::pairDevice(const QString &path) { m_client->call(QStringLiteral("PairDevice"), path); }
void BluezFacade::connectDevice(const QString &path) { m_client->call(QStringLiteral("ConnectDevice"), path); }
void BluezFacade::disconnectDevice(const QString &path) { m_client->call(QStringLiteral("DisconnectDevice"), path); }
void BluezFacade::removeDevice(const QString &path) { m_client->call(QStringLiteral("RemoveDevice"), path); }
void BluezFacade::acceptPairing() { m_client->call(QStringLiteral("AcceptPairing")); }
void BluezFacade::rejectPairing() { m_client->call(QStringLiteral("RejectPairing")); }

void BluezFacade::apply(const QJsonObject &state)
{
    const QString devicesJson = QString::fromUtf8(
                QJsonDocument(state.value(QStringLiteral("devices")).toArray())
                .toJson(QJsonDocument::Compact));
    if (devicesJson != m_devicesJson) {
        m_devicesJson = devicesJson;
        m_devices = state.value(QStringLiteral("devices")).toVariant().toList();
        emit devicesChanged();
    }
    if (setBool(&m_discovering, state.value(QStringLiteral("discovering")).toBool()))
        emit discoveringChanged();
    if (setBool(&m_bluetoothAvailable, state.value(QStringLiteral("bluetoothAvailable")).toBool()))
        emit bluetoothAvailableChanged();
    if (setBool(&m_busy, state.value(QStringLiteral("busy")).toBool()))
        emit busyChanged();
    if (setString(&m_statusMessage, state.value(QStringLiteral("statusMessage")).toString()))
        emit statusMessageChanged();
    const bool agentChanged =
            setString(&m_agentStatus, state.value(QStringLiteral("agentStatus")).toString())
            | setBool(&m_agentReady, state.value(QStringLiteral("agentReady")).toBool());
    if (agentChanged)
        emit agentStatusChanged();

    const QString code = state.value(QStringLiteral("confirmationCode")).toString();
    const QString device = state.value(QStringLiteral("confirmationDevice")).toString();
    const bool confirmationChanged = code != m_confirmationCode || device != m_confirmationDevice;
    m_confirmationCode = code;
    m_confirmationDevice = device;
    if (confirmationChanged && !code.isEmpty())
        emit confirmationRequested();
}

HealthFacade::HealthFacade(ControlClient *client, QObject *parent)
    : QObject(parent)
    , m_client(client)
    , m_status(QStringLiteral("Waiting for the WristLink service…"))
    , m_steps(0)
    , m_calories(0)
    , m_distanceMeters(0)
    , m_heartRate(0)
    , m_floors(0)
    , m_batteryPercent(0)
    , m_stepsValid(false)
    , m_caloriesValid(false)
    , m_distanceValid(false)
    , m_heartRateValid(false)
    , m_floorsValid(false)
    , m_batteryValid(false)
    , m_syncing(false)
{
}

void HealthFacade::sync(const QString &path)
{
    m_client->call(QStringLiteral("Sync"), path);
}

void HealthFacade::apply(const QJsonObject &state)
{
    bool metrics = false;
    metrics |= setInt(&m_steps, state.value(QStringLiteral("steps")).toInt());
    metrics |= setInt(&m_calories, state.value(QStringLiteral("calories")).toInt());
    metrics |= setInt(&m_distanceMeters, state.value(QStringLiteral("distanceMeters")).toInt());
    metrics |= setInt(&m_heartRate, state.value(QStringLiteral("heartRate")).toInt());
    metrics |= setInt(&m_floors, state.value(QStringLiteral("floors")).toInt());
    metrics |= setInt(&m_batteryPercent, state.value(QStringLiteral("batteryPercent")).toInt());
    metrics |= setBool(&m_stepsValid, state.value(QStringLiteral("stepsValid")).toBool());
    metrics |= setBool(&m_caloriesValid, state.value(QStringLiteral("caloriesValid")).toBool());
    metrics |= setBool(&m_distanceValid, state.value(QStringLiteral("distanceValid")).toBool());
    metrics |= setBool(&m_heartRateValid, state.value(QStringLiteral("heartRateValid")).toBool());
    metrics |= setBool(&m_floorsValid, state.value(QStringLiteral("floorsValid")).toBool());
    metrics |= setBool(&m_batteryValid, state.value(QStringLiteral("batteryValid")).toBool());
    metrics |= setString(&m_lastUpdated, state.value(QStringLiteral("lastUpdated")).toString());
    if (metrics)
        emit metricsChanged();
    if (setBool(&m_syncing, state.value(QStringLiteral("syncing")).toBool()))
        emit syncingChanged();
    if (setString(&m_status, state.value(QStringLiteral("status")).toString()))
        emit statusChanged();
    if (setString(&m_lastError, state.value(QStringLiteral("lastError")).toString()))
        emit lastErrorChanged();
}

GattFacade::GattFacade(QObject *parent)
    : QObject(parent)
    , m_status(QStringLiteral("Waiting for the WristLink service…"))
    , m_ready(false)
    , m_watchLinked(false)
{
}

void GattFacade::apply(const QJsonObject &state)
{
    const bool ready = state.value(QStringLiteral("ready")).toBool();
    const QString status = state.value(QStringLiteral("status")).toString();
    if (ready != m_ready || status != m_status) {
        m_ready = ready;
        m_status = status;
        emit statusChanged();
    }
    if (setString(&m_detail, state.value(QStringLiteral("detail")).toString()))
        emit detailChanged();
    if (setBool(&m_watchLinked, state.value(QStringLiteral("watchLinked")).toBool()))
        emit watchLinkedChanged();
}

void GattFacade::setOffline(const QString &status)
{
    if (m_watchLinked) {
        m_watchLinked = false;
        emit watchLinkedChanged();
    }
    if (m_ready || m_status != status) {
        m_ready = false;
        m_status = status;
        emit statusChanged();
    }
}

CallFacade::CallFacade(QObject *parent)
    : QObject(parent)
    , m_status(QStringLiteral("Watch will ring when the phone rings"))
    , m_ringing(false)
{
}

void CallFacade::apply(const QJsonObject &state)
{
    if (setString(&m_status, state.value(QStringLiteral("status")).toString()))
        emit statusChanged();
    if (setBool(&m_ringing, state.value(QStringLiteral("ringing")).toBool()))
        emit ringingChanged();
}

NotificationFacade::NotificationFacade(ControlClient *client, QObject *parent)
    : QObject(parent)
    , m_client(client)
    , m_status(QStringLiteral("Waiting for the WristLink service…"))
    , m_ready(false)
    , m_groupByApp(false)
    , m_skipWhenUnlocked(false)
{
    loadLocalPreferences();
}

void NotificationFacade::loadLocalPreferences()
{
    QSettings settings(wristlinkSettingsPath(), QSettings::IniFormat);
    m_groupByApp = settings.value(QStringLiteral("notifications/groupByApp"), false).toBool();
    m_skipWhenUnlocked =
            settings.value(QStringLiteral("notifications/skipWhenUnlocked"), false).toBool();
}

void NotificationFacade::setAppEnabled(const QString &app, bool enabled)
{
    m_client->call(QStringLiteral("SetAppEnabled"), app, QVariant(enabled));
}

void NotificationFacade::setGroupByApp(bool enabled)
{
    if (m_groupByApp == enabled)
        return;
    m_groupByApp = enabled;
    QSettings settings(wristlinkSettingsPath(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("notifications/groupByApp"), enabled);
    settings.sync();
    m_client->call(QStringLiteral("SetGroupByApp"), QVariant(enabled));
    emit preferencesChanged();
}

void NotificationFacade::setSkipWhenUnlocked(bool enabled)
{
    if (m_skipWhenUnlocked == enabled)
        return;
    m_skipWhenUnlocked = enabled;
    QSettings settings(wristlinkSettingsPath(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("notifications/skipWhenUnlocked"), enabled);
    settings.sync();
    m_client->call(QStringLiteral("SetSkipWhenUnlocked"), QVariant(enabled));
    emit preferencesChanged();
}

void NotificationFacade::apply(const QJsonObject &state)
{
    const QString appsJson = QString::fromUtf8(
                QJsonDocument(state.value(QStringLiteral("apps")).toArray())
                .toJson(QJsonDocument::Compact));
    if (appsJson != m_appsJson) {
        m_appsJson = appsJson;
        m_apps = state.value(QStringLiteral("apps")).toVariant().toList();
        emit appsChanged();
    }
    const bool ready = state.value(QStringLiteral("ready")).toBool();
    const QString status = state.value(QStringLiteral("status")).toString();
    if (ready != m_ready || status != m_status) {
        m_ready = ready;
        m_status = status;
        emit statusChanged();
    }
    bool prefs = false;
    prefs |= setBool(&m_groupByApp, state.value(QStringLiteral("groupByApp")).toBool());
    prefs |= setBool(&m_skipWhenUnlocked, state.value(QStringLiteral("skipWhenUnlocked")).toBool());
    if (prefs)
        emit preferencesChanged();
}

ControlClient::ControlClient(QObject *parent)
    : QObject(parent)
    , m_bluez(new BluezFacade(this, this))
    , m_health(new HealthFacade(this, this))
    , m_gatt(new GattFacade(this))
    , m_calls(new CallFacade(this))
    , m_notifications(new NotificationFacade(this, this))
    , m_retry(new QTimer(this))
    , m_connection(QStringLiteral("wristlink-none"))
    , m_connected(false)
    , m_serviceEnabled(true)
    , m_status(QStringLiteral("Starting the WristLink service…"))
{
    m_gatt->setParent(this);
    m_calls->setParent(this);
    loadServiceEnabled();
    m_retry->setInterval(2000);
    connect(m_retry, SIGNAL(timeout()), this, SLOT(tryConnect()));
    if (m_serviceEnabled)
        m_retry->start();
    QTimer::singleShot(0, this, SLOT(tryConnect()));
}

void ControlClient::loadServiceEnabled()
{
    QSettings settings(wristlinkSettingsPath(), QSettings::IniFormat);
    m_serviceEnabled = settings.value(QStringLiteral("service/enabled"), true).toBool();
}

void ControlClient::writeServiceEnabled(bool enabled)
{
    QSettings settings(wristlinkSettingsPath(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("service/enabled"), enabled);
    settings.sync();
}

void ControlClient::applyServiceUnit(bool enabled)
{
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
}

void ControlClient::restartLink()
{
    call(QStringLiteral("RestartLink"));
}

void ControlClient::setServiceEnabled(bool enabled)
{
    if (m_serviceEnabled == enabled)
        return;
    m_serviceEnabled = enabled;
    writeServiceEnabled(enabled);
    if (m_connected)
        call(QStringLiteral("SetServiceEnabled"), QVariant(enabled));
    else
        applyServiceUnit(enabled);
    if (enabled) {
        if (!m_retry->isActive())
            m_retry->start();
        QTimer::singleShot(0, this, SLOT(tryConnect()));
    } else {
        m_retry->stop();
        setConnected(false, QStringLiteral("WristLink service is disabled"));
    }
    emit serviceEnabledChanged();
}

void ControlClient::uninstall()
{
    writeServiceEnabled(false);
    m_serviceEnabled = false;
    emit serviceEnabledChanged();
    m_retry->stop();
    applyServiceUnit(false);
    QProcess::startDetached(QStringLiteral("pkcon"),
                            QStringList() << QStringLiteral("-y")
                                          << QStringLiteral("remove")
                                          << QStringLiteral("harbour-wristlink"));
}

void ControlClient::call(const QString &method, const QVariant &argument)
{
    QVariantList arguments;
    if (argument.isValid())
        arguments << argument;
    call(method, arguments.isEmpty() ? QVariant() : arguments.first(), QVariant());
}

void ControlClient::call(const QString &method, const QVariant &first, const QVariant &second)
{
    if (!m_connection.isConnected())
        return;
    QDBusMessage message = QDBusMessage::createMethodCall(
                QString(),
                QStringLiteral("/org/wristlink"),
                QStringLiteral("org.wristlink.WristLink"),
                method);
    if (first.isValid())
        message << first;
    if (second.isValid())
        message << second;
    m_connection.call(message, QDBus::NoBlock);
}

void ControlClient::tryConnect()
{
    if (!m_serviceEnabled) {
        setConnected(false, QStringLiteral("WristLink service is disabled"));
        return;
    }
    if (m_connection.isConnected())
        return;
    const QString path = wristlinkSocketPath();
    if (!QFile::exists(path)) {
        setConnected(false, QStringLiteral("WristLink service is not running"));
        return;
    }
    QDBusConnection::disconnectFromPeer(QStringLiteral("wristlink"));
    m_connection = QDBusConnection::connectToPeer(QStringLiteral("unix:path=") + path,
                                                  QStringLiteral("wristlink"));
    if (!m_connection.isConnected()) {
        setConnected(false, QStringLiteral("WristLink service is not running"));
        return;
    }
    m_connection.connect(QString(),
                         QStringLiteral("/org/wristlink"),
                         QStringLiteral("org.wristlink.WristLink"),
                         QStringLiteral("StateChanged"),
                         this, SLOT(onStateChanged(QString)));
    QDBusMessage message = QDBusMessage::createMethodCall(
                QString(),
                QStringLiteral("/org/wristlink"),
                QStringLiteral("org.wristlink.WristLink"),
                QStringLiteral("GetState"));
    const QDBusMessage reply = m_connection.call(message, QDBus::Block, 2000);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        setConnected(false, QStringLiteral("WristLink service did not answer"));
        QDBusConnection::disconnectFromPeer(QStringLiteral("wristlink"));
        m_connection = QDBusConnection(QStringLiteral("wristlink-none"));
        return;
    }
    applyState(reply.arguments().at(0).toString());
    setConnected(true, QStringLiteral("WristLink service is running"));
}

void ControlClient::onStateChanged(const QString &json)
{
    applyState(json);
    setConnected(true, QStringLiteral("WristLink service is running"));
}

void ControlClient::applyState(const QString &json)
{
    const QJsonObject root = QJsonDocument::fromJson(json.toUtf8()).object();
    m_bluez->apply(root.value(QStringLiteral("bluez")).toObject());
    m_health->apply(root.value(QStringLiteral("health")).toObject());
    m_gatt->apply(root.value(QStringLiteral("gatt")).toObject());
    m_calls->apply(root.value(QStringLiteral("calls")).toObject());
    m_notifications->apply(root.value(QStringLiteral("notifications")).toObject());
    const QJsonObject service = root.value(QStringLiteral("service")).toObject();
    if (service.contains(QStringLiteral("enabled"))) {
        const bool enabled = service.value(QStringLiteral("enabled")).toBool();
        if (setBool(&m_serviceEnabled, enabled))
            emit serviceEnabledChanged();
    }
}

void ControlClient::setConnected(bool connected, const QString &status)
{
    if (m_connected == connected && m_status == status)
        return;
    m_connected = connected;
    m_status = status;
    if (!connected)
        m_gatt->setOffline(status);
    emit connectedChanged();
}
