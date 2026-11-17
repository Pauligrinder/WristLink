#ifndef BLUEZMANAGER_H
#define BLUEZMANAGER_H

#include <QObject>
#include <QVariantList>
#include <QDBusContext>
#include <QDBusObjectPath>
#include <QEventLoop>

class QTimer;

class PairingAgent : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.bluez.Agent1")

public:
    explicit PairingAgent(QObject *parent = 0);

    Q_INVOKABLE void accept();
    Q_INVOKABLE void reject();

signals:
    void confirmationRequested(const QString &devicePath, const QString &passkey);

public slots:
    void Release();
    QString RequestPinCode(const QDBusObjectPath &device);
    uint RequestPasskey(const QDBusObjectPath &device);
    void DisplayPinCode(const QDBusObjectPath &device, const QString &pinCode);
    void DisplayPasskey(const QDBusObjectPath &device, uint passkey, ushort entered);
    void RequestConfirmation(const QDBusObjectPath &device, uint passkey);
    void RequestAuthorization(const QDBusObjectPath &device);
    void AuthorizeService(const QDBusObjectPath &device, const QString &uuid);
    void Cancel();

private:
    bool waitForDecision();

    QEventLoop *m_decisionLoop;
    bool m_accepted;
};

class BluezManager : public QObject
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
    explicit BluezManager(QObject *parent = 0);
    ~BluezManager();

    QVariantList devices() const;
    bool discovering() const;
    bool bluetoothAvailable() const;
    bool busy() const;
    QString statusMessage() const;
    QString confirmationCode() const;
    QString confirmationDevice() const;
    QString agentStatus() const;
    bool agentReady() const;

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void startDiscovery();
    Q_INVOKABLE void stopDiscovery();
    Q_INVOKABLE void pairDevice(const QString &path);
    Q_INVOKABLE void connectDevice(const QString &path);
    void ensureConnected(const QString &path);
    void reconnectDevice(const QString &path);
    Q_INVOKABLE void disconnectDevice(const QString &path);
    Q_INVOKABLE void removeDevice(const QString &path);
    Q_INVOKABLE void acceptPairing();
    Q_INVOKABLE void rejectPairing();
    Q_INVOKABLE QString connectedWatchPath() const;
    QString pairedWatchPath() const;
    bool watchIsConnected() const;

signals:
    void deviceConnected(const QString &path);
    void deviceDisconnected();
    void devicesChanged();
    void discoveringChanged();
    void bluetoothAvailableChanged();
    void busyChanged();
    void statusMessageChanged();
    void agentStatusChanged();
    void confirmationRequested();

private slots:
    void onConfirmationRequested(const QString &devicePath, const QString &passkey);

private:
    void setDiscovering(bool value);
    void setBusy(bool value);
    void setStatusMessage(const QString &message);
    void setAgentStatus(const QString &message, bool ready);
    void registerAgent();
    void setDeviceTrusted(const QString &path);
    QString deviceNameForPath(const QString &path) const;
    void callDeviceMethod(const QString &path, const QString &method,
                          const QString &workingMessage, const QString &successMessage);

    QVariantList m_devices;
    QString m_adapterPath;
    bool m_discovering;
    bool m_bluetoothAvailable;
    bool m_busy;
    QString m_statusMessage;
    QString m_confirmationCode;
    QString m_confirmationDevice;
    QString m_agentStatus;
    bool m_agentReady;
    bool m_agentRegistered;
    int m_methodRetries;
    QTimer *m_refreshTimer;
    PairingAgent *m_pairingAgent;
    QString m_connectedWatchPath;
};

#endif
