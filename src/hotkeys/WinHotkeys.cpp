#include "hotkeys/WinHotkeys.h"

#include <QCoreApplication>
#include <QKeySequence>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

WinHotkeys::WinHotkeys(QObject *parent) : QObject(parent)
{
    QCoreApplication::instance()->installNativeEventFilter(this);
}

WinHotkeys::~WinHotkeys()
{
    clear();
    if (QCoreApplication::instance()) QCoreApplication::instance()->removeNativeEventFilter(this);
}

bool WinHotkeys::toNative(const QString &text, quint32 *mods, quint32 *vk)
{
    const QKeySequence seq = QKeySequence::fromString(text, QKeySequence::PortableText);
    if (seq.isEmpty()) return false;
    const QKeyCombination combo = seq[0];
    const Qt::KeyboardModifiers m = combo.keyboardModifiers();
    *mods = MOD_NOREPEAT;
    if (m & Qt::ControlModifier) *mods |= MOD_CONTROL;
    if (m & Qt::ShiftModifier) *mods |= MOD_SHIFT;
    if (m & Qt::AltModifier) *mods |= MOD_ALT;
    if (m & Qt::MetaModifier) *mods |= MOD_WIN;

    const int k = combo.key();
    if (k >= Qt::Key_F1 && k <= Qt::Key_F24) *vk = VK_F1 + (k - Qt::Key_F1);
    else if (k >= Qt::Key_A && k <= Qt::Key_Z) *vk = 'A' + (k - Qt::Key_A);
    else if (k >= Qt::Key_0 && k <= Qt::Key_9) *vk = '0' + (k - Qt::Key_0);
    else {
        switch (k) {
        case Qt::Key_Space: *vk = VK_SPACE; break;
        case Qt::Key_Return: case Qt::Key_Enter: *vk = VK_RETURN; break;
        case Qt::Key_Escape: *vk = VK_ESCAPE; break;
        case Qt::Key_Tab: *vk = VK_TAB; break;
        case Qt::Key_Backspace: *vk = VK_BACK; break;
        case Qt::Key_Insert: *vk = VK_INSERT; break;
        case Qt::Key_Delete: *vk = VK_DELETE; break;
        case Qt::Key_Home: *vk = VK_HOME; break;
        case Qt::Key_End: *vk = VK_END; break;
        case Qt::Key_PageUp: *vk = VK_PRIOR; break;
        case Qt::Key_PageDown: *vk = VK_NEXT; break;
        case Qt::Key_Left: *vk = VK_LEFT; break;
        case Qt::Key_Right: *vk = VK_RIGHT; break;
        case Qt::Key_Up: *vk = VK_UP; break;
        case Qt::Key_Down: *vk = VK_DOWN; break;
        case Qt::Key_Print: *vk = VK_SNAPSHOT; break;
        case Qt::Key_Pause: *vk = VK_PAUSE; break;
        case Qt::Key_ScrollLock: *vk = VK_SCROLL; break;
        default: {
            // Punctuation: let Windows map the character to a key.
            const QString t = QKeySequence(k).toString(QKeySequence::PortableText);
            if (t.size() != 1) return false;
            const SHORT r = VkKeyScanW(t.at(0).unicode());
            if (r == -1) return false;
            *vk = r & 0xff;
        }
        }
    }
    return true;
}

QStringList WinHotkeys::apply(const QMap<QString, HotkeyBinding> &bindings)
{
    clear();
    QStringList refused;
    for (auto it = bindings.cbegin(); it != bindings.cend(); ++it) {
        if (it->text.isEmpty()) continue;
        quint32 mods = 0, vk = 0;
        if (!toNative(it->text, &mods, &vk)) {
            refused << it->text;
            continue;
        }
        const int id = m_next++;
        if (RegisterHotKey(nullptr, id, mods, vk)) m_ids.insert(id, it.key());
        else refused << it->text;
    }
    return refused;
}

void WinHotkeys::clear()
{
    for (auto it = m_ids.cbegin(); it != m_ids.cend(); ++it) UnregisterHotKey(nullptr, it.key());
    m_ids.clear();
}

bool WinHotkeys::nativeEventFilter(const QByteArray &type, void *message, qintptr *)
{
    if (type != "windows_generic_MSG") return false;
    const MSG *msg = static_cast<const MSG *>(message);
    if (msg->message != WM_HOTKEY) return false;
    const auto it = m_ids.constFind(int(msg->wParam));
    if (it == m_ids.cend()) return false;
    emit activated(*it);
    return true;
}
