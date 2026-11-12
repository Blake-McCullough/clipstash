#include "ui/ClipDelegate.h"

#include "core/Clip.h"
#include "core/ClipLibrary.h"
#include "ui/Theme.h"

#include <QLocale>
#include <QPainter>
#include <QPainterPath>

QSize ClipDelegate::sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const
{
    return {CardW + 14, CardH + 14};
}

static void badge(QPainter *p, const QRectF &r, const QString &text, const QColor &bg, const QColor &fg = Qt::white)
{
    p->setPen(Qt::NoPen);
    p->setBrush(bg);
    p->drawRoundedRect(r, 4, 4);
    p->setPen(fg);
    p->drawText(r, Qt::AlignCenter, text);
}

void ClipDelegate::paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &index) const
{
    const Clip c = index.data(ClipLibrary::ClipRole).value<Clip>();
    const QPixmap thumb = index.data(ClipLibrary::ThumbRole).value<QPixmap>();
    const bool hover = opt.state & QStyle::State_MouseOver;
    const bool selected = opt.state & QStyle::State_Selected;

    p->save();
    p->setRenderHint(QPainter::Antialiasing);
    const QRectF card = QRectF(opt.rect).adjusted(7, 7, -7, -7);

    QPainterPath shape;
    shape.addRoundedRect(card, 9, 9);
    p->fillPath(shape, hover ? QColor(0x24, 0x29, 0x36) : Theme::Card);

    // thumbnail
    const QRectF tr(card.left(), card.top(), card.width(), ThumbH);
    p->save();
    QPainterPath thumbShape;
    thumbShape.addRoundedRect(card, 9, 9);
    p->setClipPath(thumbShape);
    p->setClipRect(tr, Qt::IntersectClip);
    p->fillRect(tr, QColor(0x0b, 0x0c, 0x10));
    if (!thumb.isNull()) {
        p->setRenderHint(QPainter::SmoothPixmapTransform);
        const QSizeF scaled = QSizeF(thumb.size()).scaled(tr.size(), Qt::KeepAspectRatioByExpanding);
        QRectF dst(QPointF(0, 0), scaled);
        dst.moveCenter(tr.center());
        p->drawPixmap(dst, thumb, QRectF(thumb.rect()));
    } else {
        p->setPen(Theme::Muted);
        p->drawText(tr, Qt::AlignCenter, c.probed ? "No preview" : "Processing…");
    }
    p->restore();

    QFont small = opt.font;
    small.setPixelSize(11);
    small.setBold(true);
    p->setFont(small);
    const QFontMetricsF fm(small);

    if (c.duration > 0) {
        const QString d = formatDuration(c.isTrimmed() ? c.editedDuration() : c.duration);
        const qreal w = fm.horizontalAdvance(d) + 12;
        badge(p, QRectF(tr.right() - w - 8, tr.bottom() - 26, w, 18), d, QColor(0, 0, 0, 185));
    }
    qreal bx = tr.left() + 8;
    if (!c.shareUrl.isEmpty()) {
        const qreal w = fm.horizontalAdvance("SHARED") + 12;
        badge(p, QRectF(bx, tr.top() + 8, w, 18), "SHARED", Theme::Accent);
        bx += w + 6;
    }
    if (c.favorite) badge(p, QRectF(bx, tr.top() + 8, 22, 18), QString::fromUtf8("★"), QColor(0, 0, 0, 185), Theme::Warn);

    if (busyPath == c.path) {
        p->fillRect(tr, QColor(0, 0, 0, 150));
        p->setPen(Qt::white);
        p->drawText(tr.adjusted(0, 0, 0, -14), Qt::AlignCenter,
                    QString("%1 %2%").arg(busyStage).arg(int(busyFraction * 100)));
        const QRectF track(tr.left() + 30, tr.center().y() + 10, tr.width() - 60, 4);
        p->setPen(Qt::NoPen);
        p->setBrush(QColor(255, 255, 255, 60));
        p->drawRoundedRect(track, 2, 2);
        p->setBrush(Theme::Accent);
        p->drawRoundedRect(QRectF(track.left(), track.top(), track.width() * busyFraction, 4), 2, 2);
    }

    // text
    QFont title = opt.font;
    title.setPixelSize(13);
    title.setWeight(QFont::DemiBold);
    p->setFont(title);
    p->setPen(Theme::Text);
    const QRectF titleRect(card.left() + 11, tr.bottom() + 8, card.width() - 22, 18);
    p->drawText(titleRect, Qt::AlignLeft | Qt::AlignVCenter,
                QFontMetricsF(title).elidedText(c.title, Qt::ElideRight, titleRect.width()));

    QFont meta = opt.font;
    meta.setPixelSize(11);
    p->setFont(meta);
    p->setPen(Theme::Muted);
    QStringList bits{QLocale().toString(c.created, "d MMM yyyy · HH:mm"), formatSize(c.size)};
    if (c.height > 0) bits << QString("%1p").arg(c.height);
    p->drawText(QRectF(card.left() + 11, tr.bottom() + 29, card.width() - 22, 16),
                Qt::AlignLeft | Qt::AlignVCenter, bits.join("  ·  "));

    if (selected || hover) {
        p->setBrush(Qt::NoBrush);
        p->setPen(QPen(selected ? Theme::Accent : Theme::Border, selected ? 2 : 1));
        p->drawPath(shape);
    }
    p->restore();
}
