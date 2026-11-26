#ifndef GATTSERVER_H
#define GATTSERVER_H

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

class QTimer;

// Uses the watch BlueZ already paired and connected. Notifications and calls
// are written when they happen; steps are read then, and otherwise about
// every half hour. This process does not advertise or scan.
class GattServer : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool ready READ ready NOTIFY statusChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString detail READ detail NOTIFY detailChanged)
    Q_PROPERTY(bool watchLinked READ watchLinked NOTIFY watchLinkedChanged)

public:
    explicit GattServer(QObject *parent = 0);
    ~GattServer();

    bool ready() const { return m_ready; }
    QString status() const { return m_status; }
    QString detail() const { return m_detail; }
    bool watchLinked() const { return m_watchLinked; }

    Q_INVOKABLE void sendAlert(const QByteArray &payload);
    void readMetrics();
    void noteWatch(const QString &pairedPath, bool connected);
    void holdLink(bool hold);

    static const char *kServiceUuid;
    static const char *kMetricsUuid;
    static const char *kAlertUuid;

signals:
    void metricsReceived(const QByteArray &value);
    void statusChanged();
    void detailChanged();
    void watchLinkedChanged();
    void alertsSubscribed();
    void syncDue();
    void connectRequested(const QString &path);
    void reconnectRequested(const QString &path);

public slots:
    void start();
    void restart();

private slots:
    void maintain();

private:
    void discover();
    void pump();
    void writeFront();
    void readNow();
    void finishOp(int epoch, bool ok, const QString &error, const QByteArray &metrics);
    void restoreInflight();
    void markLinked();
    void dropSession();
    void requestConnect(bool now);
    void bounce();
    void requestLeReconnect();
    void enqueue(const QByteArray &payload);
    void takeMetrics(const QByteArray &value);
    void note(const QString &line);
    void setStatus(const QString &text, bool ready);
    void setWatchLinked(bool linked);
    static bool isBattery(const QByteArray &payload);

    QTimer *m_timer;
    QString m_pairedPath;
    QString m_alertPath;
    QString m_metricsPath;
    QString m_status;
    QString m_detail;
    QStringList m_notes;
    QList<QByteArray> m_writes;
    QByteArray m_inflight;
    qint64 m_lastMetricsMs;
    qint64 m_syncStartedMs;
    qint64 m_lastConnectAttemptMs;
    qint64 m_lastBounceMs;
    qint64 m_lastLeReconnectMs;
    qint64 m_quietUntilMs;
    bool m_alive;
    bool m_ready;
    bool m_watchLinked;
    bool m_linkUp;
    bool m_userHeld;
    bool m_bouncing;
    bool m_opInFlight;
    bool m_hasInflight;
    bool m_readQueued;
    bool m_bouncedForError;
    int m_opEpoch;
    int m_failures;
    int m_leReconnectAttempts;
};

#endif
