#include "ui/HotkeyEdit.h"

#include <QKeyEvent>
#include <QKeySequence>

HotkeyEdit::HotkeyEdit(QWidget *parent) : QPushButton(parent)
{
    setMinimumWidth(170);
    setFocusPolicy(Qt::StrongFocus);
    connect(this, &QPushButton::clicked, this, [this] {
        m_capturing = true;
        refresh();
    });
    refresh();
}

void HotkeyEdit::setBinding(const HotkeyBinding &b)
{
    m_binding = b;
    m_capturing = false;
    refresh();
}

void HotkeyEdit::refresh()
{
    if (m_capturing) setText("Press keys…");
    else setText(m_binding.text.isEmpty() ? "Not set" : m_binding.text);
    setStyleSheet(m_capturing ? "border-color: #7c5cff; color: #b9a8ff;" : "");
}

bool HotkeyEdit::event(QEvent *e)
{
    // Keep Tab/Space/Enter etc. from being eaten as navigation while capturing.
    if (m_capturing && e->type() == QEvent::ShortcutOverride) {
        e->accept();
        return true;
    }
    if (m_capturing && e->type() == QEvent::KeyPress) {
        keyPressEvent(static_cast<QKeyEvent *>(e));
        return true;
    }
    if (m_capturing && e->type() == QEvent::KeyRelease) return true;
    return QPushButton::event(e);
}

void HotkeyEdit::keyPressEvent(QKeyEvent *e)
{
    if (!m_capturing) {
        QPushButton::keyPressEvent(e);
        return;
    }
    const int key = e->key();
    switch (key) {
    case Qt::Key_Control: case Qt::Key_Shift: case Qt::Key_Alt: case Qt::Key_Meta:
    case Qt::Key_AltGr: case Qt::Key_Super_L: case Qt::Key_Super_R: case Qt::Key_unknown:
        return;  // wait for a non-modifier key
    }
    const Qt::KeyboardModifiers mods = e->modifiers()
        & (Qt::ControlModifier | Qt::ShiftModifier | Qt::AltModifier | Qt::MetaModifier);
    if (key == Qt::Key_Escape && !mods) {
        m_capturing = false;
        refresh();
        return;
    }
    if (key == Qt::Key_Backspace && !mods) {
        m_binding = {};
        m_capturing = false;
        refresh();
        emit bindingChanged();
        return;
    }

    HotkeyBinding b;
    if (mods & Qt::ControlModifier) b.mods |= ModCtrl;
    if (mods & Qt::ShiftModifier) b.mods |= ModShift;
    if (mods & Qt::AltModifier) b.mods |= ModAlt;
    if (mods & Qt::MetaModifier) b.mods |= ModMeta;
    // X11 and Wayland both report xkb keycodes, which are evdev codes + 8.
    const quint32 scan = e->nativeScanCode();
    b.code = scan > 8 ? int(scan - 8) : 0;
    b.text = QKeySequence(QKeyCombination(mods, Qt::Key(key))).toString(QKeySequence::PortableText);
    m_binding = b;
    m_capturing = false;
    refresh();
    emit bindingChanged();
}

void HotkeyEdit::focusOutEvent(QFocusEvent *e)
{
    if (m_capturing) {
        m_capturing = false;
        refresh();
    }
    QPushButton::focusOutEvent(e);
}
