#include "bluezmanager.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusError>
#include <QDBusInterface>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusReply>
#include <QDBusVariant>
#include <QTimer>
#include <algorithm>

typedef QMap<QString, QVariantMap> InterfaceMap;
typedef QMap<QDBusObjectPath, InterfaceMap> ManagedObjectMap;
Q_DECLARE_METATYPE(InterfaceMap)
Q_DECLARE_METATYPE(ManagedObjectMap)

static const char *kAgentPath = "/org/sailfishos/WristLink/agent";
// Same UUID the Wear OS app advertises (GattServer::kServiceUuid).
static const char *kWristLinkServiceUuid = "f1a5e001-7c4b-4d2a-9e11-57a1f15c0001";

static bool isPermissionError(const QString &name)
{
    return name == QLatin1String("org.freedesktop.DBus.Error.AuthFailed")
            || name == QLatin1String("org.freedesktop.DBus.Error.AccessDenied");
}

// "watch" / "smartwatch" as a whole word (not "Swatch"), optional trailing digits.
static bool nameHasWatchWord(const QString &name)
{
    const QString lower = name.toLower();
    int from = 0;
    while (from < lower.size()) {
        const int idx = lower.indexOf(QLatin1String("watch"), from);
        if (idx < 0)
            return false;
        const bool smartPrefix = idx >= 5
                && lower.mid(idx - 5, 5) == QLatin1String("smart");
        const int start = smartPrefix ? idx - 5 : idx;
        const bool boundaryBefore = start == 0
                || !lower.at(start - 1).isLetterOrNumber();
        int end = idx + 5;
        while (end < lower.size() && lower.at(end).isDigit())
            ++end;
        const bool boundaryAfter = end >= lower.size()
                || !lower.at(end).isLetterOrNumber();
        if (boundaryBefore && boundaryAfter)
            return true;
        from = idx + 1;
    }
    return false;
}

static bool deviceAdvertisesWristLink(const QVariantMap &properties)
{
    const QVariant uuidsVar = properties.value(QStringLiteral("UUIDs"));
    QStringList uuids;
    if (uuidsVar.canConvert<QStringList>()) {
        uuids = uuidsVar.toStringList();
    } else if (uuidsVar.canConvert<QVariantList>()) {
        foreach (const QVariant &entry, uuidsVar.toList())
            uuids.append(entry.toString());
    }
    foreach (const QString &uuid, uuids) {
        if (uuid.compare(QLatin1String(kWristLinkServiceUuid), Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

static bool isWearableDevice(const QVariantMap &properties, const QString &name)
{
    if (properties.value(QStringLiteral("Icon")).toString()
            .compare(QStringLiteral("watch"), Qt::CaseInsensitive) == 0)
        return true;

    // Bluetooth GAP Appearance: 0x00C0–0x00CF = watch subcategory.
    if (properties.contains(QStringLiteral("Appearance"))) {
        const uint appearance = properties.value(QStringLiteral("Appearance")).toUInt();
        if (appearance >= 0x00C0 && appearance <= 0x00CF)
            return true;
    }

    // Class of Device: Major = Wearable (7), Minor = Wrist Watch (1) or unclassified (0).
    if (properties.contains(QStringLiteral("Class"))) {
        const uint cod = properties.value(QStringLiteral("Class")).toUInt();
        const uint major = (cod >> 8) & 0x1F;
        const uint minor = (cod >> 2) & 0x3F;
        if (major == 7 && (minor == 0 || minor == 1))
            return true;
    }

    if (deviceAdvertisesWristLink(properties))
        return true;

    if (nameHasWatchWord(name))
        return true;

    // Samsung model codes that omit "Watch" (Galaxy Watch 4–7 era).
    static const char *const modelHints[] = {
        "SM-L3", "SM-L7", "SM-R8", "SM-R9", "SM-R5", "SM-R4",
        0
    };
    for (int i = 0; modelHints[i]; ++i) {
        if (name.contains(QLatin1String(modelHints[i]), Qt::CaseInsensitive))
            return true;
    }

    // Wear OS brands whose default names may not include the word "Watch".
    static const char *const brandHints[] = {
        "TicWatch", "Mobvoi", "Skagen Falster", "Emporio Armani",
        "TAG Heuer", "Wear OS", "WearOS",
        0
    };
    for (int i = 0; brandHints[i]; ++i) {
        if (name.contains(QLatin1String(brandHints[i]), Qt::CaseInsensitive))
            return true;
    }

    return false;
}

static bool isInProgressError(const QDBusError &error)
{
    if (error.name() == QLatin1String("org.bluez.Error.InProgress"))
        return true;
    return error.name() == QLatin1String("org.bluez.Error.Failed")
            && error.message().contains(QStringLiteral("already in progress"),
                                        Qt::CaseInsensitive);
}

PairingAgent::PairingAgent(QObject *parent)
    : QObject(parent)
    , m_decisionLoop(0)
    , m_accepted(false)
{
}

void PairingAgent::accept()
{
    m_accepted = true;
    if (m_decisionLoop)
        m_decisionLoop->quit();
}

void PairingAgent::reject()
{
    m_accepted = false;
    if (m_decisionLoop)
        m_decisionLoop->quit();
}

bool PairingAgent::waitForDecision()
{
    m_accepted = false;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    connect(&timeout, SIGNAL(timeout()), &loop, SLOT(quit()));
    m_decisionLoop = &loop;
    timeout.start(30000);
    loop.exec();
    m_decisionLoop = 0;
    return m_accepted;
}

void PairingAgent::Release()
{
}

QString PairingAgent::RequestPinCode(const QDBusObjectPath &device)
{
    emit confirmationRequested(device.path(), QStringLiteral("000000"));
    if (!waitForDecision()) {
        sendErrorReply(QStringLiteral("org.bluez.Error.Rejected"),
                       QStringLiteral("Pairing was rejected"));
        return QString();
    }
    return QStringLiteral("0000");
}

uint PairingAgent::RequestPasskey(const QDBusObjectPath &device)
{
    emit confirmationRequested(device.path(), QStringLiteral("000000"));
    if (!waitForDecision()) {
        sendErrorReply(QStringLiteral("org.bluez.Error.Rejected"),
                       QStringLiteral("Pairing was rejected"));
    }
    return 0;
}

void PairingAgent::DisplayPinCode(const QDBusObjectPath &device, const QString &pinCode)
{
    emit confirmationRequested(device.path(), pinCode);
}

void PairingAgent::DisplayPasskey(const QDBusObjectPath &device, uint passkey, ushort entered)
{
    Q_UNUSED(entered)
    emit confirmationRequested(device.path(),
                               QStringLiteral("%1").arg(passkey, 6, 10, QLatin1Char('0')));
}

void PairingAgent::RequestConfirmation(const QDBusObjectPath &device, uint passkey)
{
    emit confirmationRequested(device.path(),
                               QStringLiteral("%1").arg(passkey, 6, 10, QLatin1Char('0')));
    if (!waitForDecision()) {
        sendErrorReply(QStringLiteral("org.bluez.Error.Rejected"),
                       QStringLiteral("Pairing was rejected"));
    }
}

void PairingAgent::RequestAuthorization(const QDBusObjectPath &device)
{
    Q_UNUSED(device)
}

void PairingAgent::AuthorizeService(const QDBusObjectPath &device, const QString &uuid)
{
    Q_UNUSED(device)
    Q_UNUSED(uuid)
}

void PairingAgent::Cancel()
{
    reject();
}

BluezManager::BluezManager(QObject *parent)
    : QObject(parent)
    , m_discovering(false)
    , m_bluetoothAvailable(false)
    , m_busy(false)
    , m_refreshTimer(new QTimer(this))
    , m_pairingAgent(new PairingAgent(this))
    , m_agentReady(false)
    , m_agentRegistered(false)
    , m_methodRetries(0)
{
    qDBusRegisterMetaType<InterfaceMap>();
    qDBusRegisterMetaType<ManagedObjectMap>();

    connect(m_refreshTimer, SIGNAL(timeout()), this, SLOT(refresh()));
    connect(m_pairingAgent, SIGNAL(confirmationRequested(QString,QString)),
            this, SLOT(onConfirmationRequested(QString,QString)));
    registerAgent();
    refresh();
    m_refreshTimer->start(15000);
}

BluezManager::~BluezManager()
{
    if (m_agentRegistered) {
        QDBusInterface manager(QStringLiteral("org.bluez"), QStringLiteral("/org/bluez"),
                               QStringLiteral("org.bluez.AgentManager1"),
                               QDBusConnection::systemBus());
        manager.call(QStringLiteral("UnregisterAgent"),
                     QVariant::fromValue(QDBusObjectPath(QLatin1String(kAgentPath))));
    }
    QDBusConnection::systemBus().unregisterObject(QLatin1String(kAgentPath));
}

QVariantList BluezManager::devices() const { return m_devices; }
bool BluezManager::discovering() const { return m_discovering; }
bool BluezManager::bluetoothAvailable() const { return m_bluetoothAvailable; }
bool BluezManager::busy() const { return m_busy; }
QString BluezManager::statusMessage() const { return m_statusMessage; }
QString BluezManager::confirmationCode() const { return m_confirmationCode; }
QString BluezManager::confirmationDevice() const { return m_confirmationDevice; }

QString BluezManager::connectedWatchPath() const
{
    return m_connectedWatchPath;
}

QString BluezManager::pairedWatchPath() const
{
    // devices() is sorted with named watches first, matching the home screen.
    foreach (const QVariant &entry, m_devices) {
        const QVariantMap device = entry.toMap();
        if (device.value(QStringLiteral("paired")).toBool())
            return device.value(QStringLiteral("path")).toString();
    }
    return QString();
}

bool BluezManager::watchIsConnected() const
{
    const QString path = pairedWatchPath();
    if (path.isEmpty())
        return false;
    foreach (const QVariant &entry, m_devices) {
        const QVariantMap device = entry.toMap();
        if (device.value(QStringLiteral("path")).toString() == path)
            return device.value(QStringLiteral("connected")).toBool();
    }
    return false;
}

void BluezManager::registerAgent()
{
    QDBusConnection bus = QDBusConnection::systemBus();
    if (!bus.isConnected()) {
        setAgentStatus(QStringLiteral("No system bus: %1").arg(bus.lastError().message()),
                       false);
        return;
    }

    if (!bus.registerObject(QLatin1String(kAgentPath), m_pairingAgent,
                            QDBusConnection::ExportAllSlots)) {
        setAgentStatus(QStringLiteral("Could not export agent: %1")
                       .arg(bus.lastError().message()), false);
        return;
    }

    QDBusInterface manager(QStringLiteral("org.bluez"), QStringLiteral("/org/bluez"),
                           QStringLiteral("org.bluez.AgentManager1"), bus);
    const QDBusMessage registered =
            manager.call(QStringLiteral("RegisterAgent"),
                         QVariant::fromValue(QDBusObjectPath(QLatin1String(kAgentPath))),
                         QStringLiteral("KeyboardDisplay"));
    if (registered.type() == QDBusMessage::ErrorMessage
            && registered.errorName() != QLatin1String("org.bluez.Error.AlreadyExists")) {
        // Sailjail allows talking to BlueZ, but not registering a pairing agent.
        // Pair() still works: Sailfish's system agent shows the confirmation dialog.
        if (isPermissionError(registered.errorName())) {
            setAgentStatus(QStringLiteral("Confirm pairing in the Sailfish Bluetooth dialog"),
                           true);
        } else {
            setAgentStatus(QStringLiteral("BlueZ refused the agent: %1 (%2)")
                           .arg(registered.errorMessage(), registered.errorName()), false);
        }
        return;
    }

    m_agentRegistered = true;

    // Becoming the default agent is optional. Pair() we start still uses our
    // agent; incoming pairing may still go through the Sailfish dialog.
    const QDBusMessage madeDefault =
            manager.call(QStringLiteral("RequestDefaultAgent"),
                         QVariant::fromValue(QDBusObjectPath(QLatin1String(kAgentPath))));
    if (madeDefault.type() == QDBusMessage::ErrorMessage) {
        setAgentStatus(QStringLiteral("Pairing agent ready. If a code appears, confirm it here or in the Sailfish Bluetooth dialog."),
                       true);
        return;
    }

    setAgentStatus(QStringLiteral("Pairing agent ready"), true);
}

QString BluezManager::agentStatus() const { return m_agentStatus; }
bool BluezManager::agentReady() const { return m_agentReady; }

void BluezManager::setAgentStatus(const QString &message, bool ready)
{
    if (m_agentStatus == message && m_agentReady == ready)
        return;
    m_agentStatus = message;
    m_agentReady = ready;
    emit agentStatusChanged();
}

void BluezManager::refresh()
{
    QDBusInterface objectManager(QStringLiteral("org.bluez"), QStringLiteral("/"),
                                 QStringLiteral("org.freedesktop.DBus.ObjectManager"),
                                 QDBusConnection::systemBus());
    QDBusReply<ManagedObjectMap> reply = objectManager.call(QStringLiteral("GetManagedObjects"));
    if (!reply.isValid()) {
        bool changed = m_bluetoothAvailable;
        m_bluetoothAvailable = false;
        if (changed)
            emit bluetoothAvailableChanged();
        setStatusMessage(QStringLiteral("Bluetooth service is unavailable"));
        return;
    }

    QVariantList found;
    QString adapterPath;
    bool adapterDiscovering = false;
    const ManagedObjectMap objects = reply.value();
    ManagedObjectMap::const_iterator object = objects.constBegin();
    for (; object != objects.constEnd(); ++object) {
        const InterfaceMap interfaces = object.value();
        if (interfaces.contains(QStringLiteral("org.bluez.Adapter1"))) {
            adapterPath = object.key().path();
            const QVariantMap adapter = interfaces.value(QStringLiteral("org.bluez.Adapter1"));
            adapterDiscovering = adapter.value(QStringLiteral("Discovering")).toBool();
        }
        if (!interfaces.contains(QStringLiteral("org.bluez.Device1")))
            continue;

        const QVariantMap properties = interfaces.value(QStringLiteral("org.bluez.Device1"));
        QVariantMap device;
        QString name = properties.value(QStringLiteral("Alias")).toString();
        if (name.isEmpty())
            name = properties.value(QStringLiteral("Name")).toString();
        if (name.isEmpty())
            name = properties.value(QStringLiteral("Address")).toString();
        device.insert(QStringLiteral("name"), name);
        device.insert(QStringLiteral("address"), properties.value(QStringLiteral("Address")));
        device.insert(QStringLiteral("path"), object.key().path());
        device.insert(QStringLiteral("paired"), properties.value(QStringLiteral("Paired"), false));
        device.insert(QStringLiteral("connected"), properties.value(QStringLiteral("Connected"), false));
        device.insert(QStringLiteral("rssi"), properties.value(QStringLiteral("RSSI"), -100));
        device.insert(QStringLiteral("wearable"),
                      isWearableDevice(properties, name));
        found.append(device);
    }

    std::sort(found.begin(), found.end(), [](const QVariant &left, const QVariant &right) {
        const QVariantMap a = left.toMap();
        const QVariantMap b = right.toMap();
        if (a.value(QStringLiteral("wearable")).toBool()
                != b.value(QStringLiteral("wearable")).toBool())
            return a.value(QStringLiteral("wearable")).toBool();
        return a.value(QStringLiteral("rssi")).toInt()
                > b.value(QStringLiteral("rssi")).toInt();
    });

    const bool availabilityChanged = m_bluetoothAvailable != !adapterPath.isEmpty();
    m_bluetoothAvailable = !adapterPath.isEmpty();
    m_adapterPath = adapterPath;
    if (availabilityChanged)
        emit bluetoothAvailableChanged();
    setDiscovering(adapterDiscovering);

    QString connectedPath;
    foreach (const QVariant &entry, found) {
        const QVariantMap device = entry.toMap();
        if (device.value(QStringLiteral("connected")).toBool()
                && device.value(QStringLiteral("paired")).toBool()) {
            if (device.value(QStringLiteral("wearable")).toBool() || connectedPath.isEmpty())
                connectedPath = device.value(QStringLiteral("path")).toString();
            if (device.value(QStringLiteral("wearable")).toBool())
                break;
        }
    }
    const QString previousConnected = m_connectedWatchPath;
    m_connectedWatchPath = connectedPath;
    m_devices = found;
    emit devicesChanged();
    if (connectedPath != previousConnected) {
        if (connectedPath.isEmpty())
            emit deviceDisconnected();
        else
            emit deviceConnected(connectedPath);
    }
}

void BluezManager::startDiscovery()
{
    if (m_adapterPath.isEmpty()) {
        setStatusMessage(QStringLiteral("Turn Bluetooth on, then try again"));
        return;
    }

    QDBusInterface adapter(QStringLiteral("org.bluez"), m_adapterPath,
                           QStringLiteral("org.bluez.Adapter1"),
                           QDBusConnection::systemBus());
    QDBusMessage reply = adapter.call(QStringLiteral("StartDiscovery"));
    if (reply.type() == QDBusMessage::ErrorMessage) {
        setStatusMessage(QStringLiteral("%1 (%2)").arg(reply.errorMessage(),
                                                       reply.errorName()));
        return;
    }
    setDiscovering(true);
    setStatusMessage(QStringLiteral("Looking for nearby watches…"));
    m_refreshTimer->start(1500);
}

void BluezManager::stopDiscovery()
{
    if (!m_adapterPath.isEmpty()) {
        QDBusInterface adapter(QStringLiteral("org.bluez"), m_adapterPath,
                               QStringLiteral("org.bluez.Adapter1"),
                               QDBusConnection::systemBus());
        adapter.call(QStringLiteral("StopDiscovery"));
    }
    m_refreshTimer->start(15000);
    setDiscovering(false);
    setStatusMessage(QString());
    refresh();
}

void BluezManager::pairDevice(const QString &path)
{
    callDeviceMethod(path, QStringLiteral("Pair"),
                     QStringLiteral("Pairing with %1…").arg(deviceNameForPath(path)),
                     QStringLiteral("Watch paired"));
}

void BluezManager::setPreferredBearer(const QString &path, const QString &bearer)
{
    if (path.isEmpty() || bearer.isEmpty())
        return;
    QDBusInterface props(QStringLiteral("org.bluez"), path,
                         QStringLiteral("org.freedesktop.DBus.Properties"),
                         QDBusConnection::systemBus());
    // PreferredBearer is optional (BlueZ ≥ 5.80). Ignore UnknownProperty.
    props.call(QStringLiteral("Set"),
               QStringLiteral("org.bluez.Device1"),
               QStringLiteral("PreferredBearer"),
               QVariant::fromValue(QDBusVariant(bearer)));
}

void BluezManager::startLeDiscovery()
{
    if (m_adapterPath.isEmpty())
        return;
    QDBusInterface adapter(QStringLiteral("org.bluez"), m_adapterPath,
                           QStringLiteral("org.bluez.Adapter1"),
                           QDBusConnection::systemBus());
    QVariantMap filter;
    filter.insert(QStringLiteral("Transport"), QStringLiteral("le"));
    filter.insert(QStringLiteral("UUIDs"),
                  QStringList() << QLatin1String(kWristLinkServiceUuid));
    adapter.call(QStringLiteral("SetDiscoveryFilter"), filter);
    const QDBusMessage reply = adapter.call(QStringLiteral("StartDiscovery"));
    if (reply.type() != QDBusMessage::ErrorMessage)
        setDiscovering(true);
}

void BluezManager::clearDiscoveryFilter()
{
    if (m_adapterPath.isEmpty())
        return;
    QDBusInterface adapter(QStringLiteral("org.bluez"), m_adapterPath,
                           QStringLiteral("org.bluez.Adapter1"),
                           QDBusConnection::systemBus());
    adapter.call(QStringLiteral("SetDiscoveryFilter"), QVariantMap());
}

void BluezManager::connectDevice(const QString &path)
{
    // WristLink GATT lives on LE. Dual-mode watches (Pixel, Galaxy, …) often
    // stay on classic ACL; prefer LE so Connect() brings up ATT.
    setPreferredBearer(path, QStringLiteral("le"));
    callDeviceMethod(path, QStringLiteral("Connect"),
                     QStringLiteral("Connecting…"), QStringLiteral("Watch connected"));
}

void BluezManager::ensureConnected(const QString &path)
{
    if (path.isEmpty() || m_busy || m_discovering)
        return;
    if (path == pairedWatchPath() && watchIsConnected())
        return;
    setPreferredBearer(path, QStringLiteral("le"));
    callDeviceMethod(path, QStringLiteral("Connect"),
                     QStringLiteral("Connecting to the watch…"),
                     QStringLiteral("Watch connected"));
}

void BluezManager::reconnectDevice(const QString &path)
{
    reconnectForGatt(path);
}

void BluezManager::reconnectForGatt(const QString &path)
{
    if (path.isEmpty() || m_busy)
        return;
    if (!pairedWatchPath().isEmpty() && path != pairedWatchPath())
        return;
    if (m_discovering)
        stopDiscovery();
    setDeviceTrusted(path);
    setBusy(true);
    setStatusMessage(QStringLiteral("Switching to Bluetooth LE…"));
    QDBusInterface device(QStringLiteral("org.bluez"), path,
                          QStringLiteral("org.bluez.Device1"),
                          QDBusConnection::systemBus());
    QDBusPendingCall call = device.asyncCall(QStringLiteral("Disconnect"));
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(call, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, watcher, path](QDBusPendingCallWatcher *) {
        watcher->deleteLater();
        // PreferredBearer only applies while disconnected.
        setPreferredBearer(path, QStringLiteral("le"));
        // Brief LE scan so BlueZ has a fresh advertisement before Connect.
        startLeDiscovery();
        QTimer::singleShot(2500, this, [this, path]() {
            if (m_discovering) {
                QDBusInterface adapter(QStringLiteral("org.bluez"), m_adapterPath,
                                       QStringLiteral("org.bluez.Adapter1"),
                                       QDBusConnection::systemBus());
                adapter.call(QStringLiteral("StopDiscovery"));
                setDiscovering(false);
            }
            clearDiscoveryFilter();
            m_refreshTimer->start(15000);
            setBusy(false);
            connectDevice(path);
        });
    });
}

void BluezManager::disconnectDevice(const QString &path)
{
    callDeviceMethod(path, QStringLiteral("Disconnect"),
                     QStringLiteral("Disconnecting…"), QStringLiteral("Watch disconnected"));
}

void BluezManager::removeDevice(const QString &path)
{
    if (m_adapterPath.isEmpty())
        return;
    setBusy(true);
    QDBusInterface adapter(QStringLiteral("org.bluez"), m_adapterPath,
                           QStringLiteral("org.bluez.Adapter1"),
                           QDBusConnection::systemBus());
    QDBusPendingCall call = adapter.asyncCall(QStringLiteral("RemoveDevice"),
                                              QVariant::fromValue(QDBusObjectPath(path)));
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(call, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, watcher](QDBusPendingCallWatcher *) {
        QDBusPendingReply<> reply = *watcher;
        setStatusMessage(reply.isError()
                         ? QStringLiteral("%1 (%2)").arg(reply.error().message(),
                                                         reply.error().name())
                         : QStringLiteral("Watch removed"));
        setBusy(false);
        watcher->deleteLater();
        refresh();
    });
}

void BluezManager::setDeviceTrusted(const QString &path)
{
    QDBusInterface props(QStringLiteral("org.bluez"), path,
                         QStringLiteral("org.freedesktop.DBus.Properties"),
                         QDBusConnection::systemBus());
    props.asyncCall(QStringLiteral("Set"),
                    QStringLiteral("org.bluez.Device1"),
                    QStringLiteral("Trusted"),
                    QVariant::fromValue(QDBusVariant(true)));
}

void BluezManager::callDeviceMethod(const QString &path, const QString &method,
                                    const QString &workingMessage,
                                    const QString &successMessage)
{
    if (path.isEmpty())
        return;
    if (m_busy && m_methodRetries == 0)
        return;
    if (m_discovering)
        stopDiscovery();
    setBusy(true);
    setStatusMessage(workingMessage);
    QDBusInterface device(QStringLiteral("org.bluez"), path,
                          QStringLiteral("org.bluez.Device1"),
                          QDBusConnection::systemBus());
    QDBusPendingCall call = device.asyncCall(method);
    QDBusPendingCallWatcher *watcher = new QDBusPendingCallWatcher(call, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, watcher, path, method, workingMessage, successMessage](QDBusPendingCallWatcher *) {
        QDBusPendingReply<> reply = *watcher;
        watcher->deleteLater();
        if (reply.isError() && isInProgressError(reply.error()) && m_methodRetries < 2) {
            ++m_methodRetries;
            QTimer::singleShot(1200, this, [this, path, method, workingMessage, successMessage]() {
                callDeviceMethod(path, method, workingMessage, successMessage);
            });
            return;
        }
        m_methodRetries = 0;
        if (reply.isError()) {
            if (reply.error().name() == QLatin1String("org.bluez.Error.AlreadyConnected"))
                setStatusMessage(successMessage);
            else
                setStatusMessage(QStringLiteral("%1 (%2)").arg(reply.error().message(),
                                                               reply.error().name()));
        } else {
            if (method == QLatin1String("Pair"))
                setDeviceTrusted(path);
            setStatusMessage(successMessage);
        }
        setBusy(false);
        refresh();
    });
}

void BluezManager::acceptPairing()
{
    m_pairingAgent->accept();
}

void BluezManager::rejectPairing()
{
    m_pairingAgent->reject();
}

void BluezManager::onConfirmationRequested(const QString &devicePath,
                                           const QString &passkey)
{
    m_confirmationDevice = deviceNameForPath(devicePath);
    m_confirmationCode = passkey;
    emit confirmationRequested();
}

QString BluezManager::deviceNameForPath(const QString &path) const
{
    foreach (const QVariant &entry, m_devices) {
        const QVariantMap device = entry.toMap();
        if (device.value(QStringLiteral("path")).toString() == path)
            return device.value(QStringLiteral("name")).toString();
    }
    return QStringLiteral("watch");
}

void BluezManager::setDiscovering(bool value)
{
    if (m_discovering == value)
        return;
    m_discovering = value;
    emit discoveringChanged();
}

void BluezManager::setBusy(bool value)
{
    if (m_busy == value)
        return;
    m_busy = value;
    emit busyChanged();
}

void BluezManager::setStatusMessage(const QString &message)
{
    if (m_statusMessage == message)
        return;
    m_statusMessage = message;
    emit statusMessageChanged();
}
