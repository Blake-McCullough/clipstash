#pragma once
#include "core/Clip.h"
#include "core/Settings.h"

#include <QObject>
#include <QProcess>

struct ExportOptions {
    bool reencodeVideo = false;
    QString encoder = "libx264";  // libx264 | h264_nvenc | h264_vaapi
    int quality = 21;             // crf / cq / qp
    int maxHeight = 0;            // 0 = keep; only applies when re-encoding
    int audioKbps = 192;
    // Convert the file as it is: no trim, every audio track kept untouched.
    bool wholeFile = false;
    // Size-targeted encode (Discord): constrained bitrate instead of quality.
    int targetVideoKbps = 0;
    int maxFps = 0;

    static ExportOptions fromSettings(const Settings &s);
    // Options for one clip: applies the "make it play everywhere" rule (HEVC
    // and AV1 are converted to H.264) and resolves the automatic encoder.
    // Sharing never re-encodes just for frame-exact cuts.
    static ExportOptions forClip(const Settings &s, const Clip &clip, bool sharing);
    // h264_nvenc on NVIDIA systems, otherwise libx264.
    static QString autoEncoder();
    // Smallest-sensible H.264 file that fits `limitMb`: picks bitrate from the
    // clip length, then a resolution and frame rate that bitrate can carry.
    // `scale` shrinks the budget for a second attempt if the first overshot.
    static ExportOptions forDiscord(const Settings &s, const Clip &clip, double scale = 1.0);
};

// Renders a clip's edit state (trim + audio mix) to a new file with ffmpeg.
class Exporter : public QObject {
    Q_OBJECT
public:
    explicit Exporter(QObject *parent = nullptr);

    static QStringList buildArgs(const Clip &clip, const ExportOptions &opts, const QString &outPath);

    void start(const Clip &clip, const ExportOptions &opts, const QString &outPath);
    void cancel();
    bool isRunning() const { return m_proc && m_proc->state() != QProcess::NotRunning; }

signals:
    void progress(double fraction);
    void finished(bool ok, const QString &outPath, const QString &error);

private:
    QProcess *m_proc = nullptr;
    QString m_out;
    double m_duration = 0;
    QByteArray m_buf;
    QByteArray m_err;
    bool m_cancelled = false;
    Clip m_clip;
    ExportOptions m_opts;

    void launch();
};
