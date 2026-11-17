#ifndef NOTIFICATIONBRIDGE_H
#define NOTIFICATIONBRIDGE_H

#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QStringList>
#include <QThread>
#include <QVariantList>
#include <QVariantMap>

class QTimer;
class QSettings;
struct DBusConnection;
struct DBusMessage;

class NotificationMonitorThread : public QThread
{
    Q_OBJECT

public:
    explicit NotificationMonitorThread(QObject *parent = 0);
    ~NotificationMonitorThread();
    void stop();

signals:
    void notify(const QString &appName, uint replacesId, const QString &appIcon,
                const QString &summary, const QString &body, const QStringList &actions,
                const QVariantMap &hints, int timeout);
    void notificationClosed(uint id);
    void listed(const QString &appName, uint id, const QString &summary,
                const QString &body, const QVariantMap &hints);
    void listingStarted();
    void listingFinished();
    void monitorReady();
    void monitorFailed(const QString &reason);

protected:
    void run();

private:
    static int handleMessage(DBusConnection *connection, DBusMessage *message,
                             void *userData);
    void handleNotify(DBusMessage *message);
    void handleClosed(DBusMessage *message);
    bool pollOwners();
    bool sleepWhileRunning(int ms);
    QStringList ownerNames() const;

    DBusConnection *m_connection;
    DBusConnection *m_live;
    bool m_running;
    int m_lastListed;
};

class NotificationBridge : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY statusChanged)
    Q_PROPERTY(QVariantList apps READ apps NOTIFY appsChanged)
    Q_PROPERTY(bool groupByApp READ groupByApp WRITE setGroupByApp NOTIFY preferencesChanged)
    Q_PROPERTY(bool skipWhenUnlocked READ skipWhenUnlocked WRITE setSkipWhenUnlocked
               NOTIFY preferencesChanged)

public:
    explicit NotificationBridge(QObject *parent = 0);
    ~NotificationBridge();

    QString status() const { return m_status; }
    bool ready() const { return m_ready; }
    QVariantList apps() const;
    bool groupByApp() const { return m_groupByApp; }
    bool skipWhenUnlocked() const { return m_skipWhenUnlocked; }

    Q_INVOKABLE bool isAppEnabled(const QString &app) const;
    Q_INVOKABLE void setAppEnabled(const QString &app, bool enabled);
    Q_INVOKABLE void setGroupByApp(bool enabled);
    Q_INVOKABLE void setSkipWhenUnlocked(bool enabled);

signals:
    void notificationPosted(const QString &app, const QString &title,
                            const QString &body, uint id, uint color);
    void notificationWithdrawn(uint id);
    void callRinging(bool incoming, const QString &caller);
    void statusChanged();
    void appsChanged();
    void preferencesChanged();

private slots:
    void onNotify(const QString &appName, uint replacesId, const QString &appIcon,
                  const QString &summary, const QString &body, const QStringList &actions,
                  const QVariantMap &hints, int timeout);
    void onNotificationClosed(uint id);
    void onListed(const QString &appName, uint id, const QString &summary,
                  const QString &body, const QVariantMap &hints);
    void onListingStarted();
    void onListingFinished();
    void onTklockMode(const QString &mode);

private:
    struct ActiveAlert {
        uint id;
        QString title;
        QString body;
        uint color;
    };

    void setStatus(const QString &text, bool ready);
    void considerNotification(uint id, const QString &app, const QString &title,
                              const QString &body, const QVariantMap &hints,
                              bool fromPoll);
    void rememberApp(const QString &app);
    void loadSettings();
    void saveDisabledApps();
    void savePreferences();
    void noteIncomingCall(uint id, const QString &caller, bool fromPoll);
    void stopIncomingCall();
    void publishAlert(const QString &app, const ActiveAlert &alert, int others);
    void refreshGroupedAlert(const QString &app);
    void removeActiveAlert(uint id, bool publish);
    uint stableAppId(const QString &app) const;
    void refreshLockState();
    bool phoneIsUnlocked() const;

    QString m_status;
    QString m_callCaller;
    bool m_ready;
    bool m_seeded;
    bool m_callRinging;
    bool m_callSeenInList;
    bool m_sawCallInPoll;
    bool m_groupByApp;
    bool m_skipWhenUnlocked;
    bool m_phoneUnlocked;
    qint64 m_lastCallMs;
    QHash<uint, QString> m_fingerprints;
    QHash<uint, QString> m_activeAppById;
    QHash<QString, QList<ActiveAlert> > m_activeByApp;
    QSet<uint> m_ringingIds;
    QStringList m_knownApps;
    QSet<QString> m_disabledApps;
    NotificationMonitorThread *m_monitor;
    QSettings *m_settings;
};

#endif
