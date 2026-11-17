#include "watchhealth.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDateTime>
#include <QtEndian>
#include <cstring>

void WatchHealth::appendUtf8Field(QByteArray &out, const QString &text, int maxBytes)
{
    QByteArray utf8 = text.toUtf8();
    if (utf8.size() > maxBytes)
        utf8.truncate(maxBytes);
    const quint16 len = quint16(utf8.size());
    out.append(char(len & 0xff));
    out.append(char((len >> 8) & 0xff));
    out.append(utf8);
}

WatchHealth::WatchHealth(QObject *parent)
    : QObject(parent)
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
    , m_usingWatchApp(false)
    , m_phoneBattery(-1)
    , m_phoneCharging(false)
    , m_phoneBatteryValid(false)
{
    QDBusConnection bus = QDBusConnection::systemBus();
    bus.connect(QStringLiteral("com.nokia.mce"),
                QStringLiteral("/com/nokia/mce/signal"),
                QStringLiteral("com.nokia.mce.signal"),
                QStringLiteral("battery_level_ind"),
                this, SLOT(onBatteryLevel(int)));
    bus.connect(QStringLiteral("com.nokia.mce"),
                QStringLiteral("/com/nokia/mce/signal"),
                QStringLiteral("com.nokia.mce.signal"),
                QStringLiteral("charger_state_ind"),
                this, SLOT(onChargerState(QString)));
    refreshPhoneBattery();
    setStatus(QStringLiteral("Pair the watch and leave WristLink open on it"));
}

void WatchHealth::sync(const QString &devicePath)
{
    if (!devicePath.isEmpty())
        m_devicePath = devicePath;
    setSyncing(true);
    emit syncRequested();
    setSyncing(false);
}

void WatchHealth::stop()
{
    m_devicePath.clear();
    setSyncing(false);
    if (!m_usingWatchApp)
        setStatus(QStringLiteral("Watch disconnected"));
}

void WatchHealth::onGattStatus(const QString &text)
{
    if (!text.isEmpty())
        setStatus(text);
}

void WatchHealth::onWatchMetrics(const QByteArray &value)
{
    applyWristLinkMetrics(value);
}

void WatchHealth::applyWristLinkMetrics(const QByteArray &value)
{
    // Packed LE payload: steps, calories, distance, hr, floors, battery, flags (18 bytes).
    if (value.size() < 18)
        return;

    const char *p = value.constData();
    quint32 steps = 0;
    quint32 calories = 0;
    quint32 distance = 0;
    quint16 heart = 0;
    quint16 floors = 0;
    memcpy(&steps, p, 4);
    memcpy(&calories, p + 4, 4);
    memcpy(&distance, p + 8, 4);
    memcpy(&heart, p + 12, 2);
    memcpy(&floors, p + 14, 2);
    steps = qFromLittleEndian(steps);
    calories = qFromLittleEndian(calories);
    distance = qFromLittleEndian(distance);
    heart = qFromLittleEndian(heart);
    floors = qFromLittleEndian(floors);
    const quint8 battery = uchar(p[16]);
    const quint8 flags = uchar(p[17]);

    if ((flags & 0x01) || steps > 0) {
        m_steps = int(steps);
        m_stepsValid = true;
    }
    if (flags & 0x02) {
        m_calories = int(calories);
        m_caloriesValid = true;
    }
    if (flags & 0x04) {
        m_distanceMeters = int(distance);
        m_distanceValid = true;
    }
    if (flags & 0x08) {
        m_heartRate = int(heart);
        m_heartRateValid = true;
    }
    if (flags & 0x10) {
        m_floors = int(floors);
        m_floorsValid = true;
    }
    if (flags & 0x20) {
        m_batteryPercent = int(battery);
        m_batteryValid = true;
    }

    m_usingWatchApp = true;
    m_lastUpdated = QDateTime::currentDateTime().toString(QStringLiteral("hh:mm"));
    setStatus(QStringLiteral("Synced from the watch"));
    emit metricsChanged();
}

static void appendId(QByteArray &payload, uint id)
{
    payload.append(char(id & 0xff));
    payload.append(char((id >> 8) & 0xff));
    payload.append(char((id >> 16) & 0xff));
    payload.append(char((id >> 24) & 0xff));
}

void WatchHealth::forwardNotification(const QString &app, const QString &title,
                                      const QString &body, uint id, uint color)
{
    QByteArray payload;
    payload.append(char(1));
    payload.append(char(0));
    appendId(payload, id);
    appendUtf8Field(payload, app, 80);
    appendUtf8Field(payload, title, 120);
    appendUtf8Field(payload, body, 180);
    appendId(payload, color);
    emit alertOutgoing(payload);
    sendPhoneBattery();
}

void WatchHealth::onBatteryLevel(int percent)
{
    if (percent < 0 || percent > 100)
        return;
    if (m_phoneBatteryValid && m_phoneBattery == percent)
        return;
    m_phoneBattery = percent;
    m_phoneBatteryValid = true;
}

void WatchHealth::onChargerState(const QString &state)
{
    const bool charging = state == QLatin1String("on");
    if (m_phoneCharging == charging)
        return;
    m_phoneCharging = charging;
}

void WatchHealth::refreshPhoneBattery()
{
    QDBusConnection bus = QDBusConnection::systemBus();
    QDBusMessage levelCall = QDBusMessage::createMethodCall(
                QStringLiteral("com.nokia.mce"),
                QStringLiteral("/com/nokia/mce/request"),
                QStringLiteral("com.nokia.mce.request"),
                QStringLiteral("get_battery_level"));
    const QDBusMessage levelReply = bus.call(levelCall, QDBus::Block, 1000);
    if (levelReply.type() == QDBusMessage::ReplyMessage && !levelReply.arguments().isEmpty()) {
        const int percent = levelReply.arguments().at(0).toInt();
        if (percent >= 0 && percent <= 100) {
            m_phoneBattery = percent;
            m_phoneBatteryValid = true;
        }
    }
    QDBusMessage chargerCall = QDBusMessage::createMethodCall(
                QStringLiteral("com.nokia.mce"),
                QStringLiteral("/com/nokia/mce/request"),
                QStringLiteral("com.nokia.mce.request"),
                QStringLiteral("get_charger_state"));
    const QDBusMessage chargerReply = bus.call(chargerCall, QDBus::Block, 1000);
    if (chargerReply.type() == QDBusMessage::ReplyMessage && !chargerReply.arguments().isEmpty())
        m_phoneCharging = chargerReply.arguments().at(0).toString() == QLatin1String("on");
}

void WatchHealth::sendPhoneBattery()
{
    if (!m_phoneBatteryValid)
        refreshPhoneBattery();
    if (!m_phoneBatteryValid)
        return;
    QByteArray payload;
    payload.append(char(1));
    payload.append(char(0x04));
    payload.append(char(qBound(0, m_phoneBattery, 100)));
    payload.append(char(m_phoneCharging ? 1 : 0));
    emit alertOutgoing(payload);
}

void WatchHealth::forwardCall(bool ringing, const QString &caller)
{
    QByteArray payload;
    payload.append(char(1));
    payload.append(char(ringing ? 0x02 : 0x01));
    appendId(payload, 0x43414c4c);
    const QString name = caller.isEmpty() ? QStringLiteral("Incoming call") : caller;
    appendUtf8Field(payload, QStringLiteral("Phone"), 32);
    appendUtf8Field(payload, ringing ? name : QString(), 64);
    appendUtf8Field(payload, ringing ? QStringLiteral("Incoming call") : QString(), 32);
    emit alertOutgoing(payload);
    sendPhoneBattery();
}

void WatchHealth::setSyncing(bool value)
{
    if (m_syncing == value)
        return;
    m_syncing = value;
    emit syncingChanged();
}

void WatchHealth::setStatus(const QString &text)
{
    if (m_status == text)
        return;
    m_status = text;
    emit statusChanged();
}

void WatchHealth::setLastError(const QString &text)
{
    if (m_lastError == text)
        return;
    m_lastError = text;
    emit lastErrorChanged();
}
