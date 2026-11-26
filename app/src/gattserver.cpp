#include "gattserver.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusReply>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>
#include <QtEndian>
#include <cstring>

typedef QMap<QString, QVariantMap> InterfaceMap;
typedef QMap<QDBusObjectPath, InterfaceMap> ManagedObjectMap;
Q_DECLARE_METATYPE(InterfaceMap)
Q_DECLARE_METATYPE(ManagedObjectMap)

const char *GattServer::kServiceUuid = "f1a5e001-7c4b-4d2a-9e11-57a1f15c0001";
const char *GattServer::kMetricsUuid = "f1a5e001-7c4b-4d2a-9e11-57a1f15c0002";
const char *GattServer::kAlertUuid = "f1a5e001-7c4b-4d2a-9e11-57a1f15c0003";

static const qint64 kSyncIntervalMs = 30 * 60 * 1000;
static const qint64 kIdleConnectGapMs = 10 * 60 * 1000;
static const qint64 kEventConnectGapMs = 20 * 1000;
static const qint64 kLeReconnectGapMs = 20 * 1000;
static const int kLeReconnectMax = 6;
static const int kWriteQueueMax = 8;

static QString logFilePath()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return QDir(dir).filePath(QStringLiteral("wristlink.log"));
}

static void logLine(const QString &line)
{
    QFile file(logFilePath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        return;
    if (file.size() > 200000)
        file.resize(0);
    QTextStream out(&file);
    out << QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss.zzz"))
        << QLatin1Char(' ') << line << QLatin1Char('\n');
}

static QString uuidOf(const QVariantMap &props)
{
    return props.value(QStringLiteral("UUID")).toString().toLower();
}

GattServer::GattServer(QObject *parent)
    : QObject(parent)
    , m_timer(new QTimer(this))
    , m_lastMetricsMs(0)
    , m_syncStartedMs(0)
    , m_lastConnectAttemptMs(0)
    , m_lastBounceMs(0)
    , m_lastLeReconnectMs(0)
    , m_quietUntilMs(0)
    , m_alive(true)
    , m_ready(false)
    , m_watchLinked(false)
    , m_linkUp(false)
    , m_userHeld(false)
    , m_bouncing(false)
    , m_opInFlight(false)
    , m_hasInflight(false)
    , m_readQueued(false)
    , m_bouncedForError(false)
    , m_opEpoch(0)
    , m_failures(0)
    , m_leReconnectAttempts(0)
{
    qDBusRegisterMetaType<InterfaceMap>();
    qDBusRegisterMetaType<ManagedObjectMap>();
    connect(m_timer, SIGNAL(timeout()), this, SLOT(maintain()));
    setStatus(QStringLiteral("Starting Bluetooth…"), false);
    logLine(QStringLiteral("WristLink log %1").arg(logFilePath()));
    QTimer::singleShot(0, this, SLOT(start()));
}

GattServer::~GattServer()
{
    m_alive = false;
}

void GattServer::start()
{
    if (!m_alive)
        return;
    m_timer->start(30000);
}

void GattServer::restart()
{
    m_userHeld = false;
    m_quietUntilMs = 0;
    m_failures = 0;
    m_lastBounceMs = 0;
    m_lastLeReconnectMs = 0;
    m_leReconnectAttempts = 0;
    m_bouncedForError = false;
    dropSession();
    note(QStringLiteral("Restarting the Bluetooth connection"));
    if (m_pairedPath.isEmpty()) {
        setStatus(QStringLiteral("Pair a watch to connect"), false);
        return;
    }
    requestLeReconnect();
}

void GattServer::holdLink(bool hold)
{
    m_userHeld = hold;
    if (!hold) {
        m_quietUntilMs = 0;
        return;
    }
    m_bouncing = false;
    m_writes.clear();
    m_readQueued = false;
    m_hasInflight = false;
    m_linkUp = false;
    dropSession();
    setStatus(QStringLiteral("Watch disconnected"), true);
}

void GattServer::noteWatch(const QString &pairedPath, bool connected)
{
    if (!m_alive)
        return;
    if (m_pairedPath != pairedPath) {
        m_leReconnectAttempts = 0;
        m_lastLeReconnectMs = 0;
    }
    m_pairedPath = pairedPath;
    if (pairedPath.isEmpty()) {
        m_linkUp = false;
        dropSession();
        setStatus(QStringLiteral("Pair a watch to connect"), false);
        return;
    }

    if (m_userHeld) {
        m_linkUp = false;
        if (m_watchLinked || !m_alertPath.isEmpty() || m_opInFlight)
            dropSession();
        return;
    }

    if (!connected) {
        const bool wasUp = m_linkUp || m_watchLinked;
        m_linkUp = false;
        if (wasUp || m_opInFlight || !m_alertPath.isEmpty())
            dropSession();
        if (m_bouncing)
            return;
        if (wasUp)
            setStatus(QStringLiteral("Watch disconnected"), true);
        else if (!m_userHeld && m_lastConnectAttemptMs != 0)
            setStatus(QStringLiteral("Waiting for the watch"), true);
        if (!m_userHeld)
            requestConnect(!m_writes.isEmpty() || m_readQueued);
        return;
    }

    m_bouncing = false;
    if (!m_linkUp) {
        m_linkUp = true;
        m_failures = 0;
        dropSession();
        m_linkUp = true;
        setStatus(QStringLiteral("Watch connected"), true);
    }
    if (m_alertPath.isEmpty()) {
        if (QDateTime::currentMSecsSinceEpoch() >= m_quietUntilMs)
            discover();
    } else {
        m_leReconnectAttempts = 0;
        if (!m_opInFlight)
            pump();
    }
}

void GattServer::sendAlert(const QByteArray &payload)
{
    // Battery on its own must not bring the radio up. It rides along with a
    // notification, a call, or the half-hour sync, which connects if needed.
    if (isBattery(payload) && m_alertPath.isEmpty())
        return;
    m_quietUntilMs = 0;
    m_failures = 0;
    enqueue(payload);
}

void GattServer::readMetrics()
{
    m_readQueued = true;
    m_quietUntilMs = 0;
    if (m_metricsPath.isEmpty()) {
        if (m_linkUp)
            discover();
        else
            requestConnect(true);
        return;
    }
    pump();
}

void GattServer::maintain()
{
    if (!m_alive || !m_watchLinked || m_opInFlight || !m_writes.isEmpty())
        return;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_lastMetricsMs > 0 && now - m_lastMetricsMs < kSyncIntervalMs)
        return;
    if (m_syncStartedMs > 0 && now - m_syncStartedMs < kSyncIntervalMs)
        return;
    m_syncStartedMs = now;
    emit syncDue();
    m_readQueued = true;
    pump();
}

void GattServer::discover()
{
    if (!m_alive || !m_linkUp || m_pairedPath.isEmpty())
        return;

    QDBusInterface objectManager(QStringLiteral("org.bluez"), QStringLiteral("/"),
                                 QStringLiteral("org.freedesktop.DBus.ObjectManager"),
                                 QDBusConnection::systemBus());
    const QDBusReply<ManagedObjectMap> reply = objectManager.call(QStringLiteral("GetManagedObjects"));
    if (!reply.isValid())
        return;

    bool resolved = false;
    QString metricsPath;
    QString alertPath;
    const QString prefix = m_pairedPath + QLatin1Char('/');
    const ManagedObjectMap objects = reply.value();
    for (ManagedObjectMap::const_iterator it = objects.constBegin();
         it != objects.constEnd(); ++it) {
        const QString path = it.key().path();
        if (path == m_pairedPath) {
            const QVariantMap device = it.value().value(QStringLiteral("org.bluez.Device1"));
            resolved = device.value(QStringLiteral("ServicesResolved")).toBool();
            continue;
        }
        if (!path.startsWith(prefix))
            continue;
        const QVariantMap characteristic =
                it.value().value(QStringLiteral("org.bluez.GattCharacteristic1"));
        if (characteristic.isEmpty())
            continue;
        const QString uuid = uuidOf(characteristic);
        if (uuid == QLatin1String(kMetricsUuid))
            metricsPath = path;
        else if (uuid == QLatin1String(kAlertUuid))
            alertPath = path;
    }

    if (!metricsPath.isEmpty() && !alertPath.isEmpty()) {
        m_metricsPath = metricsPath;
        m_alertPath = alertPath;
        m_bouncedForError = false;
        m_failures = 0;
        m_leReconnectAttempts = 0;
        note(QStringLiteral("Using the paired Bluetooth connection"));
        markLinked();
        return;
    }

    if (!resolved) {
        setStatus(QStringLiteral("Watch connected"), true);
        // Dual-mode watches often finish classic SDP with no GATT. Give the
        // browse a few seconds, then switch the ACL to LE if still empty.
        if (!m_bouncing && m_leReconnectAttempts < kLeReconnectMax) {
            QTimer::singleShot(8000, this, [this]() {
                if (!m_alive || m_userHeld || !m_linkUp || !m_alertPath.isEmpty())
                    return;
                requestLeReconnect();
            });
        }
        return;
    }

    // Connected over classic (or stale GATT cache) without WristLink — Pixel
    // Watch and similar dual-mode devices need an LE reconnect.
    note(QStringLiteral("Paired link has no WristLink GATT; trying Bluetooth LE"));
    setStatus(QStringLiteral("Switching to Bluetooth LE…"), true);
    requestLeReconnect();
}

void GattServer::markLinked()
{
    if (!m_watchLinked) {
        m_syncStartedMs = QDateTime::currentMSecsSinceEpoch();
        setWatchLinked(true);
        setStatus(QStringLiteral("Watch connected"), true);
        m_readQueued = true;
        emit alertsSubscribed();
    }
    pump();
}

void GattServer::enqueue(const QByteArray &payload)
{
    if (isBattery(payload)) {
        for (int i = 0; i < m_writes.size(); ++i) {
            if (isBattery(m_writes.at(i))) {
                m_writes[i] = payload;
                m_readQueued = true;
                if (m_alertPath.isEmpty()) {
                    if (m_linkUp)
                        discover();
                    else
                        requestConnect(true);
                } else {
                    pump();
                }
                return;
            }
        }
    }
    while (m_writes.size() >= kWriteQueueMax)
        m_writes.removeFirst();
    m_writes.append(payload);
    m_readQueued = true;
    if (m_alertPath.isEmpty()) {
        if (m_linkUp)
            discover();
        else
            requestConnect(true);
        return;
    }
    pump();
}

void GattServer::pump()
{
    if (m_opInFlight)
        return;
    if (!m_writes.isEmpty()) {
        if (!m_alertPath.isEmpty())
            writeFront();
        return;
    }
    if (m_readQueued && !m_metricsPath.isEmpty()) {
        m_readQueued = false;
        readNow();
    }
}

void GattServer::writeFront()
{
    if (m_opInFlight || m_writes.isEmpty() || m_alertPath.isEmpty())
        return;
    m_inflight = m_writes.takeFirst();
    m_hasInflight = true;
    m_opInFlight = true;
    const int epoch = ++m_opEpoch;
    const QByteArray payload = m_inflight;

    QDBusMessage message = QDBusMessage::createMethodCall(
                QStringLiteral("org.bluez"), m_alertPath,
                QStringLiteral("org.bluez.GattCharacteristic1"),
                QStringLiteral("WriteValue"));
    message << payload << QVariantMap();
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(
                QDBusConnection::systemBus().asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, epoch](QDBusPendingCallWatcher *call) {
        const QDBusPendingReply<> reply = *call;
        call->deleteLater();
        const QString error = reply.isError()
                ? QStringLiteral("%1 (%2)").arg(reply.error().message(), reply.error().name())
                : QString();
        finishOp(epoch, !reply.isError(), error, QByteArray());
    });
    QTimer::singleShot(15000, this, [this, epoch]() {
        if (!m_alive || epoch != m_opEpoch || !m_opInFlight)
            return;
        finishOp(epoch, false, QStringLiteral("Bluetooth write timed out"), QByteArray());
    });
}

void GattServer::readNow()
{
    if (m_opInFlight || m_metricsPath.isEmpty())
        return;
    m_opInFlight = true;
    m_hasInflight = false;
    const int epoch = ++m_opEpoch;
    QDBusMessage message = QDBusMessage::createMethodCall(
                QStringLiteral("org.bluez"), m_metricsPath,
                QStringLiteral("org.bluez.GattCharacteristic1"),
                QStringLiteral("ReadValue"));
    message << QVariantMap();
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(
                QDBusConnection::systemBus().asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, epoch](QDBusPendingCallWatcher *call) {
        QDBusPendingReply<QByteArray> reply = *call;
        call->deleteLater();
        if (reply.isError()) {
            finishOp(epoch, false,
                     QStringLiteral("%1 (%2)").arg(reply.error().message(), reply.error().name()),
                     QByteArray());
            return;
        }
        finishOp(epoch, true, QString(), reply.value());
    });
    QTimer::singleShot(15000, this, [this, epoch]() {
        if (!m_alive || epoch != m_opEpoch || !m_opInFlight)
            return;
        finishOp(epoch, false, QStringLiteral("Bluetooth read timed out"), QByteArray());
    });
}

void GattServer::finishOp(int epoch, bool ok, const QString &error, const QByteArray &metrics)
{
    if (!m_alive || epoch != m_opEpoch || !m_opInFlight)
        return;
    ++m_opEpoch;
    m_opInFlight = false;
    if (!ok) {
        restoreInflight();
        m_alertPath.clear();
        m_metricsPath.clear();
        setWatchLinked(false);
        note(error);
        ++m_failures;
        if (m_failures >= 3)
            m_quietUntilMs = QDateTime::currentMSecsSinceEpoch() + kIdleConnectGapMs;
        if (m_linkUp && !m_bouncedForError) {
            m_bouncedForError = true;
            bounce();
        } else if (!m_linkUp) {
            requestConnect(true);
        }
        return;
    }
    m_failures = 0;
    m_hasInflight = false;
    if (!metrics.isEmpty())
        takeMetrics(metrics);
    else
        note(QStringLiteral("Sent %1 bytes").arg(m_inflight.size()));
    pump();
}

void GattServer::restoreInflight()
{
    if (!m_hasInflight)
        return;
    m_writes.prepend(m_inflight);
    m_hasInflight = false;
}

void GattServer::dropSession()
{
    ++m_opEpoch;
    m_opInFlight = false;
    restoreInflight();
    m_alertPath.clear();
    m_metricsPath.clear();
    setWatchLinked(false);
}

void GattServer::requestConnect(bool now)
{
    if (m_userHeld || m_bouncing || m_pairedPath.isEmpty() || m_linkUp)
        return;
    const qint64 t = QDateTime::currentMSecsSinceEpoch();
    if (m_quietUntilMs != 0 && t < m_quietUntilMs)
        return;
    const qint64 gap = now ? kEventConnectGapMs : kIdleConnectGapMs;
    if (m_lastConnectAttemptMs != 0 && t - m_lastConnectAttemptMs < gap)
        return;
    m_lastConnectAttemptMs = t;
    setStatus(QStringLiteral("Connecting to the watch…"), true);
    emit connectRequested(m_pairedPath);
}

void GattServer::bounce()
{
    requestLeReconnect();
}

void GattServer::requestLeReconnect()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_bouncing || m_userHeld || m_pairedPath.isEmpty())
        return;
    if (m_leReconnectAttempts >= kLeReconnectMax) {
        setStatus(QStringLiteral("Watch connected — open WristLink on the watch"), true);
        return;
    }
    if (m_lastLeReconnectMs != 0 && now - m_lastLeReconnectMs < kLeReconnectGapMs)
        return;
    m_lastLeReconnectMs = now;
    m_lastBounceMs = now;
    ++m_leReconnectAttempts;
    m_bouncing = true;
    m_linkUp = false;
    dropSession();
    note(QStringLiteral("Reconnecting over Bluetooth LE (%1/%2)")
         .arg(m_leReconnectAttempts).arg(kLeReconnectMax));
    setStatus(QStringLiteral("Switching to Bluetooth LE…"), true);
    emit reconnectRequested(m_pairedPath);
    QTimer::singleShot(25000, this, [this]() {
        m_bouncing = false;
    });
}

void GattServer::takeMetrics(const QByteArray &value)
{
    m_lastMetricsMs = QDateTime::currentMSecsSinceEpoch();
    if (value.size() < 18) {
        note(QStringLiteral("Watch returned %1 bytes").arg(value.size()));
        return;
    }
    quint32 steps = 0;
    memcpy(&steps, value.constData(), 4);
    steps = qFromLittleEndian(steps);
    note(QStringLiteral("Steps synced (%1)").arg(steps));
    emit metricsReceived(value);
}

bool GattServer::isBattery(const QByteArray &payload)
{
    return payload.size() >= 2 && uchar(payload.at(1)) == 0x04;
}

void GattServer::note(const QString &line)
{
    if (!m_notes.isEmpty() && m_notes.last() == line)
        return;
    logLine(line);
    m_notes.append(line);
    while (m_notes.size() > 8)
        m_notes.removeFirst();
    m_detail = m_notes.join(QStringLiteral("\n"));
    emit detailChanged();
}

void GattServer::setStatus(const QString &text, bool ready)
{
    if (m_status == text && m_ready == ready)
        return;
    m_status = text;
    m_ready = ready;
    emit statusChanged();
}

void GattServer::setWatchLinked(bool linked)
{
    if (m_watchLinked == linked)
        return;
    m_watchLinked = linked;
    emit watchLinkedChanged();
}
