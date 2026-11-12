#pragma once
#include <QObject>
#include <QString>

struct mpv_handle;

// The short "clip saved" chime. The sound is synthesised once into the cache
// folder and played through a tiny audio-only mpv instance, so no extra
// audio library or bundled sound file is needed.
class ClipSound : public QObject {
    Q_OBJECT
public:
    explicit ClipSound(QObject *parent = nullptr);
    ~ClipSound() override;

    void play(int volumePercent);

    static QByteArray chimeWav();

private:
    bool ensureReady();

    mpv_handle *m_mpv = nullptr;
    QString m_file;
};
