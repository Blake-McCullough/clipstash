#include "ui/Theme.h"

#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

namespace Theme {

QString styleSheet()
{
    return QStringLiteral(R"(
* { font-size: 13px; }
QWidget { background: #0f1115; color: #e6e8ee; }
QWidget#sidebar { background: #161920; border-right: 1px solid #2a2f3a; }
QWidget#panel, QFrame#panel { background: #161920; border: 1px solid #2a2f3a; border-radius: 10px; }
QWidget#panel QLabel, QWidget#panel QCheckBox, QFrame#panel QLabel, QFrame#panel QCheckBox { background: transparent; }
QLabel { background: transparent; }
QLabel#h1 { font-size: 20px; font-weight: 600; }
QLabel#h2 { font-size: 14px; font-weight: 600; }
QLabel#muted { color: #8b93a3; }
QLabel#brand { font-size: 17px; font-weight: 700; }

QPushButton {
    background: #1c2029; border: 1px solid #2a2f3a; border-radius: 7px; padding: 7px 14px;
}
QPushButton:hover { background: #242936; border-color: #3a4150; }
QPushButton:pressed { background: #1a1e27; }
QPushButton:disabled { color: #5b6270; background: #171a21; }
QPushButton#primary { background: #7c5cff; border-color: #7c5cff; color: white; font-weight: 600; }
QPushButton#primary:hover { background: #8d71ff; }
QPushButton#primary:disabled { background: #3a3266; border-color: #3a3266; color: #9a93c2; }
QPushButton#danger { color: #ff5c6c; }
QPushButton#danger:hover { background: #3a1f26; border-color: #ff5c6c; }
QPushButton#nav {
    text-align: left; border: none; background: transparent; padding: 9px 14px; border-radius: 7px;
    color: #8b93a3; font-weight: 600;
}
QPushButton#nav:hover { background: #1c2029; color: #e6e8ee; }
QPushButton#nav:checked { background: #242936; color: #e6e8ee; }
QPushButton#flat { border: none; background: transparent; padding: 6px 10px; }
QPushButton#flat:hover { background: #1c2029; }
QPushButton#chip { border-radius: 13px; padding: 4px 12px; }
QPushButton#chip:checked { background: #7c5cff; border-color: #7c5cff; color: white; }

QLineEdit, QSpinBox, QComboBox, QPlainTextEdit {
    background: #12141a; border: 1px solid #2a2f3a; border-radius: 7px; padding: 6px 9px;
    selection-background-color: #7c5cff;
}
QLineEdit:focus, QSpinBox:focus, QComboBox:focus { border-color: #7c5cff; }
QComboBox::drop-down { border: none; width: 22px; }
QComboBox QAbstractItemView {
    background: #1c2029; border: 1px solid #2a2f3a; selection-background-color: #7c5cff; outline: none;
}
QSpinBox::up-button, QSpinBox::down-button { width: 0; height: 0; border: none; }

QCheckBox { spacing: 8px; }
QCheckBox::indicator {
    width: 16px; height: 16px; border-radius: 4px; border: 1px solid #3a4150; background: #12141a;
}
QCheckBox::indicator:checked { background: #7c5cff; border-color: #7c5cff; }

QSlider::groove:horizontal { height: 4px; background: #2a2f3a; border-radius: 2px; }
QSlider::sub-page:horizontal { background: #7c5cff; border-radius: 2px; }
QSlider::handle:horizontal {
    width: 14px; height: 14px; margin: -5px 0; border-radius: 7px; background: #e6e8ee;
}
QSlider::groove:horizontal:disabled, QSlider::sub-page:horizontal:disabled { background: #22262f; }
QSlider::handle:horizontal:disabled { background: #5b6270; }

QProgressBar { background: #22262f; border: none; border-radius: 3px; max-height: 6px; text-align: center; }
QProgressBar::chunk { background: #7c5cff; border-radius: 3px; }

QScrollArea { border: none; }
QScrollBar:vertical { background: transparent; width: 10px; margin: 0; }
QScrollBar::handle:vertical { background: #2a2f3a; border-radius: 5px; min-height: 30px; }
QScrollBar::handle:vertical:hover { background: #3a4150; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
QScrollBar:horizontal { height: 0; }

QListView { border: none; outline: none; }
QMenu { background: #1c2029; border: 1px solid #2a2f3a; border-radius: 8px; padding: 5px; }
QMenu::item { padding: 6px 22px 6px 12px; border-radius: 5px; }
QMenu::item:selected { background: #7c5cff; color: white; }
QMenu::item:disabled { color: #5b6270; }
QMenu::separator { height: 1px; background: #2a2f3a; margin: 4px 6px; }
QToolTip { background: #1c2029; color: #e6e8ee; border: 1px solid #2a2f3a; padding: 4px 6px; }
)");
}

QIcon appIcon(bool recording)
{
    QIcon icon;
    for (int size : {22, 32, 48, 64, 128}) {
        QPixmap pm(size, size);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        const qreal s = size;
        p.setPen(Qt::NoPen);
        p.setBrush(Accent);
        p.drawRoundedRect(QRectF(0, 0, s, s), s * 0.24, s * 0.24);
        // play triangle
        QPainterPath tri;
        tri.moveTo(s * 0.38, s * 0.27);
        tri.lineTo(s * 0.74, s * 0.5);
        tri.lineTo(s * 0.38, s * 0.73);
        tri.closeSubpath();
        p.setBrush(Qt::white);
        p.drawPath(tri);
        if (recording) {
            p.setBrush(Bad);
            p.setPen(QPen(Qt::white, qMax(1.0, s * 0.05)));
            p.drawEllipse(QPointF(s * 0.79, s * 0.21), s * 0.16, s * 0.16);
        }
        icon.addPixmap(pm);
    }
    return icon;
}

}
