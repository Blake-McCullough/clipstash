#pragma once
#include "core/Settings.h"

#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>
#include <QSet>

class QSocketNotifier;

// Global hotkeys by listening to keyboards directly through evdev. Works on
// every compositor, but the user must be able to read /dev/input/event*
// (member of the "input" group). Keys are only observed, never grabbed.
class EvdevHotkeys : public QObject {
    Q_OBJECT
public:
    explicit EvdevHotkeys(QObject *parent = nullptr);
    ~EvdevHotkeys() override;

    bool start();
    void stop();
    bool isActive() const { return !m_devices.isEmpty(); }
    void setBindings(const QMap<QString, HotkeyBinding> &b) { m_bindings = b; }

signals:
    void activated(const QString &id);

private:
    void scan();
    void openDevice(const QString &path);
    void closeDevice(int fd);
    void readDevice(int fd);
    quint32 currentMods() const;

    struct Device {
        QString path;
        QSocketNotifier *notifier = nullptr;
    };
    QHash<int, Device> m_devices;
    QSet<int> m_pressed;
    QMap<QString, HotkeyBinding> m_bindings;
    QFileSystemWatcher m_watcher;
    bool m_started = false;
};
