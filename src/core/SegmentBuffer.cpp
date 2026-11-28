#include "core/SegmentBuffer.h"

#include "core/Recorder.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QProcess>
#include <QStandardPaths>
#include <QtMath>

namespace {
int qualityValue(const QString &quality, bool x264)
{
    // Constant-quality targets per preset; x264's CRF scale sits a little lower.
    if (quality == "medium") return x264 ? 28 : 30;
    if (quality == "high") return x264 ? 24 : 26;
    if (quality == "ultra") return x264 ? 18 : 19;
    return x264 ? 21 : 23;  // very_high
}

int monitorIndex(const QString &source)
{
    QString s = source;
    if (s.startsWith("monitor:")) s = s.mid(8);
    bool ok = false;
    const int n = s.toInt(&ok);
    return ok && n >= 0 ? n : 0;
}

int maxHeightFor(const Settings &s)
{
    int h = 0;
    const QString r = s.resolution.trimmed();
    const int x = r.indexOf('x');
    if (x > 0) h = r.mid(x + 1).toInt();
    if (s.lowResource) h = h > 0 ? qMin(h, 1080) : 1080;
    return h;
}
}

// ---------------------------------------------------------------- probe

EncoderProbe *EncoderProbe::instance()
{
    static EncoderProbe *p = new EncoderProbe(QCoreApplication::instance());
    return p;
}

QString EncoderProbe::encoderName(const QString &family, const QString &codec)
{
    const QString c = codec == "hevc" ? "hevc" : codec == "av1" ? "av1" : "h264";
    if (family == "nvenc") return c + "_nvenc";
    if (family == "amf") return c + "_amf";
    if (family == "qsv") return c + "_qsv";
    if (c == "hevc") return "libx265";
    if (c == "av1") return "libsvtav1";
    return "libx264";
}

QString EncoderProbe::label(const QString &family)
{
    if (family == "nvenc") return "NVIDIA (NVENC)";
    if (family == "amf") return "AMD (AMF)";
    if (family == "qsv") return "Intel (Quick Sync)";
    if (family == "cpu") return "CPU (x264, uses more resources)";
    return family;
}

void EncoderProbe::run(const QString &ffmpeg)
{
    if (m_done || m_running) return;
    m_running = true;
    auto families = std::make_shared<QStringList>(QStringList{"nvenc", "amf", "qsv"});
    auto next = std::make_shared<std::function<void()>>();
    *next = [this, ffmpeg, families, next] {
        if (families->isEmpty()) {
            // Last: is the GPU scaling filter there (needs FFmpeg 8)?
            auto *p = new QProcess(this);
            connect(p, &QProcess::finished, this, [this, p] {
                m_scaleD3D11 = p->readAllStandardOutput().contains(" scale_d3d11 ");
                p->deleteLater();
                m_available << "cpu";
                m_done = true;
                m_running = false;
                Recorder::log("encoders available: " + m_available.join(", ")
                              + (m_scaleD3D11 ? " (GPU scaling available)" : ""));
                emit finished();
            });
            connect(p, &QProcess::errorOccurred, this, [this, p](QProcess::ProcessError e) {
                if (e != QProcess::FailedToStart) return;
                p->deleteLater();
                m_available << "cpu";
                m_done = true;
                m_running = false;
                emit finished();
            });
            p->start(ffmpeg, {"-hide_banner", "-filters"});
            return;
        }
        const QString fam = families->takeFirst();
        auto *p = new QProcess(this);
        connect(p, &QProcess::finished, this, [this, p, fam, next](int code, QProcess::ExitStatus st) {
            p->deleteLater();
            if (st == QProcess::NormalExit && code == 0) m_available << fam;
            (*next)();
        });
        connect(p, &QProcess::errorOccurred, this, [p, next](QProcess::ProcessError e) {
            if (e != QProcess::FailedToStart) return;
            p->deleteLater();
            (*next)();
        });
        // One tiny frame: succeeds only if the encoder exists AND the GPU and
        // driver for it are actually present.
        p->start(ffmpeg, {"-hide_banner", "-v", "error", "-f", "lavfi", "-i", "color=c=black:s=256x256:r=30:d=0.1",
                          "-frames:v", "2", "-c:v", encoderName(fam, "h264"), "-f", "null", "-"});
    };
    (*next)();
}

// ---------------------------------------------------------------- buffer

SegmentBuffer::SegmentBuffer(QObject *parent) : QObject(parent)
{
    m_poll.setInterval(200);
    connect(&m_poll, &QTimer::timeout, this, &SegmentBuffer::poll);
}

QString SegmentBuffer::defaultDir()
{
    // Local, not on the clip drive: it is written all the time, so a fast
    // internal disk is best and the clip drive can sleep.
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/buffer";
}

QString SegmentBuffer::resolveEncoder(const Settings &s)
{
    const QStringList have = EncoderProbe::instance()->available();
    if (s.videoEncoder != "auto" && !s.videoEncoder.isEmpty()) {
        if (have.isEmpty() || have.contains(s.videoEncoder)) return s.videoEncoder;
        Recorder::log("encoder " + s.videoEncoder + " is not available here, using automatic");
    }
    for (const char *f : {"nvenc", "amf", "qsv"})
        if (have.contains(f)) return f;
    return "cpu";
}

QStringList SegmentBuffer::buildArgs(const Settings &s, const QString &dir, const QList<QStringList> &audioInputs,
                                     const QStringList &trackNames, const QString &family, bool haveScaleFilter,
                                     const QStringList &testVideo)
{
    const bool low = s.lowResource;
    const int fps = qBound(1, low ? qMin(s.fps, 30) : s.fps, 240);
    const bool gpuFrames = testVideo.isEmpty();  // screen capture hands over GPU textures
    QStringList a{"-hide_banner", "-nostats", "-loglevel", "warning", "-y"};

    if (!testVideo.isEmpty()) {
        a << testVideo;
    } else {
        a << "-f" << "lavfi" << "-i"
          << QString("ddagrab=output_idx=%1:framerate=%2:draw_mouse=%3")
                 .arg(monitorIndex(s.captureSource)).arg(fps).arg(s.recordCursor ? 1 : 0);
    }
    for (const QStringList &in : audioInputs) a << in;

    // Video filters, all on the GPU unless the CPU encoder was chosen.
    QStringList vf;
    const int maxH = maxHeightFor(s);
    if (maxH > 0) {
        if (gpuFrames && haveScaleFilter) vf << QString("scale_d3d11=width=-2:height='min(ih,%1)'").arg(maxH);
        else if (!gpuFrames) vf << QString("scale=-2:'min(ih,%1)'").arg(maxH);
    }
    if (gpuFrames && family == "cpu") vf << "hwdownload" << "format=bgra";
    if (gpuFrames && family == "qsv") vf << "hwmap=derive_device=qsv" << "format=qsv";
    if (!gpuFrames && family == "cpu") vf << "format=yuv420p";
    if (!vf.isEmpty()) a << "-vf" << vf.join(',');

    // Encoder.
    const bool cbr = low || s.bitrateMode == "cbr";
    const int kbps = low ? 8000 : qMax(1000, s.bitrateKbps);
    const QString rate = QString("%1k").arg(kbps), buf = QString("%1k").arg(kbps * 2);
    const QString enc = EncoderProbe::encoderName(family, s.codec);
    const QString q = QString::number(qualityValue(s.quality, family == "cpu"));
    a << "-c:v" << enc;
    if (family == "nvenc") {
        a << "-preset" << (low ? "p1" : "p4") << "-bf" << "0";
        if (cbr) a << "-rc" << "cbr" << "-b:v" << rate << "-maxrate" << rate << "-bufsize" << buf;
        else a << "-rc" << "vbr" << "-cq" << q << "-b:v" << "0";
    } else if (family == "amf") {
        a << "-usage" << "lowlatency" << "-quality" << (low ? "speed" : "balanced") << "-bf" << "0";
        if (cbr) a << "-rc" << "cbr" << "-b:v" << rate << "-maxrate" << rate << "-bufsize" << buf;
        else a << "-rc" << "cqp" << "-qp_i" << q << "-qp_p" << q;
    } else if (family == "qsv") {
        a << "-preset" << (low ? "veryfast" : "medium") << "-bf" << "0";
        if (cbr) a << "-b:v" << rate << "-maxrate" << rate << "-bufsize" << buf;
        else a << "-global_quality" << q;
    } else {
        a << "-preset" << (low ? "ultrafast" : "veryfast");
        if (enc == "libx264" || enc == "libx265") a << "-tune" << "zerolatency";
        if (cbr) a << "-b:v" << rate << "-maxrate" << rate << "-bufsize" << buf;
        else a << "-crf" << q;
    }
    // A keyframe every piece, so every piece can start a clip.
    a << "-g" << QString::number(fps * kSegmentSeconds)
      << "-force_key_frames" << QString("expr:gte(t,n_forced*%1)").arg(kSegmentSeconds);

    a << "-map" << "0:v:0";
    for (int i = 0; i < audioInputs.size(); ++i) {
        a << "-map" << QString("%1:a:0").arg(i + 1);
        const QString name = trackNames.value(i, QString("Track %1").arg(i + 1));
        a << QString("-metadata:s:a:%1").arg(i) << "title=" + name
          << QString("-metadata:s:a:%1").arg(i) << "handler_name=" + name;
    }
    if (!audioInputs.isEmpty()) a << "-c:a" << "aac" << "-b:a" << (low ? "128k" : "192k");
    a << "-max_muxing_queue_size" << "2048";

    // The rolling set of pieces: enough for the clip, the headroom for queued
    // presses and the time it takes to join them.
    const int keep = int(qCeil((s.replaySeconds + 30.0) / kSegmentSeconds)) + 2;
    a << "-f" << "segment" << "-segment_time" << QString::number(kSegmentSeconds)
      << "-segment_format" << "mpegts" << "-segment_wrap" << QString::number(keep)
      << "-segment_list" << dir + "/segments.csv" << "-segment_list_type" << "csv"
      << "-segment_list_size" << QString::number(keep) << "-segment_list_flags" << "+live"
      << "-reset_timestamps" << "0" << dir + "/piece_%03d.ts";
    return a;
}

void SegmentBuffer::begin(const QString &dir, int keepSeconds)
{
    end();
    m_dir = dir;
    m_listFile = dir + "/segments.csv";
    m_keep = int(qCeil(double(keepSeconds) / kSegmentSeconds)) + 4;
    m_segments.clear();
    m_offset = 1e9;
    m_clock.start();
    m_poll.start();
}

void SegmentBuffer::end()
{
    m_poll.stop();
    for (const Waiter &w : std::as_const(m_waiters)) w.ready(false);
    m_waiters.clear();
}

double SegmentBuffer::streamTimeAt(qint64 wallMs) const
{
    if (m_offset > 1e8) return wallMs / 1000.0;  // nothing seen yet
    return wallMs / 1000.0 - m_offset;
}

void SegmentBuffer::poll()
{
    QFile f(m_listFile);
    if (f.open(QIODevice::ReadOnly)) {
        const QList<QByteArray> lines = f.readAll().split('\n');
        const double now = m_clock.elapsed() / 1000.0;
        for (const QByteArray &raw : lines) {
            const QList<QByteArray> parts = raw.trimmed().split(',');
            if (parts.size() < 3) continue;
            Segment seg;
            seg.file = m_dir + "/" + QString::fromUtf8(parts[0]);
            seg.start = parts[1].toDouble();
            seg.end = parts[2].toDouble();
            if (seg.end <= seg.start) continue;
            bool known = false;
            for (const Segment &k : std::as_const(m_segments))
                if (k.file == seg.file && qAbs(k.start - seg.start) < 0.001) known = true;
            if (known) continue;
            // The file name is reused as the ring wraps: forget its old contents.
            for (int i = int(m_segments.size()) - 1; i >= 0; --i)
                if (m_segments[i].file == seg.file) m_segments.removeAt(i);
            m_segments.append(seg);
            // Seen a little after it was finished, never before: the smallest
            // gap is the best estimate of how stream time maps to our clock.
            m_offset = qMin(m_offset, now - seg.end);

            if (m_capturing && seg.end > m_captureFrom) {
                const QString copy = QString("%1/%2.ts").arg(m_captureDir).arg(m_captured.size(), 6, 10, QChar('0'));
                if (QFile::copy(seg.file, copy)) {
                    Segment c = seg;
                    c.file = copy;
                    m_captured.append(c);
                } else {
                    Recorder::log("recording: could not keep piece " + seg.file);
                }
            }
            emit segmentAdded(seg);
        }
        std::sort(m_segments.begin(), m_segments.end(), [](const Segment &x, const Segment &y) { return x.start < y.start; });
        while (m_segments.size() > m_keep) m_segments.removeFirst();
    }

    // Anyone waiting for a moment to be on disk?
    const double latest = m_segments.isEmpty() ? -1 : m_segments.last().end;
    for (int i = int(m_waiters.size()) - 1; i >= 0; --i) {
        if (latest >= m_waiters[i].at) {
            const auto ready = m_waiters[i].ready;
            m_waiters.removeAt(i);
            ready(true);
        } else if (m_waiters[i].since.elapsed() > 20000) {
            const auto ready = m_waiters[i].ready;
            m_waiters.removeAt(i);
            ready(false);
        }
    }
}

void SegmentBuffer::waitFor(double at, std::function<void(bool)> ready)
{
    if (!m_segments.isEmpty() && m_segments.last().end >= at) {
        ready(true);
        return;
    }
    Waiter w;
    w.at = at;
    w.since.start();
    w.ready = std::move(ready);
    m_waiters.append(w);
}

void SegmentBuffer::concat(const QStringList &files, double from, double to, const QString &out,
                           std::function<void(bool ok, const QString &error)> done)
{
    // Pieces are MPEG-TS, which can simply be joined end to end. Runs in the
    // background: it copies a minute or more of video.
    QStringList args{"-hide_banner", "-v", "error", "-y"};
    if (from > 0.01) args << "-ss" << QString::number(from, 'f', 3);
    if (to > 0) args << "-to" << QString::number(to, 'f', 3);
    args << "-i" << "concat:" + files.join('|') << "-map" << "0" << "-c" << "copy";
    for (int i = 0; i < m_trackNames.size(); ++i)
        args << QString("-metadata:s:a:%1").arg(i) << "title=" + m_trackNames[i]
             << QString("-metadata:s:a:%1").arg(i) << "handler_name=" + m_trackNames[i];
    args << "-bsf:a" << "aac_adtstoasc" << "-movflags" << "+faststart" << "-f" << "mp4" << out;
    auto *p = new QProcess(this);
    connect(p, &QProcess::finished, this, [p, out, done](int code, QProcess::ExitStatus st) {
        const QString err = QString::fromUtf8(p->readAllStandardError()).trimmed().section('\n', -1);
        p->deleteLater();
        const bool ok = st == QProcess::NormalExit && code == 0;
        if (!ok) QFile::remove(out);
        done(ok, ok ? QString() : err);
    });
    connect(p, &QProcess::errorOccurred, this, [p, done](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart) return;
        p->deleteLater();
        done(false, "Could not start ffmpeg to join the clip.");
    });
    p->start("ffmpeg", args);
}

void SegmentBuffer::assemble(double at, int length, const QString &out, Done done)
{
    QPointer<SegmentBuffer> self(this);
    waitFor(at, [self, at, length, out, done](bool ok) {
        if (!self) return;
        if (!ok) {
            done(false, "The recorder stopped writing before the clip could be saved.", 0);
            return;
        }
        QStringList files;
        double last = 0;
        for (const Segment &seg : std::as_const(self->m_segments)) {
            if (seg.end <= at - length || seg.start >= at) continue;
            files << seg.file;
            last = seg.end;
        }
        if (files.isEmpty()) {
            done(false, "Nothing was recorded for that moment.", 0);
            return;
        }
        self->concat(files, 0, 0, out, [=](bool joined, const QString &error) {
            if (!joined) done(false, "Joining the clip failed: " + error, 0);
            else done(true, {}, qMax(0.0, last - at));
        });
    });
}

void SegmentBuffer::startCapture(double at)
{
    m_captureDir = m_dir + "/capture";
    QDir(m_captureDir).removeRecursively();
    QDir().mkpath(m_captureDir);
    m_captured.clear();
    m_captureFrom = at;
    m_capturing = true;
}

void SegmentBuffer::stopCapture(double at, const QString &out, std::function<void(bool, const QString &)> done)
{
    QPointer<SegmentBuffer> self(this);
    waitFor(at, [self, at, out, done](bool) {
        if (!self) return;
        self->m_capturing = false;
        const QList<Segment> pieces = self->m_captured;
        self->m_captured.clear();
        if (pieces.isEmpty()) {
            done(false, "Nothing was recorded.");
            return;
        }
        QStringList files;
        for (const Segment &p : pieces) files << p.file;
        const double base = pieces.first().start;
        const double from = qMax(0.0, self->m_captureFrom - base);
        const double to = qMax(from + 0.5, at - base);
        const QString dir = self->m_captureDir;
        self->concat(files, from, to, out, [=](bool ok, const QString &error) {
            QDir(dir).removeRecursively();
            done(ok, error);
        });
    });
}
