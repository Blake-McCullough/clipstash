#pragma once
#include "core/Settings.h"

#include <QAbstractNativeEventFilter>
#include <QHash>
#include <QObject>

// Global hotkeys on Windows through RegisterHotKey. Works in every game that
// does not block it, needs no special permissions, and Windows only tells us
// about the exact combinations we registered.
class WinHotkeys : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
public:
    explicit WinHotkeys(QObject *parent = nullptr);
    ~WinHotkeys() override;

    // Registers the bindings; returns the ones Windows refused (already
    // taken by another program).
    QStringList apply(const QMap<QString, HotkeyBinding> &bindings);
    void clear();

    bool nativeEventFilter(const QByteArray &type, void *message, qintptr *result) override;

    // "Alt+F10" -> (modifier flags, virtual key). Returns false if unknown.
    static bool toNative(const QString &text, quint32 *mods, quint32 *vk);

signals:
    void activated(const QString &id);

private:
    QHash<int, QString> m_ids;
    int m_next = 1;
};
