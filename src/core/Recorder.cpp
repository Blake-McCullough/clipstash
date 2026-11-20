#include "core/Recorder.h"

#include <QDateTime>
#include <QDir>
#include <QStandardPaths>
#include <QFileInfo>

#include <csignal>
#include <sys/prctl.h>

QString Recorder::logPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/recorder.log";
}

void Recorder::log(const QString &line)
{
    const QString path = logPath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    if (QFileInfo(path).size() > 1024 * 1024) {
        QFile::remove(path + ".1");
        QFile::rename(path, path + ".1");
    }
    QFile f(path);
    if (!f.open(QIODevice::Append | QIODevice::Text)) return;
    f.write((QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz") + "  " + line.trimmed() + "\n").toUtf8());
}

int Recorder::bufferedSeconds() const
{
    if (!isActive() || !m_since.isValid()) return 0;
    return int(qMin<qint64>(m_replaySeconds, m_since.elapsed() / 1000));
}

Recorder::Recorder(QObject *parent) : QObject(parent)
{
    // A save that never produces a file must not fail silently.
    m_saveWatch.setInterval(1000);
    connect(&m_saveWatch, &QTimer::timeout, this, [this] {
        if (m_pending.isEmpty()) {
            m_saveWatch.stop();
            return;
        }
        if (m_pending.first().requested.elapsed() < 30000) return;
        m_pending.removeFirst();
        QStringList lines = QString::fromUtf8(m_stderrTail).split('\n', Qt::SkipEmptyParts);
        const QString detail = lines.isEmpty() ? QString() : lines.last().trimmed();
        const QString msg = "The recorder did not write the clip"
                            + (detail.isEmpty() ? QString(".") : QString(": ") + detail);
        log("save failed: no file after 30 s" + (detail.isEmpty() ? QString() : " (" + detail + ")"));
        emit saveFailed(msg);
    });

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
        const QByteArray chunk = m_proc->readAllStandardError();
        for (const QByteArray &l : chunk.split('\n')) {
            const QString line = QString::fromUtf8(l).trimmed();
            if (line.isEmpty()) continue;
            log("gsr: " + line);
            // A save the recorder reports as failed: say so now, not after the timeout.
            const QString low = line.toLower();
            if (!m_pending.isEmpty() && low.contains("replay")
                && (low.contains("fail") || low.contains("error") || low.contains("unable"))) {
                m_pending.removeFirst();
                log("save failed: " + line);
                emit saveFailed("The recorder could not save the clip: " + line);
            }
        }
        m_stderrTail += chunk;
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
    m_args = buildArgs(s);
    m_replaySeconds = qBound(5, s.replaySeconds, 86400);
    m_pending.clear();
    log("start: " + s.recorderBinary + " " + m_args.join(' '));
    m_since.start();
    m_proc->start(s.recorderBinary, m_args);
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

bool Recorder::saveReplay(int seconds, QString *why)
{
    auto refuse = [&](const QString &reason) {
        if (why) *why = reason;
        log("save refused: " + reason);
        return false;
    };
    if (!m_proc || m_proc->state() != QProcess::Running || !isActive())
        return refuse("The recorder is not running.");
    // Right after start the recorder may not have its signal handlers yet
    // (an early signal would kill it) and the buffer is empty anyway.
    if (m_state == State::Starting || m_since.elapsed() < 2000)
        return refuse("The recorder is still starting. Try again in a moment.");

    int sig = SIGUSR1;
    int window = m_replaySeconds;
    if (seconds > 0) {
        static const int windows[] = {10, 30, 60, 300, 600, 1800};
        int best = 0;
        for (int i = 1; i < 6; ++i)
            if (qAbs(windows[i] - seconds) < qAbs(windows[best] - seconds)) best = i;
        sig = SIGRTMIN + 1 + best;
        window = qMin(window, windows[best]);
    }
    if (::kill(pid_t(m_proc->processId()), sig) != 0) return refuse("Could not signal the recorder.");

    PendingSave p;
    p.requested.start();
    p.expectedSeconds = qMin(window, bufferedSeconds());
    m_pending.append(p);
    m_saveWatch.start();
    log(QString("save requested: expect %1 s (buffer has %2 s)").arg(p.expectedSeconds).arg(bufferedSeconds()));
    return true;
}

void Recorder::onStdout()
{
    m_stdoutBuf += m_proc->readAllStandardOutput();
    int nl;
    while ((nl = m_stdoutBuf.indexOf('\n')) >= 0) {
        const QString line = QString::fromUtf8(m_stdoutBuf.left(nl)).trimmed();
        m_stdoutBuf.remove(0, nl + 1);
        // gpu-screen-recorder prints the saved file path on stdout.
        if (!line.startsWith('/') || !QFileInfo::exists(line)) continue;
        int expected = bufferedSeconds();
        qint64 latency = -1;
        if (!m_pending.isEmpty()) {
            const PendingSave p = m_pending.takeFirst();
            expected = p.expectedSeconds;
            latency = p.requested.elapsed();
        }
        log(QString("saved %1 after %2 ms (%3 MB)").arg(line).arg(latency)
                .arg(QFileInfo(line).size() / (1024 * 1024)));
        emit clipSaved(line, expected, latency);
    }
}

void Recorder::onFinished(int code, QProcess::ExitStatus status)
{
    log(QString("exited: code %1%2").arg(code).arg(status == QProcess::CrashExit ? " (crashed)" : ""));
    m_pending.clear();
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
