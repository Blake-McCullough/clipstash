#pragma once
#include "core/Settings.h"

#include <QDBusObjectPath>
#include <QObject>
#include <QVariantMap>

// Global hotkeys through the xdg-desktop-portal GlobalShortcuts interface
// (KDE Plasma, Hyprland, GNOME 48+). The desktop owns the actual key
// assignment; our bindings are passed as the preferred triggers.
class PortalHotkeys : public QObject {
    Q_OBJECT
public:
    explicit PortalHotkeys(QObject *parent = nullptr);
    ~PortalHotkeys() override;

    void start(const QMap<QString, HotkeyBinding> &bindings);
    void stop();
    bool isActive() const { return m_bound; }

    static QString portalTrigger(const HotkeyBinding &b);
    // Tells the portal which desktop file this (non-sandboxed) app belongs to.
    static void registerApp(const QString &appId);

signals:
    void activated(const QString &id);
    void ready();
    void unavailable(const QString &reason);

private slots:
    void onCreateResponse(uint response, const QVariantMap &results);
    void onBindResponse(uint response, const QVariantMap &results);
    void onActivated(const QDBusObjectPath &session, const QString &id, qulonglong timestamp,
                     const QVariantMap &options);

private:
    QString requestPath(const QString &token) const;
    void bind();

    QMap<QString, HotkeyBinding> m_bindings;
    QString m_session;
    QString m_createPath;
    QString m_bindPath;
    bool m_bound = false;
    bool m_signalConnected = false;
    int m_tokenCounter = 0;
};
