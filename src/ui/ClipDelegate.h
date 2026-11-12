#pragma once
#include <QStyledItemDelegate>

class ClipDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    static constexpr int CardW = 268;
    static constexpr int ThumbH = 151;
    static constexpr int CardH = ThumbH + 58;

    void paint(QPainter *p, const QStyleOptionViewItem &opt, const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override;

    // Set while a job runs so the card can show progress.
    QString busyPath;
    QString busyStage;
    double busyFraction = 0;
};
