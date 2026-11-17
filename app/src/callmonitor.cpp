#include "callmonitor.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QProcess>
#include <QStandardPaths>
#include <QTextStream>
#include <QTimer>
#include <QVariantMap>

static void logLine(const QString &line)
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    QFile file(QDir(dir).filePath(QStringLiteral("wristlink.log")));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append))
        return;
    QTextStream out(&file);
    out << QDateTime::currentDateTime().toString(QStringLiteral("hh:mm:ss.zzz"))
        << QLatin1Char(' ') << line << QLatin1Char('\n');
}

static QString callerLabel(const QVariantMap &properties)
{
    const QString name = properties.value(QStringLiteral("Name")).toString().trimmed();
    if (!name.isEmpty())
        return name;
    const QString line = properties.value(QStringLiteral("LineIdentification")).toString().trimmed();
    if (!line.isEmpty())
        return line;
    return QStringLiteral("Incoming call");
}

static bool isRingingState(const QString &state)
{
    return state == QLatin1String("incoming") || state == QLatin1String("waiting");
}

static bool looksLikeNumber(const QString &text)
{
    int digits = 0;
    for (int i = 0; i < text.size(); ++i) {
        const QChar c = text.at(i);
        if (c.isDigit())
            ++digits;
        else if (c != QLatin1Char('+') && c != QLatin1Char(' ') && c != QLatin1Char('-')
                 && c != QLatin1Char('(') && c != QLatin1Char(')'))
            return false;
    }
    return digits >= 5;
}

static QString contactName(const QString &label)
{
    const QString trimmed = label.trimmed();
    if (!looksLikeNumber(trimmed))
        return trimmed;

    static QHash<QString, QString> cache;
    if (cache.contains(trimmed)) {
        const QString cached = cache.value(trimmed);
        return cached.isEmpty() ? trimmed : cached;
    }

    QProcess process;
    process.start(QStringLiteral("/usr/libexec/harbour-wristlink-contact"),
                  QStringList() << trimmed);
    if (!process.waitForFinished(1500) || process.exitCode() != 0) {
        process.kill();
        cache.insert(trimmed, QString());
        return trimmed;
    }
    const QString name = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
    cache.insert(trimmed, name);
    return name.isEmpty() ? trimmed : name;
}

CallMonitor::CallMonitor(QObject *parent)
    : QObject(parent)
    , m_repeat(new QTimer(this))
    , m_ofonoTimer(new QTimer(this))
    , m_ringing(false)
    , m_ofonoReady(false)
    , m_refreshing(false)
    , m_ofonoErrorLogged(false)
    , m_stopBursts(0)
{
    m_repeat->setInterval(2000);
    connect(m_repeat, SIGNAL(timeout()), this, SLOT(pulse()));
    m_ofonoTimer->setInterval(1000);
    connect(m_ofonoTimer, SIGNAL(timeout()), this, SLOT(refreshCalls()));
    m_ofonoTimer->start();
    setStatus(QStringLiteral("Watch will ring when the phone rings"));
}

void CallMonitor::setIncoming(bool incoming, const QString &caller)
{
    if (m_ofonoReady)
        return;
    applyRinging(incoming, caller);
}

QStringList CallMonitor::voiceModems()
{
    QDBusMessage message = QDBusMessage::createMethodCall(
                QStringLiteral("org.ofono"), QStringLiteral("/"),
                QStringLiteral("org.ofono.Manager"), QStringLiteral("GetModems"));
    const QDBusMessage reply = QDBusConnection::systemBus().call(message, QDBus::Block, 2000);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        if (!m_ofonoErrorLogged) {
            m_ofonoErrorLogged = true;
            const QString reason = reply.type() == QDBusMessage::ErrorMessage
                    ? reply.errorMessage()
                    : QStringLiteral("no reply");
            logLine(QStringLiteral("Phone call watch failed: %1").arg(reason));
        }
        return QStringList();
    }

    QStringList modems;
    const QDBusArgument list = reply.arguments().at(0).value<QDBusArgument>();
    list.beginArray();
    while (!list.atEnd()) {
        QDBusObjectPath path;
        QVariantMap properties;
        list.beginStructure();
        list >> path >> properties;
        list.endStructure();
        QStringList interfaces = properties.value(QStringLiteral("Interfaces")).toStringList();
        if (interfaces.isEmpty()) {
            const QVariant raw = properties.value(QStringLiteral("Interfaces"));
            if (raw.canConvert<QDBusArgument>()) {
                const QDBusArgument names = raw.value<QDBusArgument>();
                names.beginArray();
                while (!names.atEnd()) {
                    QString name;
                    names >> name;
                    interfaces.append(name);
                }
                names.endArray();
            }
        }
        if (interfaces.contains(QStringLiteral("org.ofono.VoiceCallManager")))
            modems.append(path.path());
    }
    list.endArray();
    return modems;
}

void CallMonitor::subscribeModem(const QString &path)
{
    if (m_modems.contains(path))
        return;
    QDBusConnection bus = QDBusConnection::systemBus();
    bus.connect(QStringLiteral("org.ofono"), path,
                QStringLiteral("org.ofono.VoiceCallManager"), QStringLiteral("CallAdded"),
                this, SLOT(refreshCalls()));
    bus.connect(QStringLiteral("org.ofono"), path,
                QStringLiteral("org.ofono.VoiceCallManager"), QStringLiteral("CallRemoved"),
                this, SLOT(refreshCalls()));
    m_modems.insert(path);
    logLine(QStringLiteral("Watching phone calls on %1").arg(path));
}

void CallMonitor::subscribeCall(const QString &path)
{
    if (m_watchedCalls.contains(path))
        return;
    QDBusConnection::systemBus().connect(
                QStringLiteral("org.ofono"), path,
                QStringLiteral("org.ofono.VoiceCall"), QStringLiteral("PropertyChanged"),
                this, SLOT(refreshCalls()));
    m_watchedCalls.insert(path);
}

void CallMonitor::refreshCalls()
{
    if (m_refreshing)
        return;
    m_refreshing = true;

    const QStringList modems = voiceModems();
    bool ringing = false;
    QString caller;
    foreach (const QString &modem, modems) {
        subscribeModem(modem);
        QDBusMessage message = QDBusMessage::createMethodCall(
                    QStringLiteral("org.ofono"), modem,
                    QStringLiteral("org.ofono.VoiceCallManager"), QStringLiteral("GetCalls"));
        const QDBusMessage reply = QDBusConnection::systemBus().call(message, QDBus::Block, 2000);
        if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
            continue;
        m_ofonoReady = true;
        const QDBusArgument list = reply.arguments().at(0).value<QDBusArgument>();
        list.beginArray();
        while (!list.atEnd()) {
            QDBusObjectPath path;
            QVariantMap properties;
            list.beginStructure();
            list >> path >> properties;
            list.endStructure();
            subscribeCall(path.path());
            const QString state = properties.value(QStringLiteral("State")).toString();
            if (!ringing && isRingingState(state)) {
                ringing = true;
                caller = contactName(callerLabel(properties));
            }
        }
        list.endArray();
    }

    if (m_ofonoReady)
        applyRinging(ringing, ringing ? caller : m_caller);
    m_refreshing = false;
}

void CallMonitor::pulse()
{
    if (m_ringing)
        emit callAlert(true, m_caller);
}

void CallMonitor::flushStop()
{
    if (m_ringing || m_stopBursts <= 0)
        return;
    emit callAlert(false, m_caller);
    --m_stopBursts;
    if (m_stopBursts > 0)
        QTimer::singleShot(700, this, SLOT(flushStop()));
}

void CallMonitor::setStatus(const QString &text)
{
    if (m_status == text)
        return;
    m_status = text;
    emit statusChanged();
}

void CallMonitor::applyRinging(bool ringing, const QString &caller)
{
    if (ringing) {
        const QString name = caller.isEmpty() ? QStringLiteral("Incoming call") : caller;
        const bool changedCaller = m_caller != name;
        m_caller = name;
        m_stopBursts = 0;
        if (!m_ringing) {
            m_ringing = true;
            m_repeat->start();
            emit ringingChanged();
            setStatus(QStringLiteral("Ringing the watch — %1").arg(m_caller));
            logLine(QStringLiteral("Phone is ringing: %1").arg(m_caller));
            emit callAlert(true, m_caller);
        } else if (changedCaller) {
            emit ringingChanged();
            setStatus(QStringLiteral("Ringing the watch — %1").arg(m_caller));
            emit callAlert(true, m_caller);
        }
        return;
    }
    if (!m_ringing)
        return;
    m_ringing = false;
    m_repeat->stop();
    emit ringingChanged();
    setStatus(QStringLiteral("Watch will ring when the phone rings"));
    logLine(QStringLiteral("Phone stopped ringing"));
    m_stopBursts = 3;
    flushStop();
}
