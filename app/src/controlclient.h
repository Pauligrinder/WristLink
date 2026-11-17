#ifndef CONTROLCLIENT_H
#define CONTROLCLIENT_H

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVariantList>
#include <QDBusConnection>

class QTimer;

class ControlClient;

class BluezFacade : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList devices READ devices NOTIFY devicesChanged)
    Q_PROPERTY(bool discovering READ discovering NOTIFY discoveringChanged)
    Q_PROPERTY(bool bluetoothAvailable READ bluetoothAvailable NOTIFY bluetoothAvailableChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(QString confirmationCode READ confirmationCode NOTIFY confirmationRequested)
    Q_PROPERTY(QString confirmationDevice READ confirmationDevice NOTIFY confirmationRequested)
    Q_PROPERTY(QString agentStatus READ agentStatus NOTIFY agentStatusChanged)
    Q_PROPERTY(bool agentReady READ agentReady NOTIFY agentStatusChanged)

public:
    explicit BluezFacade(ControlClient *client, QObject *parent = 0);

    QVariantList devices() const { return m_devices; }
    bool discovering() const { return m_discovering; }
    bool bluetoothAvailable() const { return m_bluetoothAvailable; }
    bool busy() const { return m_busy; }
    QString statusMessage() const { return m_statusMessage; }
    QString confirmationCode() const { return m_confirmationCode; }
    QString confirmationDevice() const { return m_confirmationDevice; }
    QString agentStatus() const { return m_agentStatus; }
    bool agentReady() const { return m_agentReady; }

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void startDiscovery();
    Q_INVOKABLE void stopDiscovery();
    Q_INVOKABLE void pairDevice(const QString &path);
    Q_INVOKABLE void connectDevice(const QString &path);
    Q_INVOKABLE void disconnectDevice(const QString &path);
    Q_INVOKABLE void removeDevice(const QString &path);
    Q_INVOKABLE void acceptPairing();
    Q_INVOKABLE void rejectPairing();

    void apply(const QJsonObject &state);

signals:
    void devicesChanged();
    void discoveringChanged();
    void bluetoothAvailableChanged();
    void busyChanged();
    void statusMessageChanged();
    void agentStatusChanged();
    void confirmationRequested();

private:
    ControlClient *m_client;
    QVariantList m_devices;
    QString m_devicesJson;
    QString m_statusMessage;
    QString m_confirmationCode;
    QString m_confirmationDevice;
    QString m_agentStatus;
    bool m_discovering;
    bool m_bluetoothAvailable;
    bool m_busy;
    bool m_agentReady;
};

class HealthFacade : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int steps READ steps NOTIFY metricsChanged)
    Q_PROPERTY(int calories READ calories NOTIFY metricsChanged)
    Q_PROPERTY(int distanceMeters READ distanceMeters NOTIFY metricsChanged)
    Q_PROPERTY(int heartRate READ heartRate NOTIFY metricsChanged)
    Q_PROPERTY(int floors READ floors NOTIFY metricsChanged)
    Q_PROPERTY(int batteryPercent READ batteryPercent NOTIFY metricsChanged)
    Q_PROPERTY(bool stepsValid READ stepsValid NOTIFY metricsChanged)
    Q_PROPERTY(bool caloriesValid READ caloriesValid NOTIFY metricsChanged)
    Q_PROPERTY(bool distanceValid READ distanceValid NOTIFY metricsChanged)
    Q_PROPERTY(bool heartRateValid READ heartRateValid NOTIFY metricsChanged)
    Q_PROPERTY(bool floorsValid READ floorsValid NOTIFY metricsChanged)
    Q_PROPERTY(bool batteryValid READ batteryValid NOTIFY metricsChanged)
    Q_PROPERTY(bool syncing READ syncing NOTIFY syncingChanged)
    Q_PROPERTY(QString lastUpdated READ lastUpdated NOTIFY metricsChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)

public:
    explicit HealthFacade(ControlClient *client, QObject *parent = 0);

    int steps() const { return m_steps; }
    int calories() const { return m_calories; }
    int distanceMeters() const { return m_distanceMeters; }
    int heartRate() const { return m_heartRate; }
    int floors() const { return m_floors; }
    int batteryPercent() const { return m_batteryPercent; }
    bool stepsValid() const { return m_stepsValid; }
    bool caloriesValid() const { return m_caloriesValid; }
    bool distanceValid() const { return m_distanceValid; }
    bool heartRateValid() const { return m_heartRateValid; }
    bool floorsValid() const { return m_floorsValid; }
    bool batteryValid() const { return m_batteryValid; }
    bool syncing() const { return m_syncing; }
    QString lastUpdated() const { return m_lastUpdated; }
    QString status() const { return m_status; }
    QString lastError() const { return m_lastError; }

    Q_INVOKABLE void sync(const QString &path);
    void apply(const QJsonObject &state);

signals:
    void metricsChanged();
    void syncingChanged();
    void statusChanged();
    void lastErrorChanged();

private:
    ControlClient *m_client;
    QString m_lastUpdated;
    QString m_status;
    QString m_lastError;
    int m_steps;
    int m_calories;
    int m_distanceMeters;
    int m_heartRate;
    int m_floors;
    int m_batteryPercent;
    bool m_stepsValid;
    bool m_caloriesValid;
    bool m_distanceValid;
    bool m_heartRateValid;
    bool m_floorsValid;
    bool m_batteryValid;
    bool m_syncing;
};

class GattFacade : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool ready READ ready NOTIFY statusChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString detail READ detail NOTIFY detailChanged)
    Q_PROPERTY(bool watchLinked READ watchLinked NOTIFY watchLinkedChanged)

public:
    explicit GattFacade(QObject *parent = 0);

    bool ready() const { return m_ready; }
    QString status() const { return m_status; }
    QString detail() const { return m_detail; }
    bool watchLinked() const { return m_watchLinked; }
    void apply(const QJsonObject &state);
    void setOffline(const QString &status);

signals:
    void statusChanged();
    void detailChanged();
    void watchLinkedChanged();

private:
    QString m_status;
    QString m_detail;
    bool m_ready;
    bool m_watchLinked;
};

class CallFacade : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool ringing READ ringing NOTIFY ringingChanged)

public:
    explicit CallFacade(QObject *parent = 0);

    QString status() const { return m_status; }
    bool ringing() const { return m_ringing; }
    void apply(const QJsonObject &state);

signals:
    void statusChanged();
    void ringingChanged();

private:
    QString m_status;
    bool m_ringing;
};

class NotificationFacade : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY statusChanged)
    Q_PROPERTY(QVariantList apps READ apps NOTIFY appsChanged)
    Q_PROPERTY(bool groupByApp READ groupByApp WRITE setGroupByApp NOTIFY preferencesChanged)
    Q_PROPERTY(bool skipWhenUnlocked READ skipWhenUnlocked WRITE setSkipWhenUnlocked
               NOTIFY preferencesChanged)

public:
    explicit NotificationFacade(ControlClient *client, QObject *parent = 0);

    QString status() const { return m_status; }
    bool ready() const { return m_ready; }
    QVariantList apps() const { return m_apps; }
    bool groupByApp() const { return m_groupByApp; }
    bool skipWhenUnlocked() const { return m_skipWhenUnlocked; }

    Q_INVOKABLE void setAppEnabled(const QString &app, bool enabled);
    Q_INVOKABLE void setGroupByApp(bool enabled);
    Q_INVOKABLE void setSkipWhenUnlocked(bool enabled);
    void apply(const QJsonObject &state);
    void loadLocalPreferences();

signals:
    void statusChanged();
    void appsChanged();
    void preferencesChanged();

private:
    ControlClient *m_client;
    QVariantList m_apps;
    QString m_appsJson;
    QString m_status;
    bool m_ready;
    bool m_groupByApp;
    bool m_skipWhenUnlocked;
};

class ControlClient : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(QString status READ status NOTIFY connectedChanged)
    Q_PROPERTY(bool serviceEnabled READ serviceEnabled WRITE setServiceEnabled
               NOTIFY serviceEnabledChanged)

public:
    explicit ControlClient(QObject *parent = 0);

    bool connected() const { return m_connected; }
    QString status() const { return m_status; }
    bool serviceEnabled() const { return m_serviceEnabled; }

    Q_INVOKABLE void restartLink();
    Q_INVOKABLE void setServiceEnabled(bool enabled);
    Q_INVOKABLE void uninstall();

    BluezFacade *bluez() const { return m_bluez; }
    HealthFacade *health() const { return m_health; }
    GattFacade *gatt() const { return m_gatt; }
    CallFacade *calls() const { return m_calls; }
    NotificationFacade *notifications() const { return m_notifications; }

    void call(const QString &method, const QVariant &argument = QVariant());
    void call(const QString &method, const QVariant &first, const QVariant &second);

signals:
    void connectedChanged();
    void serviceEnabledChanged();

private slots:
    void tryConnect();
    void onStateChanged(const QString &json);

private:
    void applyState(const QString &json);
    void setConnected(bool connected, const QString &status);
    void loadServiceEnabled();
    void writeServiceEnabled(bool enabled);
    void applyServiceUnit(bool enabled);

    BluezFacade *m_bluez;
    HealthFacade *m_health;
    GattFacade *m_gatt;
    CallFacade *m_calls;
    NotificationFacade *m_notifications;
    QTimer *m_retry;
    QDBusConnection m_connection;
    bool m_connected;
    bool m_serviceEnabled;
    QString m_status;
};

#endif
