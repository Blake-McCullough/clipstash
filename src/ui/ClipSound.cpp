#include "ui/ClipSound.h"

#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QtEndian>

#include <mpv/client.h>

#include <clocale>
#include <cmath>

ClipSound::ClipSound(QObject *parent) : QObject(parent) {}

ClipSound::~ClipSound()
{
    if (m_mpv) mpv_terminate_destroy(m_mpv);
}

// Two soft rising notes (A5 then E6), about 0.4 s, 48 kHz mono 16-bit.
QByteArray ClipSound::chimeWav()
{
    const int rate = 48000;
    const int total = int(rate * 0.42);
    QByteArray pcm(total * 2, '\0');
    auto *out = reinterpret_cast<qint16 *>(pcm.data());

    struct Note { double freq, start, length; };
    const Note notes[] = {{880.0, 0.0, 0.30}, {1318.51, 0.11, 0.31}};
    const double twoPi = 6.283185307179586;

    for (int i = 0; i < total; ++i) {
        const double t = double(i) / rate;
        double v = 0;
        for (const Note &n : notes) {
            const double local = t - n.start;
            if (local < 0 || local > n.length) continue;
            const double attack = qMin(1.0, local / 0.004);              // no click at the start
            const double release = qMin(1.0, (n.length - local) / 0.02); // nor at the end
            const double env = attack * release * std::exp(-local * 11.0);
            v += env * (std::sin(twoPi * n.freq * local) + 0.25 * std::sin(twoPi * 2 * n.freq * local));
        }
        out[i] = qToLittleEndian(qint16(qBound(-1.0, v * 0.5, 1.0) * 32000));
    }

    QByteArray wav;
    auto u32 = [&](quint32 x) { x = qToLittleEndian(x); wav.append(reinterpret_cast<const char *>(&x), 4); };
    auto u16 = [&](quint16 x) { x = qToLittleEndian(x); wav.append(reinterpret_cast<const char *>(&x), 2); };
    wav.append("RIFF");
    u32(36 + pcm.size());
    wav.append("WAVEfmt ");
    u32(16);
    u16(1);         // PCM
    u16(1);         // mono
    u32(rate);
    u32(rate * 2);  // byte rate
    u16(2);         // block align
    u16(16);        // bits per sample
    wav.append("data");
    u32(pcm.size());
    wav.append(pcm);
    return wav;
}

bool ClipSound::ensureReady()
{
    if (m_file.isEmpty()) {
        const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
        QDir().mkpath(dir);
        const QString path = dir + "/clip-saved.wav";
        const QByteArray wav = chimeWav();
        QFile f(path);
        if (f.size() != wav.size()) {
            if (!f.open(QIODevice::WriteOnly) || f.write(wav) != wav.size()) return false;
            f.close();
        }
        m_file = path;
    }
    if (!m_mpv) {
        std::setlocale(LC_NUMERIC, "C");  // required by libmpv
        m_mpv = mpv_create();
        if (!m_mpv) return false;
        mpv_set_option_string(m_mpv, "config", "no");
        mpv_set_option_string(m_mpv, "terminal", "no");
        mpv_set_option_string(m_mpv, "vid", "no");
        mpv_set_option_string(m_mpv, "vo", "null");
        mpv_set_option_string(m_mpv, "idle", "yes");
        mpv_set_option_string(m_mpv, "audio-client-name", "Clipstash");
        mpv_set_option_string(m_mpv, "input-default-bindings", "no");
        if (mpv_initialize(m_mpv) < 0) {
            mpv_destroy(m_mpv);
            m_mpv = nullptr;
            return false;
        }
    }
    return true;
}

void ClipSound::play(int volumePercent)
{
    if (!ensureReady()) return;
    double volume = qBound(0, volumePercent, 100);
    mpv_set_property_async(m_mpv, 0, "volume", MPV_FORMAT_DOUBLE, &volume);
    const QByteArray path = m_file.toUtf8();
    const char *cmd[] = {"loadfile", path.constData(), nullptr};
    mpv_command_async(m_mpv, 0, cmd);
}
