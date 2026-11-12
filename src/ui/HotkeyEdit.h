#pragma once
#include "core/Settings.h"

#include <QPushButton>

// Click, then press a key combination. Backspace clears, Esc cancels.
class HotkeyEdit : public QPushButton {
    Q_OBJECT
public:
    explicit HotkeyEdit(QWidget *parent = nullptr);

    void setBinding(const HotkeyBinding &b);
    HotkeyBinding binding() const { return m_binding; }

signals:
    void bindingChanged();

protected:
    void keyPressEvent(QKeyEvent *e) override;
    void focusOutEvent(QFocusEvent *e) override;
    bool event(QEvent *e) override;

private:
    void refresh();
    HotkeyBinding m_binding;
    bool m_capturing = false;
};
