#pragma once
#include "core/Settings.h"

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

public slots:
    void start(const Settings &s);
    void stop();
    void restart(const Settings &s);
    // seconds == 0 saves the whole buffer; otherwise the nearest window
    // gpu-screen-recorder supports (10, 30, 60, 300, 600, 1800).
    bool saveReplay(int seconds = 0);

signals:
    void stateChanged(Recorder::State state);
    void clipSaved(const QString &path);
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
};
