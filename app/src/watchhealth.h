#ifndef WATCHHEALTH_H
#define WATCHHEALTH_H

#include <QObject>
#include <QByteArray>
#include <QString>

class WatchHealth : public QObject
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
    Q_PROPERTY(bool usingWatchApp READ usingWatchApp NOTIFY metricsChanged)

public:
    explicit WatchHealth(QObject *parent = 0);

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
    bool usingWatchApp() const { return m_usingWatchApp; }

    Q_INVOKABLE void sync(const QString &devicePath);
    Q_INVOKABLE void stop();
    Q_INVOKABLE void forwardNotification(const QString &app, const QString &title,
                                         const QString &body, uint id, uint color);
    Q_INVOKABLE void withdrawNotification(uint id);
    Q_INVOKABLE void forwardCall(bool ringing, const QString &caller);
    Q_INVOKABLE void sendPhoneBattery();

signals:
    void metricsChanged();
    void syncingChanged();
    void statusChanged();
    void lastErrorChanged();
    void alertOutgoing(const QByteArray &payload);
    void syncRequested();

public slots:
    void onWatchMetrics(const QByteArray &value);
    void onBatteryLevel(int percent);
    void onChargerState(const QString &state);
    void onGattStatus(const QString &text);

private:
    void setSyncing(bool value);
    void setStatus(const QString &text);
    void setLastError(const QString &text);
    void refreshPhoneBattery();
    void applyWristLinkMetrics(const QByteArray &value);
    static void appendUtf8Field(QByteArray &out, const QString &text, int maxBytes);

    QString m_devicePath;
    QString m_status;
    QString m_lastError;
    QString m_lastUpdated;
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
    bool m_usingWatchApp;
    int m_phoneBattery;
    bool m_phoneCharging;
    bool m_phoneBatteryValid;
};

#endif
