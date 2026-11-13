#include "core/Recorder.h"

#include <QDir>
#include <QFileInfo>

#include <csignal>
#include <sys/prctl.h>

Recorder::Recorder(QObject *parent) : QObject(parent)
{
    m_runningTimer.setSingleShot(true);
    m_runningTimer.setInterval(1500);
    connect(&m_runningTimer, &QTimer::timeout, this, [this] {
        if (m_proc && m_proc->state() == QProcess::Running && m_state == State::Starting)
            setState(State::Running);
    });
}

Recorder::~Recorder()
{
    if (m_proc) {
        m_proc->disconnect(this);
        if (m_proc->state() != QProcess::NotRunning) {
            ::kill(pid_t(m_proc->processId()), SIGINT);
            if (!m_proc->waitForFinished(2000)) m_proc->kill();
            m_proc->waitForFinished(500);
        }
    }
}

QStringList Recorder::buildArgs(const Settings &s)
{
    QStringList a;
    a << "-w" << (s.captureSource.isEmpty() ? QString("screen") : s.captureSource);
    a << "-c" << "mp4";
    a << "-f" << QString::number(qBound(1, s.fps, 500));
    a << "-r" << QString::number(qBound(5, s.replaySeconds, 86400));
    a << "-k" << s.codec;
    a << "-ac" << s.audioCodec;
    if (s.bitrateMode == "cbr") {
        a << "-bm" << "cbr" << "-q" << QString::number(qMax(500, s.bitrateKbps));
    } else {
        a << "-bm" << (s.bitrateMode == "vbr" ? "vbr" : "qp") << "-q" << s.quality;
    }
    a << "-fm" << s.frameMode;
    a << "-cursor" << (s.recordCursor ? "yes" : "no");
    a << "-replay-storage" << s.replayStorage;
    if (!s.resolution.trimmed().isEmpty()) a << "-s" << s.resolution.trimmed();
    if (s.captureSource == "portal")
        a << "-restore-portal-session" << (s.restorePortalSession ? "yes" : "no");
    const QList<AudioTrackCfg> tracks = s.effectiveTracks();
    for (const AudioTrackCfg &t : tracks) {
        const QString src = t.sources.trimmed();
        if (!src.isEmpty()) a << "-a" << src;
    }
    a << "-v" << "no";
    if (!s.extraArgs.trimmed().isEmpty()) a << QProcess::splitCommand(s.extraArgs);
    a << "-o" << s.libraryDir;
    return a;
}

void Recorder::setState(State s)
{
    if (m_state == s) return;
    m_state = s;
    emit stateChanged(s);
}

void Recorder::start(const Settings &s)
{
    if (m_proc && m_proc->state() != QProcess::NotRunning) return;
    if (m_proc) {
        m_proc->disconnect(this);
        m_proc->deleteLater();
        m_proc = nullptr;
    }
    m_lastError.clear();
    m_stderrTail.clear();
    m_stdoutBuf.clear();
    m_stopping = false;

    if (!QDir().mkpath(s.libraryDir)) {
        m_lastError = QString("Cannot create clip folder %1").arg(s.libraryDir);
        setState(State::Failed);
        emit failed(m_lastError);
        return;
    }

    m_proc = new QProcess(this);
    connect(m_proc, &QProcess::readyReadStandardOutput, this, &Recorder::onStdout);
    connect(m_proc, &QProcess::readyReadStandardError, this, [this] {
        m_stderrTail += m_proc->readAllStandardError();
        if (m_stderrTail.size() > 4096) m_stderrTail = m_stderrTail.right(4096);
    });
    connect(m_proc, &QProcess::finished, this, &Recorder::onFinished);
    connect(m_proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart) return;
        m_lastError = QString("Could not start \"%1\". Is gpu-screen-recorder installed?")
                          .arg(m_proc->program());
        setState(State::Failed);
        emit failed(m_lastError);
    });

    // If Clipstash is killed or crashes, take the recorder down with it
    // instead of leaving an orphan capturing in the background.
    m_proc->setChildProcessModifier([] { ::prctl(PR_SET_PDEATHSIG, SIGTERM); });
    setState(State::Starting);
    m_proc->start(s.recorderBinary, buildArgs(s));
    m_runningTimer.start();
}

void Recorder::stop()
{
    m_runningTimer.stop();
    if (!m_proc || m_proc->state() == QProcess::NotRunning) {
        m_restartPending = false;
        setState(State::Stopped);
        return;
    }
    m_stopping = true;
    ::kill(pid_t(m_proc->processId()), SIGINT);
    QProcess *p = m_proc;
    QTimer::singleShot(3000, p, [p] {
        if (p->state() != QProcess::NotRunning) p->kill();
    });
}

void Recorder::restart(const Settings &s)
{
    if (!m_proc || m_proc->state() == QProcess::NotRunning) {
        setState(State::Stopped);
        start(s);
        return;
    }
    m_restartSettings = s;
    m_restartPending = true;
    stop();
}

bool Recorder::saveReplay(int seconds)
{
    if (!m_proc || m_proc->state() != QProcess::Running) return false;
    int sig = SIGUSR1;
    if (seconds > 0) {
        static const int windows[] = {10, 30, 60, 300, 600, 1800};
        int best = 0;
        for (int i = 1; i < 6; ++i)
            if (qAbs(windows[i] - seconds) < qAbs(windows[best] - seconds)) best = i;
        sig = SIGRTMIN + 1 + best;
    }
    return ::kill(pid_t(m_proc->processId()), sig) == 0;
}

void Recorder::onStdout()
{
    m_stdoutBuf += m_proc->readAllStandardOutput();
    int nl;
    while ((nl = m_stdoutBuf.indexOf('\n')) >= 0) {
        const QString line = QString::fromUtf8(m_stdoutBuf.left(nl)).trimmed();
        m_stdoutBuf.remove(0, nl + 1);
        // gpu-screen-recorder prints the saved file path on stdout.
        if (line.startsWith('/') && QFileInfo::exists(line)) emit clipSaved(line);
    }
}

void Recorder::onFinished(int code, QProcess::ExitStatus status)
{
    m_runningTimer.stop();
    if (m_stopping) {
        setState(State::Stopped);
        if (m_restartPending) {
            m_restartPending = false;
            start(m_restartSettings);
        }
        return;
    }
    // Exited on its own: surface the last meaningful stderr lines.
    QStringList lines = QString::fromUtf8(m_stderrTail).split('\n', Qt::SkipEmptyParts);
    QStringList tail;
    for (int i = lines.size() - 1; i >= 0 && tail.size() < 3; --i) {
        const QString l = lines[i].trimmed();
        if (!l.isEmpty()) tail.prepend(l);
    }
    m_lastError = tail.isEmpty()
        ? QString("gpu-screen-recorder exited (%1)").arg(status == QProcess::CrashExit ? "crashed" : QString::number(code))
        : tail.join('\n');
    setState(State::Failed);
    emit failed(m_lastError);
}
