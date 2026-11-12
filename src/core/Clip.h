#pragma once
#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QMetaType>
#include <QString>

struct ClipTrack {
    QString name;
    bool enabled = true;
    double volume = 1.0;  // 0.0 - 2.0
};

struct Clip {
    QString path;       // absolute file path
    QString title;
    QDateTime created;
    qint64 size = 0;

    // probed media info
    double duration = 0;
    int width = 0;
    int height = 0;
    bool probed = false;

    // non-destructive edit state
    QList<ClipTrack> tracks;
    double trimStart = 0;
    double trimEnd = -1;  // -1 = end of clip

    bool favorite = false;
    QString shareUrl;
    QString shareId;

    QString fileName() const;
    double effectiveEnd() const { return trimEnd > 0 && trimEnd < duration ? trimEnd : duration; }
    double effectiveStart() const { return qBound(0.0, trimStart, qMax(0.0, duration)); }
    double editedDuration() const { return qMax(0.0, effectiveEnd() - effectiveStart()); }
    bool isTrimmed() const { return effectiveStart() > 0.01 || effectiveEnd() < duration - 0.01; }
    // True when the file itself does not sound/look like the edit, so a render
    // is needed before sharing (browsers only play the first audio track).
    bool needsRender() const;

    QJsonObject toJson() const;
    void applyJson(const QJsonObject &o);
};
Q_DECLARE_METATYPE(Clip)

QString formatDuration(double seconds);
QString formatSize(qint64 bytes);
