#pragma once
#include "core/Clip.h"

#include <QOpenGLWidget>

struct mpv_handle;
struct mpv_render_context;

// libmpv video surface. mpv is used because it can mix several audio tracks
// live (lavfi-complex), which is what makes per-source mute/volume previewable.
class MpvWidget : public QOpenGLWidget {
    Q_OBJECT
public:
    explicit MpvWidget(QWidget *parent = nullptr);
    ~MpvWidget() override;

    void load(const QString &path, const QList<ClipTrack> &tracks);
    void unload();
    void setPaused(bool paused);
    bool isPaused() const { return m_paused; }
    void seek(double seconds, bool exact = true);
    void frameStep(bool forward);
    void setAudioMix(const QList<ClipTrack> &tracks);
    void setLoopRange(double a, double b);
    void setVolume(int percent);
    double position() const { return m_position; }
    double duration() const { return m_duration; }

    static QString mixGraph(const QList<ClipTrack> &tracks);

signals:
    void positionChanged(double seconds);
    void durationChanged(double seconds);
    void pausedChanged(bool paused);
    void fileLoaded();
    void clicked();

protected:
    void initializeGL() override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent *e) override;

private slots:
    void onMpvEvents();
    void maybeUpdate();

private:
    void releaseGL();
    static void onUpdate(void *ctx);
    static void onWakeup(void *ctx);
    void command(const QStringList &args);
    void setMpv(const char *name, const QString &value);

    mpv_handle *m_mpv = nullptr;
    mpv_render_context *m_gl = nullptr;
    double m_position = 0;
    double m_duration = 0;
    bool m_paused = true;
    QString m_graph;
};
