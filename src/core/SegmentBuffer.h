#pragma once
#include "core/Settings.h"

#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QTimer>
#include <functional>

// The ffmpeg recording engine (used on Windows). ffmpeg captures the screen
// on the GPU (Desktop Duplication), encodes with the GPU's video encoder and
// writes 2-second pieces into a small rolling set of files. A clip is made by
// joining the pieces that cover the wanted time, without re-encoding.
class SegmentBuffer : public QObject {
    Q_OBJECT
public:
    struct Segment {
        QString file;
        double start = 0;  // stream seconds
        double end = 0;
    };

    explicit SegmentBuffer(QObject *parent = nullptr);

    static constexpr int kSegmentSeconds = 2;
    static QString defaultDir();

    // Encoder family to use for `s`, after resolving "auto" against what this
    // PC's ffmpeg can actually do (see EncoderProbe).
    static QString resolveEncoder(const Settings &s);
    // ffmpeg arguments. `audioInputs` are complete input arguments per audio
    // track (named pipes on Windows). `testVideo`, when set, replaces the
    // screen capture input (used by the tests).
    static QStringList buildArgs(const Settings &s, const QString &dir, const QList<QStringList> &audioInputs,
                                 const QStringList &trackNames, const QString &encoderFamily, bool haveScaleFilter,
                                 const QStringList &testVideo = {});

    // Starts watching `dir` for finished pieces (ffmpeg is started by the caller).
    void begin(const QString &dir, int keepSeconds);
    // MPEG-TS pieces can't hold track names, so clips get them when assembled.
    void setTrackNames(const QStringList &names) { m_trackNames = names; }
    void end();

    // Stream time ffmpeg had reached at the given moment (msecs since begin()).
    double streamTimeAt(qint64 wallMs) const;
    qint64 wallMs() const { return m_clock.isValid() ? m_clock.elapsed() : 0; }
    const QList<Segment> &segments() const { return m_segments; }

    using Done = std::function<void(bool ok, const QString &error, double delaySeconds)>;
    // Writes the pieces covering [at - length, at] (stream seconds) to `out`
    // once the piece containing `at` is finished. `delaySeconds` is how much of
    // the end of `out` comes after `at`, for the final trim.
    void assemble(double at, int length, const QString &out, Done done);

    // Full recordings: pieces are copied aside as they finish, so the rolling
    // buffer can keep overwriting old ones.
    void startCapture(double at);
    void stopCapture(double at, const QString &out, std::function<void(bool ok, const QString &error)> done);
    bool capturing() const { return m_capturing; }

signals:
    void segmentAdded(const Segment &segment);

private:
    void poll();
    void concat(const QStringList &files, double from, double to, const QString &out,
                std::function<void(bool ok, const QString &error)> done);
    void waitFor(double at, std::function<void(bool)> ready);

    QString m_dir;
    QStringList m_trackNames;
    QString m_listFile;
    QList<Segment> m_segments;  // newest last
    int m_keep = 40;
    QTimer m_poll;
    QElapsedTimer m_clock;
    double m_offset = 1e9;  // wall seconds minus stream seconds (smallest seen)

    bool m_capturing = false;
    double m_captureFrom = 0;
    QString m_captureDir;
    QList<Segment> m_captured;

    struct Waiter {
        double at;
        QElapsedTimer since;
        std::function<void(bool)> ready;
    };
    QList<Waiter> m_waiters;
};

// Finds which hardware encoders the bundled ffmpeg can use on this PC by
// encoding a single tiny frame with each. Runs once, in the background.
class EncoderProbe : public QObject {
    Q_OBJECT
public:
    static EncoderProbe *instance();
    bool done() const { return m_done; }
    QStringList available() const { return m_available; }  // "nvenc", "amf", "qsv", "cpu"
    bool haveScaleD3D11() const { return m_scaleD3D11; }
    void run(const QString &ffmpeg);

    static QString encoderName(const QString &family, const QString &codec);
    static QString label(const QString &family);

signals:
    void finished();

private:
    using QObject::QObject;
    bool m_done = false;
    bool m_running = false;
    QStringList m_available;
    bool m_scaleD3D11 = false;
};
