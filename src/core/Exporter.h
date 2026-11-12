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

    static ExportOptions fromSettings(const Settings &s);
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
};
