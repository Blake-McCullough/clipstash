#pragma once
#include <QPixmap>
#include <QWidget>

// Scrubber with a filmstrip background and draggable trim handles.
class Timeline : public QWidget {
    Q_OBJECT
public:
    explicit Timeline(QWidget *parent = nullptr);

    void setDuration(double seconds);
    void setPosition(double seconds);
    void setTrim(double start, double end);
    void setFilmstrip(const QPixmap &strip);
    double trimStart() const { return m_start; }
    double trimEnd() const { return m_end; }

signals:
    void seekRequested(double seconds, bool finished);
    void trimChanged(double start, double end, bool finished);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    QSize sizeHint() const override { return {600, 74}; }

private:
    enum class Drag { None, Start, End, Playhead };
    QRectF bar() const;
    double xToTime(qreal x) const;
    qreal timeToX(double t) const;
    Drag hitTest(const QPointF &pos) const;

    double m_duration = 0;
    double m_position = 0;
    double m_start = 0;
    double m_end = 0;
    QPixmap m_strip;
    Drag m_drag = Drag::None;
};
