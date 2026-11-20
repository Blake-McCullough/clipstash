#pragma once
#include "core/Settings.h"

#include <QDateTime>
#include <QElapsedTimer>
#include <QList>
#include <QPair>
#include <QObject>
#include <QProcess>
#include <QTimer>

// Owns the background gpu-screen-recorder process running in replay mode.
class Recorder : public QObject {
    Q_OBJECT
public:
    enum class State { Stopped, Starting, Running, Failed };
    Q_ENUM(State)

    explicit Recorder(QObject *parent = nullptr);
    ~Recorder() override;

    static QStringList buildArgs(const Settings &s);
    // The recorder keeps this much more than the clip length, so a save that
    // has to wait for the previous one can still be cut to end at the moment
    // the button was pressed.
    static constexpr int kHeadroomSeconds = 20;
    // Raw saves land here (hidden) and are moved or cut into the library.
    static QString incomingDir(const QString &libraryDir) { return libraryDir + "/.clipstash/incoming"; }
    // Full (start/stop) recordings are written here until they are finished.
    static QString recordingDir(const QString &libraryDir) { return libraryDir + "/.clipstash/recording"; }

    bool isFullRecording() const { return m_fullRecording; }
    qint64 fullRecordingMs() const { return m_fullRecording ? m_fullSince.elapsed() : 0; }
    // Part of a saved file of `fileSeconds` to keep so the clip is `length`
    // long and ends `delaySeconds` before the end of the file.
    static QPair<double, double> cutWindow(double fileSeconds, double delaySeconds, int length);

    State state() const { return m_state; }
    bool isActive() const { return m_state == State::Starting || m_state == State::Running; }
    QString lastError() const { return m_lastError; }
    // Arguments of the running recorder (empty when not running).
    QStringList runningArgs() const { return isActive() ? m_args : QStringList{}; }
    // How much footage the buffer can hold right now: the replay length, or
    // less if the recorder started only recently.
    int bufferedSeconds() const;

    // Appends a timestamped line to the recorder log (kept for troubleshooting).
    static void log(const QString &line);
    static QString logPath();

public slots:
    void start(const Settings &s);
    void stop();
    void restart(const Settings &s);
    // Saves the last `seconds` (0 = the configured clip length) as of now.
    // Presses are queued, so several quick presses each get their own full
    // clip. On refusal `why` says what is wrong (still starting...).
    bool saveReplay(int seconds = 0, QString *why = nullptr);
    // Starts or stops a normal recording alongside the replay buffer.
    bool toggleFullRecording(QString *why = nullptr);

signals:
    void stateChanged(Recorder::State state);
    // expectedSeconds: how long the clip should be; latencyMs: press to file.
    void clipSaved(const QString &path, int expectedSeconds, qint64 latencyMs);
    // A save was requested but no file arrived.
    void saveFailed(const QString &message);
    void fullRecordingChanged(bool recording);
    // A full recording was finished and moved into the clip folder.
    void recordingSaved(const QString &path);
    void failed(const QString &message);

private:
    void setState(State s);
    void onStdout();
    void onFinished(int code, QProcess::ExitStatus status);
    void dispatch();
    void failCurrent(const QString &message);
    void recoverIncoming();
    void collectRecordings(const QString &announcedPath = {});
    void setFullRecording(bool on);

    struct Request {
        QElapsedTimer pressed;
        QDateTime pressedAt;
        int length = 0;          // seconds wanted
        int expected = 0;        // seconds the buffer actually held at the press
        bool exact = false;      // signalled immediately with a matching window
        qint64 delayMs = 0;      // press to signal
        QElapsedTimer dispatched;
    };
    void finalize(const QString &raw, const Request &r);

    QProcess *m_proc = nullptr;
    State m_state = State::Stopped;
    QString m_lastError;
    QByteArray m_stdoutBuf;
    QByteArray m_stderrTail;
    QTimer m_runningTimer;
    bool m_stopping = false;
    bool m_restartPending = false;
    Settings m_restartSettings;
    QStringList m_args;
    int m_replaySeconds = 0;
    QElapsedTimer m_since;

    QString m_libraryDir;
    QList<Request> m_queue;
    Request m_current;
    bool m_saving = false;
    bool m_fullRecording = false;
    QElapsedTimer m_fullSince;
    QDateTime m_fullStartedAt;
    QTimer m_recordingSweep;
    QTimer m_saveWatch;
};
