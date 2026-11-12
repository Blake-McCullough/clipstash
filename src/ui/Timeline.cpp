#include "ui/Timeline.h"

#include "core/Clip.h"
#include "ui/Theme.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>

static const qreal kHandleW = 10;
static const double kMinLength = 0.5;

Timeline::Timeline(QWidget *parent) : QWidget(parent)
{
    setMinimumHeight(74);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMouseTracking(true);
}

void Timeline::setDuration(double s)
{
    m_duration = qMax(0.0, s);
    update();
}

void Timeline::setPosition(double s)
{
    if (m_drag == Drag::Playhead) return;
    m_position = s;
    update();
}

void Timeline::setTrim(double start, double end)
{
    m_start = start;
    m_end = end;
    update();
}

void Timeline::setFilmstrip(const QPixmap &strip)
{
    m_strip = strip;
    update();
}

QRectF Timeline::bar() const
{
    return QRectF(kHandleW, 16, width() - 2 * kHandleW, height() - 22);
}

double Timeline::xToTime(qreal x) const
{
    const QRectF b = bar();
    if (m_duration <= 0 || b.width() <= 0) return 0;
    return qBound(0.0, (x - b.left()) / b.width() * m_duration, m_duration);
}

qreal Timeline::timeToX(double t) const
{
    const QRectF b = bar();
    if (m_duration <= 0) return b.left();
    return b.left() + qBound(0.0, t / m_duration, 1.0) * b.width();
}

Timeline::Drag Timeline::hitTest(const QPointF &pos) const
{
    const qreal xs = timeToX(m_start), xe = timeToX(m_end);
    if (pos.x() >= xs - kHandleW - 2 && pos.x() <= xs + 3) return Drag::Start;
    if (pos.x() >= xe - 3 && pos.x() <= xe + kHandleW + 2) return Drag::End;
    return Drag::Playhead;
}

void Timeline::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF b = bar();

    QPainterPath clip;
    clip.addRoundedRect(b, 6, 6);
    p.save();
    p.setClipPath(clip);
    p.fillRect(b, Theme::Card);
    if (!m_strip.isNull()) {
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.drawPixmap(b, m_strip, QRectF(m_strip.rect()));
    }
    const qreal xs = timeToX(m_start), xe = timeToX(m_end);
    const QColor dim(8, 9, 12, 190);
    p.fillRect(QRectF(b.left(), b.top(), xs - b.left(), b.height()), dim);
    p.fillRect(QRectF(xe, b.top(), b.right() - xe, b.height()), dim);
    p.restore();

    // selected range frame + handles
    p.setPen(Qt::NoPen);
    p.setBrush(Theme::Accent);
    p.drawRoundedRect(QRectF(xs - kHandleW, b.top() - 2, kHandleW, b.height() + 4), 4, 4);
    p.drawRoundedRect(QRectF(xe, b.top() - 2, kHandleW, b.height() + 4), 4, 4);
    p.fillRect(QRectF(xs, b.top() - 2, xe - xs, 2), Theme::Accent);
    p.fillRect(QRectF(xs, b.bottom(), xe - xs, 2), Theme::Accent);
    p.setBrush(Qt::white);
    p.drawRoundedRect(QRectF(xs - kHandleW / 2 - 1, b.center().y() - 8, 2, 16), 1, 1);
    p.drawRoundedRect(QRectF(xe + kHandleW / 2 - 1, b.center().y() - 8, 2, 16), 1, 1);

    // playhead
    const qreal xp = timeToX(m_position);
    p.setBrush(Qt::white);
    p.drawRect(QRectF(xp - 1, b.top() - 6, 2, b.height() + 10));
    QPainterPath head;
    head.moveTo(xp - 6, b.top() - 12);
    head.lineTo(xp + 6, b.top() - 12);
    head.lineTo(xp, b.top() - 4);
    head.closeSubpath();
    p.drawPath(head);

    // time labels
    p.setPen(Theme::Muted);
    QFont f = font();
    f.setPixelSize(10);
    p.setFont(f);
    p.drawText(QRectF(b.left(), 0, 80, 12), Qt::AlignLeft | Qt::AlignVCenter, formatDuration(m_start));
    p.drawText(QRectF(b.right() - 80, 0, 80, 12), Qt::AlignRight | Qt::AlignVCenter, formatDuration(m_end));
}

void Timeline::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton || m_duration <= 0) return;
    m_drag = hitTest(e->position());
    mouseMoveEvent(e);
}

void Timeline::mouseMoveEvent(QMouseEvent *e)
{
    if (m_drag == Drag::None) {
        const Drag h = hitTest(e->position());
        setCursor(h == Drag::Playhead ? Qt::PointingHandCursor : Qt::SizeHorCursor);
        return;
    }
    const double t = xToTime(e->position().x() + (m_drag == Drag::Start ? kHandleW / 2 : m_drag == Drag::End ? -kHandleW / 2 : 0));
    if (m_drag == Drag::Start) {
        m_start = qBound(0.0, t, m_end - kMinLength);
        emit trimChanged(m_start, m_end, false);
    } else if (m_drag == Drag::End) {
        m_end = qBound(m_start + kMinLength, t, m_duration);
        emit trimChanged(m_start, m_end, false);
    } else {
        m_position = t;
        emit seekRequested(t, false);
    }
    update();
}

void Timeline::mouseReleaseEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton) return;
    const Drag d = m_drag;
    m_drag = Drag::None;
    if (d == Drag::Start || d == Drag::End) emit trimChanged(m_start, m_end, true);
    else if (d == Drag::Playhead) emit seekRequested(m_position, true);
}
