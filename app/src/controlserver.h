#ifndef CONTROLSERVER_H
#define CONTROLSERVER_H

#include <QObject>
#include <QString>
#include <QStringList>

#include "bluezmanager.h"
#include "callmonitor.h"
#include "gattserver.h"
#include "notificationbridge.h"
#include "watchhealth.h"

class QTimer;
class QDBusServer;
class QDBusConnection;

class WristlinkCore : public QObject
{
    Q_OBJECT

public:
    explicit WristlinkCore(QObject *parent = 0);

    QString stateJson() const;

    BluezManager *bluez() { return &m_bluez; }
    WatchHealth *health() { return &m_health; }
    GattServer *gatt() { return &m_gatt; }
    void restartLink();
    NotificationBridge *notifications() { return &m_notifications; }

signals:
    void changed();

private:
    BluezManager m_bluez;
    WatchHealth m_health;
    NotificationBridge m_notifications;
    GattServer m_gatt;
    CallMonitor m_calls;
};

class ControlApi : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.wristlink.WristLink")

public:
    explicit ControlApi(WristlinkCore *core, QObject *parent = 0);

public slots:
    QString GetState();
    void Refresh();
    void StartDiscovery();
    void StopDiscovery();
    void PairDevice(const QString &path);
    void ConnectDevice(const QString &path);
    void DisconnectDevice(const QString &path);
    void RemoveDevice(const QString &path);
    void AcceptPairing();
    void RejectPairing();
    void Sync(const QString &path);
    void SetAppEnabled(const QString &app, bool enabled);
    void RestartLink();

signals:
    void StateChanged(const QString &state);

private:
    WristlinkCore *m_core;
};

class ControlServer : public QObject
{
    Q_OBJECT

public:
    explicit ControlServer(WristlinkCore *core, QObject *parent = 0);

private slots:
    void publish();
    void schedule();
    void onNewConnection(const QDBusConnection &connection);

private:
    WristlinkCore *m_core;
    ControlApi *m_api;
    QDBusServer *m_server;
    QTimer *m_timer;
    QStringList m_connections;
    QString m_lastJson;
};

#endif
