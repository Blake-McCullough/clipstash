#include "hotkeys/HotkeyManager.h"

HotkeyManager::HotkeyManager(QObject *parent) : QObject(parent)
{
#ifdef Q_OS_WIN
    connect(&m_win, &WinHotkeys::activated, this, &HotkeyManager::fire);
#else

    connect(&m_portal, &PortalHotkeys::activated, this, &HotkeyManager::fire);
    connect(&m_evdev, &EvdevHotkeys::activated, this, &HotkeyManager::fire);
    connect(&m_portal, &PortalHotkeys::ready, this, [this] {
        setStatus("Active via desktop portal. Your desktop's shortcut settings have the final say on the keys.");
    });
    connect(&m_portal, &PortalHotkeys::unavailable, this, [this](const QString &why) {
        if (m_settings.hotkeyBackend == "auto") startEvdev(true, why);
        else setStatus(why + ".");
    });
#endif
}

void HotkeyManager::setStatus(const QString &s)
{
    m_status = s;
    emit statusChanged(s);
}

void HotkeyManager::fire(const QString &id)
{
    // Debounce key repeat and double delivery.
    if (m_lastId == id && m_lastFire.isValid() && m_lastFire.elapsed() < 400) return;
    m_lastId = id;
    m_lastFire.restart();
    emit activated(id);
}

#ifndef Q_OS_WIN
void HotkeyManager::startEvdev(bool isFallback, const QString &why)
{
    m_evdev.setBindings(m_settings.hotkeys);
    if (m_evdev.start()) {
        setStatus("Active via direct keyboard access.");
        return;
    }
    QString msg = "Direct keyboard access needs permission: run "
                  "\"sudo usermod -aG input $USER\" and log out and back in.";
    if (isFallback) msg = why + ". " + msg + " Or bind \"clipstash --save-clip\" in your compositor.";
    setStatus(msg);
}

#endif

void HotkeyManager::apply(const Settings &s)
{
#ifdef Q_OS_WIN
    m_settings = s;
    if (s.hotkeyBackend == "none") {
        m_win.clear();
        setStatus("Disabled. You can still run \"clipstash --save-clip\".");
        return;
    }
    const QStringList refused = m_win.apply(s.hotkeys);
    setStatus(refused.isEmpty() ? QString("Active.")
                                : "Active, but Windows refused " + refused.join(", ")
                                      + " (another program already uses it). Pick a different key.");
    return;
#endif
    applyLinux(s);
}

void HotkeyManager::applyLinux([[maybe_unused]] const Settings &s)
{
#ifndef Q_OS_WIN
    m_settings = s;
    m_portal.stop();
    m_evdev.stop();
    const QString backend = s.hotkeyBackend;
    if (backend == "none") {
        setStatus("Disabled. You can still bind \"clipstash --save-clip\" in your compositor.");
    } else if (backend == "evdev") {
        startEvdev(false);
    } else {
        setStatus("Asking the desktop portal…");
        m_portal.start(s.hotkeys);
    }
#endif
}
