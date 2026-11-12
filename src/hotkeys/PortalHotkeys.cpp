#include "hotkeys/PortalHotkeys.h"

#include <QCoreApplication>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QKeySequence>

namespace {
const QString kService = "org.freedesktop.portal.Desktop";
const QString kPath = "/org/freedesktop/portal/desktop";
const QString kIface = "org.freedesktop.portal.GlobalShortcuts";
const QString kRequest = "org.freedesktop.portal.Request";

struct PortalShortcut {
    QString id;
    QVariantMap props;
};
QDBusArgument &operator<<(QDBusArgument &arg, const PortalShortcut &s)
{
    arg.beginStructure();
    arg << s.id << s.props;
    arg.endStructure();
    return arg;
}
const QDBusArgument &operator>>(const QDBusArgument &arg, PortalShortcut &s)
{
    arg.beginStructure();
    arg >> s.id >> s.props;
    arg.endStructure();
    return arg;
}
}
Q_DECLARE_METATYPE(PortalShortcut)

PortalHotkeys::PortalHotkeys(QObject *parent) : QObject(parent)
{
    qDBusRegisterMetaType<PortalShortcut>();
    qDBusRegisterMetaType<QList<PortalShortcut>>();
}

PortalHotkeys::~PortalHotkeys() { stop(); }

void PortalHotkeys::registerApp(const QString &appId)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) return;
    QDBusMessage msg = QDBusMessage::createMethodCall(kService, kPath,
                                                      "org.freedesktop.host.portal.Registry", "Register");
    msg << appId << QVariantMap{};
    bus.call(msg, QDBus::Block, 1500);  // errors (old portal, already registered) are fine
}

QString PortalHotkeys::portalTrigger(const HotkeyBinding &b)
{
    if (b.text.isEmpty()) return {};
    // XDG shortcut syntax: MODIFIER+MODIFIER+keysym, e.g. "ALT+F10".
    QStringList parts = b.text.split('+', Qt::SkipEmptyParts);
    if (parts.isEmpty()) return {};
    const QString key = parts.takeLast();
    QStringList out;
    for (const QString &p : std::as_const(parts)) {
        if (p == "Ctrl") out << "CTRL";
        else if (p == "Shift") out << "SHIFT";
        else if (p == "Alt") out << "ALT";
        else if (p == "Meta") out << "LOGO";
    }
    out << (key.size() == 1 ? key.toLower() : key);
    return out.join('+');
}

QString PortalHotkeys::requestPath(const QString &token) const
{
    QString sender = QDBusConnection::sessionBus().baseService().mid(1);
    sender.replace('.', '_');
    return QString("/org/freedesktop/portal/desktop/request/%1/%2").arg(sender, token);
}

void PortalHotkeys::start(const QMap<QString, HotkeyBinding> &bindings)
{
    stop();
    m_bindings = bindings;
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        emit unavailable("No D-Bus session bus");
        return;
    }

    const QString token = QString("clipstash_%1_%2").arg(QCoreApplication::applicationPid()).arg(++m_tokenCounter);
    m_createPath = requestPath(token);
    bus.connect(kService, m_createPath, kRequest, "Response", this,
                SLOT(onCreateResponse(uint, QVariantMap)));

    QDBusMessage msg = QDBusMessage::createMethodCall(kService, kPath, kIface, "CreateSession");
    msg << QVariantMap{{"handle_token", token}, {"session_handle_token", token + "_s"}};
    auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(msg, 5000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        QDBusPendingReply<QDBusObjectPath> reply = *w;
        w->deleteLater();
        if (reply.isError())
            emit unavailable("Desktop portal has no global shortcuts support");
    });
}

void PortalHotkeys::onCreateResponse(uint response, const QVariantMap &results)
{
    QDBusConnection::sessionBus().disconnect(kService, m_createPath, kRequest, "Response", this,
                                             SLOT(onCreateResponse(uint, QVariantMap)));
    if (response != 0) {
        emit unavailable("Desktop portal refused the shortcut session");
        return;
    }
    m_session = results.value("session_handle").toString();
    if (m_session.isEmpty()) {
        emit unavailable("Desktop portal returned no session");
        return;
    }
    if (!m_signalConnected) {
        m_signalConnected = QDBusConnection::sessionBus().connect(
            kService, kPath, kIface, "Activated", this,
            SLOT(onActivated(QDBusObjectPath, QString, qulonglong, QVariantMap)));
    }
    bind();
}

void PortalHotkeys::bind()
{
    QList<PortalShortcut> shortcuts;
    for (const QString &id : HotkeyIds::all()) {
        const HotkeyBinding b = m_bindings.value(id);
        if (!b.isSet()) continue;
        QVariantMap props{{"description", HotkeyIds::label(id)}};
        const QString trigger = portalTrigger(b);
        if (!trigger.isEmpty()) props["preferred_trigger"] = trigger;
        shortcuts.append({id, props});
    }
    if (shortcuts.isEmpty()) {
        m_bound = true;
        emit ready();
        return;
    }

    QDBusConnection bus = QDBusConnection::sessionBus();
    const QString token = QString("clipstash_%1_%2").arg(QCoreApplication::applicationPid()).arg(++m_tokenCounter);
    m_bindPath = requestPath(token);
    bus.connect(kService, m_bindPath, kRequest, "Response", this,
                SLOT(onBindResponse(uint, QVariantMap)));

    QDBusMessage msg = QDBusMessage::createMethodCall(kService, kPath, kIface, "BindShortcuts");
    msg << QVariant::fromValue(QDBusObjectPath(m_session)) << QVariant::fromValue(shortcuts)
        << QString() << QVariantMap{{"handle_token", token}};
    auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(msg, -1), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *w) {
        const bool error = w->isError();
        w->deleteLater();
        if (error) emit unavailable("Desktop portal could not bind shortcuts");
    });
}

void PortalHotkeys::onBindResponse(uint response, const QVariantMap &)
{
    QDBusConnection::sessionBus().disconnect(kService, m_bindPath, kRequest, "Response", this,
                                             SLOT(onBindResponse(uint, QVariantMap)));
    if (response != 0) {
        emit unavailable("Shortcut binding was cancelled");
        return;
    }
    m_bound = true;
    emit ready();
}

void PortalHotkeys::onActivated(const QDBusObjectPath &session, const QString &id, qulonglong,
                                const QVariantMap &)
{
    if (session.path() == m_session) emit activated(id);
}

void PortalHotkeys::stop()
{
    m_bound = false;
    if (m_session.isEmpty()) return;
    QDBusMessage msg = QDBusMessage::createMethodCall(kService, m_session,
                                                      "org.freedesktop.portal.Session", "Close");
    QDBusConnection::sessionBus().asyncCall(msg);
    m_session.clear();
}
