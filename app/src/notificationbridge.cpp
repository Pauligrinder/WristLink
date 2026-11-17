#include "notificationbridge.h"

#include "socketpath.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QSettings>
#include <QStandardPaths>
#include <QTextStream>
#include <QMetaType>
#include <QVariant>
#include <dbus/dbus.h>

static void logLine(const QString &line)
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    QFile file(QDir(dir).filePath(QStringLiteral("wristlink.log")));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        return;
    if (file.size() > 200000)
        file.resize(0);
    QTextStream out(&file);
    out << QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss.zzz"))
        << QLatin1Char(' ') << line << QLatin1Char('\n');
}

static QString becomeMonitor(DBusConnection *conn)
{
    DBusMessage *msg = dbus_message_new_method_call(
                DBUS_SERVICE_DBUS, DBUS_PATH_DBUS,
                "org.freedesktop.DBus.Monitoring", "BecomeMonitor");
    if (!msg)
        return QStringLiteral("Could not create BecomeMonitor call");

    const char *matches[] = {
        "type='method_call',interface='org.freedesktop.Notifications',member='Notify'",
        "type='method_call',interface='org.freedesktop.Notifications',member='CloseNotification'",
        "type='signal',interface='org.freedesktop.Notifications',member='NotificationClosed'"
    };
    const char **matchesPtr = matches;
    dbus_uint32_t flags = 0;
    if (!dbus_message_append_args(msg,
                                  DBUS_TYPE_ARRAY, DBUS_TYPE_STRING, &matchesPtr, 3,
                                  DBUS_TYPE_UINT32, &flags,
                                  DBUS_TYPE_INVALID)) {
        dbus_message_unref(msg);
        return QStringLiteral("Could not append BecomeMonitor arguments");
    }

    DBusError error;
    dbus_error_init(&error);
    DBusMessage *reply = dbus_connection_send_with_reply_and_block(conn, msg, 3000, &error);
    dbus_message_unref(msg);
    if (!reply) {
        const QString reason = QString::fromUtf8(error.message ? error.message : "BecomeMonitor failed");
        dbus_error_free(&error);
        return reason;
    }
    dbus_message_unref(reply);
    return QString();
}

NotificationMonitorThread::NotificationMonitorThread(QObject *parent)
    : QThread(parent)
    , m_connection(0)
    , m_live(0)
    , m_running(true)
    , m_lastListed(-1)
{
    qRegisterMetaType<QStringList>("QStringList");
    qRegisterMetaType<QVariantMap>("QVariantMap");
}

NotificationMonitorThread::~NotificationMonitorThread()
{
    stop();
    wait(2000);
}

void NotificationMonitorThread::stop()
{
    m_running = false;
    if (m_connection)
        dbus_connection_close(m_connection);
    if (m_live)
        dbus_connection_close(m_live);
}

bool NotificationMonitorThread::sleepWhileRunning(int ms)
{
    const int steps = qMax(1, ms / 100);
    for (int i = 0; i < steps && m_running; ++i)
        msleep(100);
    return m_running;
}

static DBusConnection *openPrivateBus(QString *errorOut)
{
    DBusError error;
    dbus_error_init(&error);
    DBusConnection *connection = dbus_bus_get_private(DBUS_BUS_SESSION, &error);
    if (!connection) {
        if (errorOut) {
            *errorOut = QString::fromUtf8(error.message ? error.message : "no session bus");
        }
        dbus_error_free(&error);
        return 0;
    }
    dbus_connection_set_exit_on_disconnect(connection, false);
    return connection;
}

static bool dispatchBus(DBusConnection *connection, int timeoutMs)
{
    if (!connection)
        return true;
    if (!dbus_connection_read_write(connection, timeoutMs))
        return false;
    while (dbus_connection_dispatch(connection) == DBUS_DISPATCH_DATA_REMAINS) {
    }
    return dbus_connection_get_is_connected(connection);
}

void NotificationMonitorThread::run()
{
    while (m_running) {
        QString busError;
        DBusConnection *poll = openPrivateBus(&busError);
        if (!poll) {
            logLine(QStringLiteral("Notification bus unavailable: %1").arg(busError));
            emit monitorFailed(busError);
            if (!sleepWhileRunning(2000))
                break;
            continue;
        }
        m_connection = poll;

        // BecomeMonitor disables method calls on that connection. The list is
        // read with GetNotifications, so the live monitor needs its own bus.
        DBusConnection *live = openPrivateBus(0);
        if (live) {
            dbus_connection_set_route_peer_messages(live, true);
            dbus_connection_add_filter(live,
                                       reinterpret_cast<DBusHandleMessageFunction>(
                                           &NotificationMonitorThread::handleMessage),
                                       this, 0);
            const QString monitorError = becomeMonitor(live);
            if (monitorError.isEmpty()) {
                m_live = live;
                emit monitorReady();
            } else {
                logLine(QStringLiteral("Live notification monitor unavailable: %1").arg(monitorError));
                dbus_connection_unref(live);
                live = 0;
                emit monitorFailed(monitorError);
            }
        }

        bool polling = pollOwners();
        int idleTicks = 0;
        while (m_running && polling && dispatchBus(poll, live ? 0 : 1000)) {
            if (live && !dispatchBus(live, 0)) {
                logLine(QStringLiteral("Live notification monitor disconnected"));
                dbus_connection_unref(live);
                m_live = 0;
                live = 0;
            }
            if (live && !dispatchBus(poll, 1000))
                break;
            if (!m_running)
                break;
            if (++idleTicks >= 2) {
                idleTicks = 0;
                polling = pollOwners();
            }
        }
        if (m_running && !polling)
            logLine(QStringLiteral("Notification list could not be read, reconnecting"));
        else if (m_running)
            logLine(QStringLiteral("Notification bus closed, reconnecting"));

        if (live) {
            dbus_connection_unref(live);
            live = 0;
        }
        m_live = 0;
        dbus_connection_unref(poll);
        m_connection = 0;
        if (!m_running)
            break;
        if (!sleepWhileRunning(1000))
            break;
    }
}

int NotificationMonitorThread::handleMessage(DBusConnection *, DBusMessage *message,
                                             void *userData)
{
    NotificationMonitorThread *self = static_cast<NotificationMonitorThread *>(userData);
    if (dbus_message_is_method_call(message, "org.freedesktop.Notifications", "Notify"))
        self->handleNotify(message);
    else if (dbus_message_is_method_call(message, "org.freedesktop.Notifications", "CloseNotification")
             || dbus_message_is_signal(message, "org.freedesktop.Notifications", "NotificationClosed"))
        self->handleClosed(message);
    // Never claim HANDLED: eavesdrop filters must not swallow notifications.
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static QString iterString(DBusMessageIter *iter)
{
    if (dbus_message_iter_get_arg_type(iter) != DBUS_TYPE_STRING)
        return QString();
    const char *value = 0;
    dbus_message_iter_get_basic(iter, &value);
    dbus_message_iter_next(iter);
    return QString::fromUtf8(value ? value : "");
}

static uint iterUint(DBusMessageIter *iter)
{
    if (dbus_message_iter_get_arg_type(iter) != DBUS_TYPE_UINT32)
        return 0;
    dbus_uint32_t value = 0;
    dbus_message_iter_get_basic(iter, &value);
    dbus_message_iter_next(iter);
    return value;
}

static int iterInt(DBusMessageIter *iter)
{
    if (dbus_message_iter_get_arg_type(iter) != DBUS_TYPE_INT32)
        return 0;
    dbus_int32_t value = 0;
    dbus_message_iter_get_basic(iter, &value);
    dbus_message_iter_next(iter);
    return value;
}

static QStringList iterStringList(DBusMessageIter *iter)
{
    QStringList list;
    if (dbus_message_iter_get_arg_type(iter) != DBUS_TYPE_ARRAY)
        return list;
    DBusMessageIter sub;
    dbus_message_iter_recurse(iter, &sub);
    dbus_message_iter_next(iter);
    while (dbus_message_iter_get_arg_type(&sub) != DBUS_TYPE_INVALID)
        list.append(iterString(&sub));
    return list;
}

static QVariant iterVariant(DBusMessageIter *iter)
{
    if (dbus_message_iter_get_arg_type(iter) != DBUS_TYPE_VARIANT)
        return QVariant();
    DBusMessageIter sub;
    dbus_message_iter_recurse(iter, &sub);
    dbus_message_iter_next(iter);
    const int type = dbus_message_iter_get_arg_type(&sub);
    switch (type) {
    case DBUS_TYPE_BOOLEAN: {
        dbus_bool_t value = false;
        dbus_message_iter_get_basic(&sub, &value);
        return QVariant(bool(value));
    }
    case DBUS_TYPE_STRING: {
        const char *value = 0;
        dbus_message_iter_get_basic(&sub, &value);
        return QVariant(QString::fromUtf8(value ? value : ""));
    }
    case DBUS_TYPE_BYTE: {
        unsigned char value = 0;
        dbus_message_iter_get_basic(&sub, &value);
        return QVariant(uint(value));
    }
    case DBUS_TYPE_INT32: {
        dbus_int32_t value = 0;
        dbus_message_iter_get_basic(&sub, &value);
        return QVariant(int(value));
    }
    case DBUS_TYPE_UINT32: {
        dbus_uint32_t value = 0;
        dbus_message_iter_get_basic(&sub, &value);
        return QVariant(uint(value));
    }
    default:
        return QVariant();
    }
}

static QVariantMap iterVariantMap(DBusMessageIter *iter)
{
    QVariantMap map;
    if (dbus_message_iter_get_arg_type(iter) != DBUS_TYPE_ARRAY)
        return map;
    DBusMessageIter sub;
    dbus_message_iter_recurse(iter, &sub);
    dbus_message_iter_next(iter);
    while (dbus_message_iter_get_arg_type(&sub) == DBUS_TYPE_DICT_ENTRY) {
        DBusMessageIter entry;
        dbus_message_iter_recurse(&sub, &entry);
        const QString key = iterString(&entry);
        const QVariant value = iterVariant(&entry);
        if (!key.isEmpty())
            map.insert(key, value);
        dbus_message_iter_next(&sub);
    }
    return map;
}

void NotificationMonitorThread::handleNotify(DBusMessage *message)
{
    if (!dbus_message_has_signature(message, "susssasa{sv}i"))
        return;
    DBusMessageIter iter;
    dbus_message_iter_init(message, &iter);
    const QString appName = iterString(&iter);
    const uint replacesId = iterUint(&iter);
    const QString appIcon = iterString(&iter);
    const QString summary = iterString(&iter);
    const QString body = iterString(&iter);
    const QStringList actions = iterStringList(&iter);
    const QVariantMap hints = iterVariantMap(&iter);
    const int timeout = iterInt(&iter);
    emit notify(appName, replacesId, appIcon, summary, body, actions, hints, timeout);
}

void NotificationMonitorThread::handleClosed(DBusMessage *message)
{
    DBusMessageIter iter;
    if (!dbus_message_iter_init(message, &iter))
        return;
    if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_UINT32)
        return;
    dbus_uint32_t id = 0;
    dbus_message_iter_get_basic(&iter, &id);
    emit notificationClosed(id);
}

QStringList NotificationMonitorThread::ownerNames() const
{
    QStringList owners;
    owners << QStringLiteral("messageserver5")
           << QStringLiteral("commhistoryd")
           << QStringLiteral("apkd-bridge")
           << QStringLiteral("voicecall-ui")
           << QStringLiteral("voicecall-manager")
           << QStringLiteral("jolla-messages")
           << QStringLiteral("jolla-email")
           << QStringLiteral("lipstick")
           << QString()
           << QStringLiteral("sailfish-browser");

    QDir applications(QStringLiteral("/usr/share/applications"));
    const QStringList desktops = applications.entryList(QStringList() << QStringLiteral("*.desktop"));
    foreach (const QString &name, desktops) {
        QFile desktop(applications.filePath(name));
        if (!desktop.open(QIODevice::ReadOnly))
            continue;
        while (!desktop.atEnd()) {
            const QByteArray line = desktop.readLine().trimmed();
            if (!line.startsWith("Exec="))
                continue;
            const QStringList parts = QString::fromUtf8(line.mid(5)).split(
                        QLatin1Char(' '), QString::SkipEmptyParts);
            for (int i = parts.size() - 1; i >= 0; --i) {
                const QString token = parts.at(i);
                if (token.startsWith(QLatin1Char('-')) || token.startsWith(QLatin1Char('%'))
                        || token.contains(QLatin1String(".desktop")))
                    continue;
                const QString exec = token.section(QLatin1Char('/'), -1);
                if (!exec.isEmpty() && !exec.startsWith(QLatin1Char('%'))
                        && exec != QLatin1String("invoker")
                        && exec != QLatin1String("sailjail"))
                    owners.append(exec);
                break;
            }
            break;
        }
    }
    owners.removeDuplicates();
    return owners;
}

static int deliverNotifications(DBusMessage *reply, NotificationMonitorThread *self)
{
    DBusMessageIter iter;
    if (!dbus_message_iter_init(reply, &iter))
        return 0;
    if (dbus_message_iter_get_arg_type(&iter) != DBUS_TYPE_ARRAY)
        return 0;
    DBusMessageIter array;
    dbus_message_iter_recurse(&iter, &array);
    int count = 0;
    while (dbus_message_iter_get_arg_type(&array) == DBUS_TYPE_STRUCT) {
        DBusMessageIter structure;
        dbus_message_iter_recurse(&array, &structure);
        const QString app = iterString(&structure);
        const uint id = iterUint(&structure);
        iterString(&structure);
        const QString summary = iterString(&structure);
        const QString body = iterString(&structure);
        iterStringList(&structure);
        const QVariantMap hints = iterVariantMap(&structure);
        emit self->listed(app, id, summary, body, hints);
        dbus_message_iter_next(&array);
        ++count;
    }
    return count;
}

bool NotificationMonitorThread::pollOwners()
{
    if (!m_connection || !dbus_connection_get_is_connected(m_connection))
        return false;
    const QStringList owners = ownerNames();
    emit listingStarted();
    int listed = 0;
    foreach (const QString &owner, owners) {
        if (!m_running)
            break;
        const QByteArray encoded = owner.toUtf8();
        const char *ownerArg = encoded.constData();
        DBusMessage *message = dbus_message_new_method_call(
                    "org.freedesktop.Notifications",
                    "/org/freedesktop/Notifications",
                    "org.freedesktop.Notifications",
                    "GetNotifications");
        if (!message)
            continue;
        dbus_message_append_args(message, DBUS_TYPE_STRING, &ownerArg, DBUS_TYPE_INVALID);
        DBusError error;
        dbus_error_init(&error);
        DBusMessage *reply = dbus_connection_send_with_reply_and_block(
                    m_connection, message, 3000, &error);
        dbus_message_unref(message);
        if (!reply) {
            if (dbus_error_is_set(&error)) {
                const QString messageText = QString::fromUtf8(
                            error.message ? error.message : "GetNotifications failed");
                dbus_error_free(&error);
                const bool closed = messageText.contains(QLatin1String("closed"), Qt::CaseInsensitive)
                        || messageText.contains(QLatin1String("disconnected"), Qt::CaseInsensitive);
                if (closed || m_lastListed < 0) {
                    logLine(QStringLiteral("GetNotifications(%1) failed: %2")
                            .arg(owner, messageText));
                }
                if (closed)
                    return false;
            }
            continue;
        }
        listed += deliverNotifications(reply, this);
        dbus_message_unref(reply);
    }
    if (listed != m_lastListed) {
        m_lastListed = listed;
        logLine(QStringLiteral("Notification list has %1 item(s)").arg(listed));
    }
    emit listingFinished();
    return true;
}

NotificationBridge::NotificationBridge(QObject *parent)
    : QObject(parent)
    , m_ready(false)
    , m_seeded(false)
    , m_callRinging(false)
    , m_callSeenInList(false)
    , m_sawCallInPoll(false)
    , m_groupByApp(false)
    , m_skipWhenUnlocked(false)
    , m_phoneUnlocked(false)
    , m_lastCallMs(0)
    , m_monitor(new NotificationMonitorThread(this))
    , m_settings(0)
{
    m_settings = new QSettings(wristlinkSettingsPath(), QSettings::IniFormat, this);
    loadSettings();
    connect(m_monitor, SIGNAL(notify(QString,uint,QString,QString,QString,QStringList,QVariantMap,int)),
            this, SLOT(onNotify(QString,uint,QString,QString,QString,QStringList,QVariantMap,int)),
            Qt::QueuedConnection);
    connect(m_monitor, SIGNAL(notificationClosed(uint)),
            this, SLOT(onNotificationClosed(uint)),
            Qt::QueuedConnection);
    connect(m_monitor, SIGNAL(listed(QString,uint,QString,QString,QVariantMap)),
            this, SLOT(onListed(QString,uint,QString,QString,QVariantMap)),
            Qt::QueuedConnection);
    connect(m_monitor, SIGNAL(listingStarted()),
            this, SLOT(onListingStarted()),
            Qt::QueuedConnection);
    connect(m_monitor, SIGNAL(listingFinished()),
            this, SLOT(onListingFinished()),
            Qt::QueuedConnection);
    connect(m_monitor, &NotificationMonitorThread::monitorReady, this, [this]() {
        setStatus(QStringLiteral("Forwarding phone notifications to the watch"), true);
    });
    connect(m_monitor, &NotificationMonitorThread::monitorFailed, this, [this](const QString &reason) {
        logLine(QStringLiteral("Live notification monitor unavailable: %1").arg(reason));
        setStatus(QStringLiteral("Watching the notification list"), true);
    });
    QDBusConnection::systemBus().connect(
                QStringLiteral("com.nokia.mce"),
                QStringLiteral("/com/nokia/mce/signal"),
                QStringLiteral("com.nokia.mce.signal"),
                QStringLiteral("tklock_mode_ind"),
                this, SLOT(onTklockMode(QString)));
    refreshLockState();
    m_monitor->start();
    setStatus(QStringLiteral("Starting notification listener…"), false);
}

NotificationBridge::~NotificationBridge()
{
    m_monitor->stop();
}

void NotificationBridge::loadSettings()
{
    const QStringList disabled =
            m_settings->value(QStringLiteral("notifications/disabledApps")).toStringList();
    m_disabledApps = QSet<QString>::fromList(disabled);
    const QStringList known =
            m_settings->value(QStringLiteral("notifications/knownApps")).toStringList();
    m_knownApps = known;
    m_groupByApp = m_settings->value(QStringLiteral("notifications/groupByApp"), false).toBool();
    m_skipWhenUnlocked =
            m_settings->value(QStringLiteral("notifications/skipWhenUnlocked"), false).toBool();
}

void NotificationBridge::saveDisabledApps()
{
    m_settings->setValue(QStringLiteral("notifications/disabledApps"),
                         QStringList(m_disabledApps.toList()));
    m_settings->setValue(QStringLiteral("notifications/knownApps"), m_knownApps);
    m_settings->sync();
}

void NotificationBridge::savePreferences()
{
    m_settings->setValue(QStringLiteral("notifications/groupByApp"), m_groupByApp);
    m_settings->setValue(QStringLiteral("notifications/skipWhenUnlocked"), m_skipWhenUnlocked);
    m_settings->sync();
}

void NotificationBridge::setGroupByApp(bool enabled)
{
    if (m_groupByApp == enabled)
        return;
    m_groupByApp = enabled;
    savePreferences();
    emit preferencesChanged();
}

void NotificationBridge::setSkipWhenUnlocked(bool enabled)
{
    if (m_skipWhenUnlocked == enabled)
        return;
    m_skipWhenUnlocked = enabled;
    savePreferences();
    emit preferencesChanged();
}

void NotificationBridge::refreshLockState()
{
    QDBusMessage call = QDBusMessage::createMethodCall(
                QStringLiteral("com.nokia.mce"),
                QStringLiteral("/com/nokia/mce/request"),
                QStringLiteral("com.nokia.mce.request"),
                QStringLiteral("get_tklock_mode"));
    const QDBusMessage reply = QDBusConnection::systemBus().call(call, QDBus::Block, 1000);
    if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty())
        onTklockMode(reply.arguments().at(0).toString());
}

void NotificationBridge::onTklockMode(const QString &mode)
{
    m_phoneUnlocked = mode.contains(QLatin1String("unlocked"), Qt::CaseInsensitive);
}

bool NotificationBridge::phoneIsUnlocked() const
{
    return m_phoneUnlocked;
}

uint NotificationBridge::stableAppId(const QString &app) const
{
    uint id = qHash(app);
    if (id == 0)
        id = 1;
    return id;
}

void NotificationBridge::publishAlert(const QString &app, const ActiveAlert &alert, int others)
{
    QString body = alert.body;
    if (others > 0) {
        const QString suffix = others == 1
                ? QStringLiteral("+1 other notification")
                : QStringLiteral("+%1 other notifications").arg(others);
        if (body.isEmpty())
            body = suffix;
        else
            body = body + QLatin1Char('\n') + suffix;
    }
    logLine(QStringLiteral("Forwarding %1: %2").arg(app, alert.title));
    emit notificationPosted(app, alert.title, body, alert.id, alert.color);
    setStatus(QStringLiteral("Forwarding phone notifications to the watch"), true);
}

void NotificationBridge::refreshGroupedAlert(const QString &app)
{
    const QList<ActiveAlert> alerts = m_activeByApp.value(app);
    if (alerts.isEmpty()) {
        emit notificationWithdrawn(stableAppId(app));
        return;
    }
    ActiveAlert latest = alerts.last();
    latest.id = stableAppId(app);
    publishAlert(app, latest, alerts.size() - 1);
}

void NotificationBridge::removeActiveAlert(uint id, bool publish)
{
    const QString app = m_activeAppById.take(id);
    if (app.isEmpty())
        return;
    QList<ActiveAlert> &alerts = m_activeByApp[app];
    for (int i = 0; i < alerts.size(); ++i) {
        if (alerts.at(i).id != id)
            continue;
        alerts.removeAt(i);
        break;
    }
    if (alerts.isEmpty())
        m_activeByApp.remove(app);
    if (!publish)
        return;
    if (m_groupByApp)
        refreshGroupedAlert(app);
    else
        emit notificationWithdrawn(id);
}

QVariantList NotificationBridge::apps() const
{
    QVariantList list;
    foreach (const QString &name, m_knownApps) {
        QVariantMap entry;
        entry.insert(QStringLiteral("name"), name);
        entry.insert(QStringLiteral("enabled"), !m_disabledApps.contains(name));
        list.append(entry);
    }
    return list;
}

bool NotificationBridge::isAppEnabled(const QString &app) const
{
    if (app.isEmpty())
        return true;
    return !m_disabledApps.contains(app);
}

void NotificationBridge::setAppEnabled(const QString &app, bool enabled)
{
    if (app.isEmpty())
        return;
    rememberApp(app);
    const bool currentlyEnabled = !m_disabledApps.contains(app);
    if (currentlyEnabled == enabled)
        return;
    if (enabled)
        m_disabledApps.remove(app);
    else
        m_disabledApps.insert(app);
    saveDisabledApps();
    emit appsChanged();
}

void NotificationBridge::rememberApp(const QString &app)
{
    if (app.isEmpty() || m_knownApps.contains(app))
        return;
    m_knownApps.append(app);
    m_knownApps.sort(Qt::CaseInsensitive);
    m_settings->setValue(QStringLiteral("notifications/knownApps"), m_knownApps);
    emit appsChanged();
}

void NotificationBridge::setStatus(const QString &text, bool ready)
{
    if (m_status == text && m_ready == ready)
        return;
    m_status = text;
    m_ready = ready;
    emit statusChanged();
}

void NotificationBridge::onNotify(const QString &appName, uint replacesId,
                                  const QString &appIcon, const QString &summary,
                                  const QString &body, const QStringList &actions,
                                  const QVariantMap &hints, int timeout)
{
    Q_UNUSED(appIcon)
    Q_UNUSED(actions)
    Q_UNUSED(timeout)
    considerNotification(replacesId, appName, summary, body, hints, false);
}

static uint notificationColor(const QVariantMap &hints)
{
    const QString text = hints.value(QStringLiteral("x-nemo-color")).toString().trimmed();
    if (text.size() != 7 && text.size() != 9)
        return 0;
    if (!text.startsWith(QLatin1Char('#')))
        return 0;
    bool ok = false;
    const uint value = text.mid(1).toUInt(&ok, 16);
    if (!ok)
        return 0;
    if (text.size() == 7)
        return 0xff000000u | value;
    if ((value >> 24) == 0)
        return 0;
    return value;
}

static bool isMissedCall(const QString &category, const QString &title, const QString &body)
{
    const QString blob = (category + QLatin1Char(' ') + title + QLatin1Char(' ') + body).toLower();
    return blob.contains(QLatin1String("missed"))
            || blob.contains(QLatin1String("voicemail"));
}

static bool isIncomingCall(const QString &app, const QString &title, const QString &body,
                           const QVariantMap &hints, QString *caller)
{
    const QString category = hints.value(QStringLiteral("category")).toString();
    const QString owner = hints.value(QStringLiteral("x-nemo-owner")).toString();
    const QString previewSummary = hints.value(QStringLiteral("x-nemo-preview-summary")).toString();
    const QString previewBody = hints.value(QStringLiteral("x-nemo-preview-body")).toString();
    if (isMissedCall(category, title.isEmpty() ? previewSummary : title,
                     body.isEmpty() ? previewBody : body))
        return false;

    const bool callCategory = category.contains(QLatin1String("call"), Qt::CaseInsensitive)
            || category.contains(QLatin1String("ring"), Qt::CaseInsensitive);
    const QString feedback = hints.value(QStringLiteral("x-nemo-feedback")).toString()
            + hints.value(QStringLiteral("x-nemo-feedback-event")).toString();
    const bool callFeedback = feedback.contains(QLatin1String("ring"), Qt::CaseInsensitive)
            || feedback.contains(QLatin1String("telephone"), Qt::CaseInsensitive);
    const bool callOwner = owner.contains(QLatin1String("voicecall"), Qt::CaseInsensitive)
            && (callCategory
                || title.contains(QLatin1String("incoming"), Qt::CaseInsensitive)
                || body.contains(QLatin1String("incoming"), Qt::CaseInsensitive)
                || previewBody.contains(QLatin1String("incoming"), Qt::CaseInsensitive));
    if (!callCategory && !callFeedback && !callOwner)
        return false;

    QString name = !title.isEmpty() ? title : previewSummary;
    if (name.isEmpty())
        name = !body.isEmpty() ? body : previewBody;
    if (name.isEmpty())
        name = QStringLiteral("Incoming call");
    *caller = name;
    Q_UNUSED(app)
    return true;
}

void NotificationBridge::noteIncomingCall(uint id, const QString &caller, bool fromPoll)
{
    if (id != 0)
        m_ringingIds.insert(id);
    if (fromPoll) {
        m_sawCallInPoll = true;
        m_callSeenInList = true;
    }
    m_lastCallMs = QDateTime::currentMSecsSinceEpoch();
    if (m_callRinging && m_callCaller == caller)
        return;
    m_callRinging = true;
    m_callCaller = caller;
    emit callRinging(true, caller);
}

void NotificationBridge::stopIncomingCall()
{
    if (!m_callRinging)
        return;
    m_callRinging = false;
    m_callSeenInList = false;
    m_ringingIds.clear();
    emit callRinging(false, m_callCaller);
}

void NotificationBridge::onNotificationClosed(uint id)
{
    if (id == 0)
        return;
    if (m_ringingIds.contains(id)) {
        m_ringingIds.remove(id);
        if (m_ringingIds.isEmpty())
            stopIncomingCall();
    }
    removeActiveAlert(id, true);
}

void NotificationBridge::considerNotification(uint id, const QString &app,
                                              const QString &title, const QString &body,
                                              const QVariantMap &hints, bool fromPoll)
{
    const QString owner = hints.value(QStringLiteral("x-nemo-owner")).toString();
    if (owner.contains(QLatin1String("wristlink"), Qt::CaseInsensitive)
            || app.contains(QLatin1String("wristlink"), Qt::CaseInsensitive)
            || app.contains(QLatin1String("WristLink")))
        return;

    const QString category = hints.value(QStringLiteral("category")).toString();
    if (isMissedCall(category, title, body))
        stopIncomingCall();

    QString caller;
    if (isIncomingCall(app, title, body, hints, &caller)) {
        noteIncomingCall(id, caller, fromPoll);
        return;
    }

    if (hints.value(QStringLiteral("x-nemo-hidden")).toBool())
        return;
    if (hints.value(QStringLiteral("transient")).toBool()
            && hints.value(QStringLiteral("x-nemo-preview-summary")).toString().isEmpty()
            && title.isEmpty())
        return;

    const QString resolvedTitle = !title.isEmpty()
            ? title
            : hints.value(QStringLiteral("x-nemo-preview-summary")).toString();
    const QString resolvedBody = !body.isEmpty()
            ? body
            : hints.value(QStringLiteral("x-nemo-preview-body")).toString();
    if (resolvedTitle.isEmpty() && resolvedBody.isEmpty())
        return;

    if (category.startsWith(QLatin1String("x-nemo.system")))
        return;

    QString appLabel = !app.isEmpty() ? app : owner;
    if (owner == QLatin1String("messageserver5"))
        appLabel = QStringLiteral("Email");
    else if (owner == QLatin1String("commhistoryd")
             || owner == QLatin1String("jolla-messages"))
        appLabel = QStringLiteral("Messages");
    rememberApp(appLabel);
    if (!isAppEnabled(appLabel))
        return;
    if (m_skipWhenUnlocked && phoneIsUnlocked())
        return;

    uint key = id;
    const QString fingerprint = appLabel + QLatin1Char('\n') + resolvedTitle
            + QLatin1Char('\n') + resolvedBody;
    if (key == 0) {
        key = qHash(fingerprint);
        if (key == 0)
            key = 1;
    }
    if (m_fingerprints.value(key) == fingerprint)
        return;
    m_fingerprints.insert(key, fingerprint);
    if (m_fingerprints.size() > 400)
        m_fingerprints.erase(m_fingerprints.begin());
    if (fromPoll && !m_seeded)
        return;

    const uint color = notificationColor(hints);
    ActiveAlert alert;
    alert.id = key;
    alert.title = resolvedTitle;
    alert.body = resolvedBody;
    alert.color = color;

    if (m_activeAppById.contains(key))
        removeActiveAlert(key, false);
    m_activeAppById.insert(key, appLabel);
    m_activeByApp[appLabel].append(alert);

    if (m_groupByApp) {
        refreshGroupedAlert(appLabel);
        return;
    }
    publishAlert(appLabel, alert, 0);
}

void NotificationBridge::onListed(const QString &appName, uint id, const QString &summary,
                                  const QString &body, const QVariantMap &hints)
{
    considerNotification(id, appName, summary, body, hints, true);
}

void NotificationBridge::onListingStarted()
{
    m_sawCallInPoll = false;
}

void NotificationBridge::onListingFinished()
{
    if (m_callRinging && m_callSeenInList && !m_sawCallInPoll)
        stopIncomingCall();
    else if (m_callRinging && !m_callSeenInList
             && QDateTime::currentMSecsSinceEpoch() - m_lastCallMs > 120000)
        stopIncomingCall();
    if (!m_seeded) {
        m_seeded = true;
        logLine(QStringLiteral("Not forwarding %1 notification(s) already on the phone")
                .arg(m_fingerprints.size()));
        setStatus(QStringLiteral("Forwarding new phone notifications to the watch"), true);
    }
}
