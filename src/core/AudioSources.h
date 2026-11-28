#pragma once
#include "core/Settings.h"

#include <QList>
#include <QObject>
#include <QStringList>
#include <memory>

// Audio for the ffmpeg engine. On Windows each track is captured with WASAPI
// (desktop sound via loopback, the microphone directly) and streamed to
// ffmpeg through a named pipe, padded with silence so ffmpeg never waits.
class AudioSources : public QObject {
    Q_OBJECT
public:
    explicit AudioSources(QObject *parent = nullptr);
    ~AudioSources() override;

    // ffmpeg input arguments, one entry per recorded track, in track order.
    // `names` receives the track names that go with them.
    QList<QStringList> open(const Settings &s, QStringList *names = nullptr);
    void close();

    // Whether "app:Name" sources can be recorded on this platform.
    static bool supportsPerApp();
    // Sources offered in the settings: (value, description).
    static QList<QPair<QString, QString>> choices();

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
