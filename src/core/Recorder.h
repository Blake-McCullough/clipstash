#pragma once
#include "core/Settings.h"

#include <QElapsedTimer>
#include <QList>
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
    // seconds == 0 saves the whole buffer; otherwise the nearest window
    // gpu-screen-recorder supports (10, 30, 60, 300, 600, 1800).
    // On refusal `why` says what is wrong (still starting, not running...).
    bool saveReplay(int seconds = 0, QString *why = nullptr);

signals:
    void stateChanged(Recorder::State state);
    // expectedSeconds: how long the clip should be; latencyMs: press to file.
    void clipSaved(const QString &path, int expectedSeconds, qint64 latencyMs);
    // A save was requested but no file arrived.
    void saveFailed(const QString &message);
    void failed(const QString &message);

private:
    void setState(State s);
    void onStdout();
    void onFinished(int code, QProcess::ExitStatus status);

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

    struct PendingSave {
        QElapsedTimer requested;
        int expectedSeconds;
    };
    QList<PendingSave> m_pending;
    QTimer m_saveWatch;
};
