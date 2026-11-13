#include "core/Exporter.h"

#include <QFile>

ExportOptions ExportOptions::fromSettings(const Settings &s)
{
    ExportOptions o;
    o.reencodeVideo = s.exportVideoMode == "reencode";
    o.encoder = s.exportEncoder;
    o.quality = s.exportQuality;
    o.maxHeight = s.exportMaxHeight;
    o.audioKbps = s.exportAudioKbps;
    return o;
}

QString ExportOptions::autoEncoder()
{
    return QFile::exists("/proc/driver/nvidia/version") ? "h264_nvenc" : "libx264";
}

ExportOptions ExportOptions::forClip(const Settings &s, const Clip &clip, bool sharing)
{
    ExportOptions o = fromSettings(s);
    if (sharing) o.reencodeVideo = false;
    if (s.exportCompat && !clip.playsEverywhere()) o.reencodeVideo = true;
    if (o.encoder == "auto") o.encoder = autoEncoder();
    return o;
}

Exporter::Exporter(QObject *parent) : QObject(parent) {}

QStringList Exporter::buildArgs(const Clip &clip, const ExportOptions &o, const QString &outPath)
{
    QStringList a{"-y", "-hide_banner", "-nostats", "-progress", "pipe:1"};
    const bool vaapi = o.reencodeVideo && o.encoder == "h264_vaapi";
    if (vaapi) a << "-vaapi_device" << "/dev/dri/renderD128";

    if (clip.isTrimmed() && !o.wholeFile) {
        a << "-ss" << QString::number(clip.effectiveStart(), 'f', 3);
        a << "-to" << QString::number(clip.effectiveEnd(), 'f', 3);
    }
    a << "-i" << clip.path;
    a << "-map" << "0:v:0";

    // audio
    QList<int> enabled;
    for (int i = 0; i < clip.tracks.size(); ++i)
        if (clip.tracks[i].enabled && clip.tracks[i].volume > 0.001) enabled << i;

    if (clip.tracks.isEmpty() || o.wholeFile) {
        a << "-map" << "0:a?" << "-c:a" << "copy";
    } else if (enabled.isEmpty()) {
        a << "-an";
    } else if (enabled.size() == 1 && qAbs(clip.tracks[enabled[0]].volume - 1.0) < 0.01) {
        a << "-map" << QString("0:a:%1").arg(enabled[0]) << "-c:a" << "copy";
    } else {
        QStringList chains;
        QString inputs;
        for (int n = 0; n < enabled.size(); ++n) {
            const int i = enabled[n];
            const QString label = enabled.size() == 1 ? "aout" : QString("a%1").arg(n);
            chains << QString("[0:a:%1]volume=%2[%3]")
                          .arg(i).arg(clip.tracks[i].volume, 0, 'f', 2).arg(label);
            inputs += QString("[a%1]").arg(n);
        }
        if (enabled.size() > 1)
            chains << QString("%1amix=inputs=%2:normalize=0:dropout_transition=0[aout]")
                          .arg(inputs).arg(enabled.size());
        a << "-filter_complex" << chains.join(';');
        a << "-map" << "[aout]" << "-c:a" << "aac" << "-b:a" << QString("%1k").arg(o.audioKbps);
    }

    // video
    if (!o.reencodeVideo) {
        a << "-c:v" << "copy";
    } else {
        QStringList vf;
        if (o.maxHeight > 0) vf << QString("scale=-2:'min(ih,%1)'").arg(o.maxHeight);
        if (vaapi) vf << "format=nv12" << "hwupload";
        if (!vf.isEmpty()) a << "-vf" << vf.join(',');
        const QString q = QString::number(o.quality);
        if (o.encoder == "h264_nvenc")
            a << "-c:v" << "h264_nvenc" << "-preset" << "p5" << "-rc" << "vbr" << "-cq" << q << "-b:v" << "0"
              << "-pix_fmt" << "yuv420p";
        else if (vaapi)
            a << "-c:v" << "h264_vaapi" << "-qp" << q;
        else
            a << "-c:v" << "libx264" << "-preset" << "veryfast" << "-crf" << q << "-pix_fmt" << "yuv420p";
    }
    a << "-avoid_negative_ts" << "make_zero" << "-movflags" << "+faststart" << outPath;
    return a;
}

void Exporter::start(const Clip &clip, const ExportOptions &opts, const QString &outPath)
{
    if (isRunning()) return;
    m_out = outPath;
    m_clip = clip;
    m_opts = opts;
    m_duration = opts.wholeFile ? clip.duration : clip.editedDuration();
    m_cancelled = false;
    launch();
}

void Exporter::launch()
{
    if (m_proc) m_proc->deleteLater();
    m_proc = new QProcess(this);
    m_buf.clear();
    m_err.clear();
    QProcess *proc = m_proc;

    connect(proc, &QProcess::readyReadStandardOutput, this, [this, proc] {
        m_buf += proc->readAllStandardOutput();
        int nl;
        while ((nl = m_buf.indexOf('\n')) >= 0) {
            const QByteArray line = m_buf.left(nl).trimmed();
            m_buf.remove(0, nl + 1);
            // out_time_us (and the misnamed out_time_ms) are both microseconds.
            if (line.startsWith("out_time_us=") || line.startsWith("out_time_ms=")) {
                const double sec = line.mid(12).toDouble() / 1e6;
                if (m_duration > 0) emit progress(qBound(0.0, sec / m_duration, 1.0));
            }
        }
    });
    connect(proc, &QProcess::readyReadStandardError, this, [this, proc] {
        m_err += proc->readAllStandardError();
        if (m_err.size() > 8192) m_err = m_err.right(8192);
    });
    connect(proc, &QProcess::finished, this, [this](int code, QProcess::ExitStatus st) {
        const bool ok = !m_cancelled && st == QProcess::NormalExit && code == 0;
        if (!ok && !m_cancelled && m_opts.reencodeVideo && m_opts.encoder != "libx264") {
            // The hardware encoder was refused (driver, session limit, odd
            // pixel format). The CPU encoder always works, just slower.
            QFile::remove(m_out);
            m_opts.encoder = "libx264";
            emit progress(0);
            launch();
            return;
        }
        QString error;
        if (!ok) {
            QFile::remove(m_out);
            if (m_cancelled) error = "Cancelled";
            else {
                const QStringList lines = QString::fromUtf8(m_err).split('\n', Qt::SkipEmptyParts);
                error = lines.isEmpty() ? QString("ffmpeg failed (%1)").arg(code)
                                        : QStringList(lines.mid(qMax(0, lines.size() - 3))).join('\n');
            }
        }
        emit finished(ok, m_out, error);
    });
    connect(proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart)
            emit finished(false, m_out, "Could not start ffmpeg. Is it installed?");
    });
    proc->start("ffmpeg", buildArgs(m_clip, m_opts, m_out));
}

void Exporter::cancel()
{
    if (!isRunning()) return;
    m_cancelled = true;
    m_proc->kill();
}
