#include "core/MediaTools.h"

#include "core/Proc.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace MediaTools {

ProbeResult parseProbeJson(const QByteArray &json)
{
    ProbeResult r;
    const QJsonObject root = QJsonDocument::fromJson(json).object();
    if (root.isEmpty()) return r;
    r.duration = root["format"].toObject()["duration"].toString().toDouble();
    for (const QJsonValue &v : root["streams"].toArray()) {
        const QJsonObject s = v.toObject();
        const QString type = s["codec_type"].toString();
        if (type == "video" && r.width == 0) {
            r.width = s["width"].toInt();
            r.height = s["height"].toInt();
            if (r.duration <= 0) r.duration = s["duration"].toString().toDouble();
        } else if (type == "audio") {
            r.audioTitles << s["tags"].toObject()["title"].toString();
        }
    }
    r.ok = r.width > 0 && r.duration > 0;
    return r;
}

void probe(const QString &path, std::function<void(const ProbeResult &)> cb)
{
    ProcQueue::shared()->run("ffprobe",
        {"-v", "error", "-print_format", "json", "-show_format", "-show_streams", path},
        [cb](int code, const QByteArray &out, const QByteArray &) {
            cb(code == 0 ? parseProbeJson(out) : ProbeResult{});
        });
}

void thumbnail(const QString &path, const QString &outJpg, double at, std::function<void(bool)> cb)
{
    ProcQueue::shared()->run("ffmpeg",
        {"-y", "-v", "error", "-ss", QString::number(at, 'f', 2), "-i", path, "-an",
         "-frames:v", "1", "-vf", "scale=480:-2", "-q:v", "4", outJpg},
        [cb](int code, const QByteArray &, const QByteArray &) { cb(code == 0); });
}

void filmstrip(const QString &path, const QString &outJpg, double duration, int frames,
               std::function<void(bool)> cb)
{
    if (duration <= 0 || frames < 1) {
        cb(false);
        return;
    }
    // Only keyframes are decoded, which keeps this fast even for long 4K clips.
    // tpad holds the last keyframe so the strip is always completely filled.
    const QString vf = QString("tpad=stop_mode=clone:stop_duration=%3,fps=%1/%2,scale=-2:54,tile=%1x1")
                           .arg(frames).arg(duration, 0, 'f', 3).arg(duration / frames + 4.0, 0, 'f', 2);
    ProcQueue::shared()->run("ffmpeg",
        {"-y", "-v", "error", "-skip_frame", "nokey", "-i", path, "-an", "-vf", vf,
         "-frames:v", "1", "-q:v", "5", outJpg},
        [cb](int code, const QByteArray &, const QByteArray &) { cb(code == 0); });
}

NamedList parseGsrList(const QByteArray &out)
{
    NamedList list;
    for (const QByteArray &raw : out.split('\n')) {
        const QString line = QString::fromUtf8(raw).trimmed();
        if (line.isEmpty()) continue;
        const int bar = line.indexOf('|');
        if (bar < 0) list.append({line, QString()});
        else list.append({line.left(bar), line.mid(bar + 1)});
    }
    return list;
}

void gsrList(const QString &binary, const QString &flag, std::function<void(const NamedList &)> cb)
{
    ProcQueue::shared()->run(binary, {flag},
        [cb](int code, const QByteArray &out, const QByteArray &) {
            cb(code == 0 ? parseGsrList(out) : NamedList{});
        });
}

}
