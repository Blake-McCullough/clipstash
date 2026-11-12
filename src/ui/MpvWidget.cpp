#include "ui/MpvWidget.h"

#include <QGuiApplication>
#include <QMouseEvent>
#include <QOpenGLContext>
#include <QTimer>

#include <mpv/client.h>
#include <mpv/render_gl.h>

#include <clocale>

static void *getProcAddress(void *, const char *name)
{
    QOpenGLContext *ctx = QOpenGLContext::currentContext();
    return ctx ? reinterpret_cast<void *>(ctx->getProcAddress(QByteArray(name))) : nullptr;
}

MpvWidget::MpvWidget(QWidget *parent) : QOpenGLWidget(parent)
{
    setMinimumSize(320, 180);
    std::setlocale(LC_NUMERIC, "C");  // required by libmpv
    m_mpv = mpv_create();
    if (!m_mpv) return;

    mpv_set_option_string(m_mpv, "vo", "libmpv");
    mpv_set_option_string(m_mpv, "hwdec", "auto-safe");
    mpv_set_option_string(m_mpv, "keep-open", "yes");
    mpv_set_option_string(m_mpv, "hr-seek", "yes");
    mpv_set_option_string(m_mpv, "pause", "yes");
    mpv_set_option_string(m_mpv, "osc", "no");
    mpv_set_option_string(m_mpv, "input-default-bindings", "no");
    mpv_set_option_string(m_mpv, "input-vo-keyboard", "no");
    mpv_set_option_string(m_mpv, "audio-client-name", "Clipstash");
    mpv_set_option_string(m_mpv, "config", "no");
    if (qEnvironmentVariableIsSet("CLIPSTASH_MPV_LOG")) {
        mpv_set_option_string(m_mpv, "terminal", "yes");
        mpv_set_option_string(m_mpv, "msg-level", "all=v");
    } else {
        mpv_set_option_string(m_mpv, "terminal", "no");
    }
    mpv_set_option_string(m_mpv, "idle", "yes");
    // Cheap rendering path: plain bilinear scaling and no float intermediate
    // framebuffers. A clip preview does not need mpv's high-quality scalers,
    // and this keeps GPU use negligible while a game is running.
    mpv_set_option_string(m_mpv, "scale", "bilinear");
    mpv_set_option_string(m_mpv, "dscale", "bilinear");
    mpv_set_option_string(m_mpv, "dither-depth", "no");
    mpv_set_option_string(m_mpv, "correct-downscaling", "no");
    mpv_set_option_string(m_mpv, "linear-downscaling", "no");
    mpv_set_option_string(m_mpv, "sigmoid-upscaling", "no");
    mpv_set_option_string(m_mpv, "hdr-compute-peak", "no");
    // Escape hatch for odd GPU/driver setups, e.g.
    //   CLIPSTASH_MPV_OPTS="hwdec=no,gpu-dumb-mode=yes" clipstash
    const QStringList extra = qEnvironmentVariable("CLIPSTASH_MPV_OPTS").split(',', Qt::SkipEmptyParts);
    for (const QString &kv : extra) {
        const int eq = kv.indexOf('=');
        if (eq > 0)
            mpv_set_option_string(m_mpv, kv.left(eq).trimmed().toUtf8().constData(),
                                  kv.mid(eq + 1).trimmed().toUtf8().constData());
    }
    if (mpv_initialize(m_mpv) < 0) {
        mpv_destroy(m_mpv);
        m_mpv = nullptr;
        return;
    }
    mpv_observe_property(m_mpv, 0, "time-pos", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "duration", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "pause", MPV_FORMAT_FLAG);
    mpv_set_wakeup_callback(m_mpv, onWakeup, this);
}

MpvWidget::~MpvWidget()
{
    releaseGL();
    if (m_mpv) mpv_terminate_destroy(m_mpv);
}

void MpvWidget::releaseGL()
{
    if (!m_gl) return;
    makeCurrent();
    mpv_render_context_free(m_gl);
    m_gl = nullptr;
    doneCurrent();
}

void MpvWidget::initializeGL()
{
    if (!m_mpv) return;
    // Qt recreates the widget's GL context when the window is first switched to
    // GL compositing (i.e. the first time the editor is shown). mpv's
    // framebuffers die with the old context, so rebuild the render context
    // whenever that happens instead of drawing black.
    connect(context(), &QOpenGLContext::aboutToBeDestroyed, this, &MpvWidget::releaseGL,
            Qt::ConnectionType(Qt::DirectConnection | Qt::UniqueConnection));
    if (m_gl) return;
    mpv_opengl_init_params glInit{getProcAddress, nullptr};
    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_API_TYPE, const_cast<char *>(MPV_RENDER_API_TYPE_OPENGL)},
        {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &glInit},
        {MPV_RENDER_PARAM_INVALID, nullptr},
        {MPV_RENDER_PARAM_INVALID, nullptr},
    };
    // Hand mpv the native display so VAAPI/NVDEC hardware decoding can interop.
    if (auto *x11 = qGuiApp->nativeInterface<QNativeInterface::QX11Application>()) {
        params[2] = {MPV_RENDER_PARAM_X11_DISPLAY, x11->display()};
    }
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    else if (auto *wl = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>()) {
        params[2] = {MPV_RENDER_PARAM_WL_DISPLAY, wl->display()};
    }
#endif
    if (mpv_render_context_create(&m_gl, m_mpv, params) < 0) {
        m_gl = nullptr;
        return;
    }
    mpv_render_context_set_update_callback(m_gl, onUpdate, this);
    QTimer::singleShot(0, this, [this] { update(); });
}

void MpvWidget::paintGL()
{
    if (!m_gl) return;
    const qreal dpr = devicePixelRatioF();
    mpv_opengl_fbo fbo{int(defaultFramebufferObject()), int(width() * dpr), int(height() * dpr), 0};
    int flip = 1;
    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_OPENGL_FBO, &fbo},
        {MPV_RENDER_PARAM_FLIP_Y, &flip},
        {MPV_RENDER_PARAM_INVALID, nullptr},
    };
    mpv_render_context_render(m_gl, params);
}

void MpvWidget::onUpdate(void *ctx)
{
    QMetaObject::invokeMethod(static_cast<MpvWidget *>(ctx), "maybeUpdate", Qt::QueuedConnection);
}

void MpvWidget::onWakeup(void *ctx)
{
    QMetaObject::invokeMethod(static_cast<MpvWidget *>(ctx), "onMpvEvents", Qt::QueuedConnection);
}

void MpvWidget::maybeUpdate()
{
    if (isVisible()) update();
}

void MpvWidget::onMpvEvents()
{
    while (m_mpv) {
        mpv_event *ev = mpv_wait_event(m_mpv, 0);
        if (ev->event_id == MPV_EVENT_NONE) break;
        if (ev->event_id == MPV_EVENT_FILE_LOADED) {
            emit fileLoaded();
        } else if (ev->event_id == MPV_EVENT_PLAYBACK_RESTART) {
            update();
        } else if (ev->event_id == MPV_EVENT_PROPERTY_CHANGE) {
            auto *prop = static_cast<mpv_event_property *>(ev->data);
            const QByteArray name(prop->name);
            if (name == "time-pos" && prop->format == MPV_FORMAT_DOUBLE) {
                m_position = *static_cast<double *>(prop->data);
                emit positionChanged(m_position);
            } else if (name == "duration" && prop->format == MPV_FORMAT_DOUBLE) {
                m_duration = *static_cast<double *>(prop->data);
                emit durationChanged(m_duration);
            } else if (name == "pause" && prop->format == MPV_FORMAT_FLAG) {
                m_paused = *static_cast<int *>(prop->data);
                emit pausedChanged(m_paused);
            }
        }
    }
}

void MpvWidget::command(const QStringList &args)
{
    if (!m_mpv) return;
    QList<QByteArray> utf8;
    for (const QString &a : args) utf8 << a.toUtf8();
    QList<const char *> argv;
    for (const QByteArray &a : std::as_const(utf8)) argv << a.constData();
    argv << nullptr;
    mpv_command_async(m_mpv, 0, argv.data());
}

void MpvWidget::setMpv(const char *name, const QString &value)
{
    if (m_mpv) mpv_set_property_string(m_mpv, name, value.toUtf8().constData());
}

QString MpvWidget::mixGraph(const QList<ClipTrack> &tracks)
{
    QStringList chains;
    QString inputs;
    int n = 0;
    for (int i = 0; i < tracks.size(); ++i) {
        if (!tracks[i].enabled || tracks[i].volume <= 0.001) continue;
        chains << QString("[aid%1]volume=%2[a%3]").arg(i + 1).arg(tracks[i].volume, 0, 'f', 2).arg(n);
        inputs += QString("[a%1]").arg(n);
        ++n;
    }
    if (n == 0) return {};
    if (n == 1) return chains[0].left(chains[0].lastIndexOf('[')) + "[ao]";
    chains << QString("%1amix=inputs=%2:normalize=0:dropout_transition=0[ao]").arg(inputs).arg(n);
    return chains.join(';');
}

void MpvWidget::load(const QString &path, const QList<ClipTrack> &tracks)
{
    m_position = 0;
    m_duration = 0;
    m_graph = mixGraph(tracks);
    setMpv("ab-loop-a", "no");
    setMpv("ab-loop-b", "no");
    setMpv("lavfi-complex", m_graph);
    setMpv("aid", tracks.isEmpty() ? "auto" : "no");
    setMpv("pause", "yes");
    command({"loadfile", path});
}

void MpvWidget::unload()
{
    command({"stop"});
    setMpv("lavfi-complex", "");
    m_graph.clear();
}

void MpvWidget::setAudioMix(const QList<ClipTrack> &tracks)
{
    if (tracks.isEmpty()) return;
    const QString graph = mixGraph(tracks);
    if (graph == m_graph) return;
    m_graph = graph;
    setMpv("lavfi-complex", graph);
}

void MpvWidget::setPaused(bool paused) { setMpv("pause", paused ? "yes" : "no"); }

void MpvWidget::seek(double seconds, bool exact)
{
    command({"seek", QString::number(qMax(0.0, seconds), 'f', 3), exact ? "absolute+exact" : "absolute+keyframes"});
}

void MpvWidget::frameStep(bool forward) { command({forward ? "frame-step" : "frame-back-step"}); }

void MpvWidget::setLoopRange(double a, double b)
{
    setMpv("ab-loop-a", QString::number(a, 'f', 3));
    setMpv("ab-loop-b", QString::number(b, 'f', 3));
}

void MpvWidget::setVolume(int percent) { setMpv("volume", QString::number(percent)); }

void MpvWidget::mousePressEvent(QMouseEvent *e)
{
    if (e->button() == Qt::LeftButton) emit clicked();
    QOpenGLWidget::mousePressEvent(e);
}
