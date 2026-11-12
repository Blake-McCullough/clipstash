#pragma once
#include "core/Settings.h"
#include "hotkeys/EvdevHotkeys.h"
#include "hotkeys/PortalHotkeys.h"

#include <QElapsedTimer>
#include <QObject>

// Picks a global-hotkey backend and exposes one activated() signal.
class HotkeyManager : public QObject {
    Q_OBJECT
public:
    explicit HotkeyManager(QObject *parent = nullptr);

    void apply(const Settings &s);
    QString status() const { return m_status; }

signals:
    void activated(const QString &id);
    void statusChanged(const QString &status);

private:
    void setStatus(const QString &s);
    void startEvdev(bool isFallback, const QString &why = {});
    void fire(const QString &id);

    PortalHotkeys m_portal;
    EvdevHotkeys m_evdev;
    Settings m_settings;
    QString m_status;
    QElapsedTimer m_lastFire;
    QString m_lastId;
};
