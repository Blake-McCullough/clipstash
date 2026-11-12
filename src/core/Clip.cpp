#include "core/Clip.h"

#include <QFileInfo>
#include <QJsonArray>
#include <cmath>

QString Clip::fileName() const { return QFileInfo(path).fileName(); }

bool Clip::needsRender() const
{
    if (isTrimmed()) return true;
    if (tracks.size() > 1) return true;
    for (const ClipTrack &t : tracks)
        if (!t.enabled || std::abs(t.volume - 1.0) > 0.01) return true;
    return false;
}

QJsonObject Clip::toJson() const
{
    QJsonObject o;
    o["title"] = title;
    o["duration"] = duration;
    o["width"] = width;
    o["height"] = height;
    o["probed"] = probed;
    o["trimStart"] = trimStart;
    o["trimEnd"] = trimEnd;
    o["favorite"] = favorite;
    o["shareUrl"] = shareUrl;
    o["shareId"] = shareId;
    QJsonArray arr;
    for (const ClipTrack &t : tracks)
        arr.append(QJsonObject{{"name", t.name}, {"enabled", t.enabled}, {"volume", t.volume}});
    o["tracks"] = arr;
    return o;
}

void Clip::applyJson(const QJsonObject &o)
{
    if (o.contains("title")) title = o["title"].toString();
    duration = o["duration"].toDouble(duration);
    width = o["width"].toInt(width);
    height = o["height"].toInt(height);
    probed = o["probed"].toBool(probed);
    trimStart = o["trimStart"].toDouble(0);
    trimEnd = o["trimEnd"].toDouble(-1);
    favorite = o["favorite"].toBool(false);
    shareUrl = o["shareUrl"].toString();
    shareId = o["shareId"].toString();
    tracks.clear();
    for (const QJsonValue &v : o["tracks"].toArray()) {
        const QJsonObject t = v.toObject();
        tracks.append({t["name"].toString(), t["enabled"].toBool(true), t["volume"].toDouble(1.0)});
    }
}

QString formatDuration(double seconds)
{
    if (seconds < 0 || !std::isfinite(seconds)) seconds = 0;
    const int total = int(seconds + 0.5);
    const int h = total / 3600, m = (total % 3600) / 60, s = total % 60;
    if (h > 0) return QString("%1:%2:%3").arg(h).arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'));
    return QString("%1:%2").arg(m).arg(s, 2, 10, QChar('0'));
}

QString formatSize(qint64 bytes)
{
    const double mb = bytes / (1024.0 * 1024.0);
    if (mb >= 1024) return QString::number(mb / 1024.0, 'f', 2) + " GB";
    if (mb >= 10) return QString::number(mb, 'f', 0) + " MB";
    return QString::number(mb, 'f', 1) + " MB";
}
