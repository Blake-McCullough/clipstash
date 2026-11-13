#pragma once
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>
#include <functional>

struct ProbeResult {
    bool ok = false;
    double duration = 0;
    int width = 0;
    int height = 0;
    QString videoCodec;  // ffprobe codec_name, e.g. h264, hevc, av1
    QStringList audioTitles;  // one entry per audio stream (may be empty strings)
};

namespace MediaTools {
ProbeResult parseProbeJson(const QByteArray &json);
void probe(const QString &path, std::function<void(const ProbeResult &)> cb);
void thumbnail(const QString &path, const QString &outJpg, double atSeconds,
               std::function<void(bool ok)> cb);
// One wide image made of `frames` evenly spaced frames, for the editor timeline.
void filmstrip(const QString &path, const QString &outJpg, double duration, int frames,
               std::function<void(bool ok)> cb);

using NamedList = QList<QPair<QString, QString>>;  // (value, description)
// gpu-screen-recorder --list-* output: "value|description" or plain "value" lines.
NamedList parseGsrList(const QByteArray &out);
void gsrList(const QString &binary, const QString &flag, std::function<void(const NamedList &)> cb);
}
