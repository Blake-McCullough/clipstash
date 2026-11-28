#include "core/Recorder.h"

#include <QSet>

#include "core/AudioSources.h"
#include "core/MediaTools.h"
#include "core/Platform.h"
#include "core/SegmentBuffer.h"

#include <QDateTime>
#include <QDir>
#include <QPointer>
#include <QStandardPaths>
#include <QFileInfo>

#include <csignal>
#ifdef Q_OS_LINUX
#include <sys/prctl.h>
#endif
#include <QUuid>

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
    m_seg = new SegmentBuffer(this);
    m_audio = new AudioSources(this);
    // A save that never produces a file must not fail silently.
    m_saveWatch.setInterval(1000);
    connect(&m_saveWatch, &QTimer::timeout, this, [this] {
        if (!m_saving) {
            m_saveWatch.stop();
            return;
        }
        if (m_current.dispatched.elapsed() < 30000) return;
        QStringList lines = QString::fromUtf8(m_stderrTail).split('\n', Qt::SkipEmptyParts);
        const QString detail = lines.isEmpty() ? QString() : lines.last().trimmed();
        failCurrent("The recorder did not write the clip" + (detail.isEmpty() ? QString(".") : QString(": ") + detail));
    });

    // If the recorder does not announce a finished recording, pick it up anyway.
    m_recordingSweep.setSingleShot(true);
    m_recordingSweep.setInterval(15000);
    connect(&m_recordingSweep, &QTimer::timeout, this, [this] { collectRecordings(); });

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
            if (m_ffmpeg) m_proc->write("q\n");
#ifndef Q_OS_WIN
            else ::kill(pid_t(m_proc->processId()), SIGINT);
#endif
            if (!m_proc->waitForFinished(3000)) m_proc->kill();
            m_proc->waitForFinished(500);
        }
    }
    if (m_audio) m_audio->close();
}

QStringList Recorder::buildArgs(const Settings &s)
{
    QStringList a;
    a << "-w" << (s.captureSource.isEmpty() ? QString("screen") : s.captureSource);
    a << "-c" << "mp4";
    a << "-f" << QString::number(qBound(1, s.lowResource ? qMin(s.fps, 30) : s.fps, 500));
    a << "-r" << QString::number(qBound(5, s.replaySeconds, 86400) + kHeadroomSeconds);
    a << "-k" << s.codec;
    a << "-ac" << s.audioCodec;
    if (s.lowResource) {
        a << "-bm" << "cbr" << "-q" << "8000";
    } else if (s.bitrateMode == "cbr") {
        a << "-bm" << "cbr" << "-q" << QString::number(qMax(500, s.bitrateKbps));
    } else {
        a << "-bm" << (s.bitrateMode == "vbr" ? "vbr" : "qp") << "-q" << s.quality;
    }
    a << "-fm" << s.frameMode;
    a << "-cursor" << (s.recordCursor ? "yes" : "no");
    a << "-replay-storage" << s.replayStorage;
    QString size = s.resolution.trimmed();
    if (s.lowResource && (size.isEmpty() || size.section('x', 1).toInt() > 1080)) size = "1920x1080";
    if (!size.isEmpty()) a << "-s" << size;
    if (s.videoEncoder == "cpu") a << "-encoder" << "cpu";
    if (s.captureSource == "portal")
        a << "-restore-portal-session" << (s.restorePortalSession ? "yes" : "no");
    const QList<AudioTrackCfg> tracks = s.effectiveTracks();
    for (const AudioTrackCfg &t : tracks) {
        const QString src = t.sources.trimmed();
        if (!src.isEmpty()) a << "-a" << src;
    }
    a << "-v" << "no";
    if (!s.extraArgs.trimmed().isEmpty()) a << QProcess::splitCommand(s.extraArgs);
    a << "-ro" << recordingDir(s.libraryDir);
    a << "-o" << incomingDir(s.libraryDir);
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
    if (m_waitingForProbe) return;
    if (m_proc) {
        m_proc->disconnect(this);
        m_proc->deleteLater();
        m_proc = nullptr;
    }
    m_lastError.clear();
    m_stderrTail.clear();
    m_stdoutBuf.clear();
    m_stopping = false;

    if (!QDir().mkpath(s.libraryDir) || !QDir().mkpath(incomingDir(s.libraryDir))
        || !QDir().mkpath(recordingDir(s.libraryDir))) {
        m_lastError = QString("Cannot create clip folder %1").arg(s.libraryDir);
        setState(State::Failed);
        emit failed(m_lastError);
        return;
    }

    // The ffmpeg engine first needs to know which encoders this PC has.
    if (s.recorderEngine == "ffmpeg" && !EncoderProbe::instance()->done()) {
        m_waitingForProbe = true;
        setState(State::Starting);
        auto *probe = EncoderProbe::instance();
        connect(probe, &EncoderProbe::finished, this, [this, s] {
            m_waitingForProbe = false;
            if (m_state == State::Starting) startProcess(s);
        }, Qt::SingleShotConnection);
        probe->run("ffmpeg");
        return;
    }
    startProcess(s);
}

void Recorder::startProcess(const Settings &s)
{
    m_ffmpeg = s.recorderEngine == "ffmpeg";
    m_started = s;
    m_proc = new QProcess(this);
    connect(m_proc, &QProcess::readyReadStandardOutput, this, &Recorder::onStdout);
    connect(m_proc, &QProcess::readyReadStandardError, this, [this] {
        const QByteArray chunk = m_proc->readAllStandardError();
        for (const QByteArray &l : chunk.split('\n')) {
            const QString line = QString::fromUtf8(l).trimmed();
            if (line.isEmpty() || line == m_lastStderr) continue;  // no repeated spam in the log
            m_lastStderr = line;
            log((m_ffmpeg ? "ffmpeg: " : "gsr: ") + line);
            // A save the recorder reports as failed: say so now, not after the timeout.
            const QString low = line.toLower();
            if (!m_ffmpeg && m_saving && low.contains("replay")
                && (low.contains("fail") || low.contains("error") || low.contains("unable")))
                failCurrent("The recorder could not save the clip: " + line);
        }
        m_stderrTail += chunk;
        if (m_stderrTail.size() > 4096) m_stderrTail = m_stderrTail.right(4096);
    });
    connect(m_proc, &QProcess::finished, this, &Recorder::onFinished);
    connect(m_proc, &QProcess::errorOccurred, this, [this](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart) return;
        m_lastError = m_ffmpeg ? QString("Could not start ffmpeg. Reinstall Clipstash to restore it.")
                               : QString("Could not start \"%1\". Is gpu-screen-recorder installed?").arg(m_proc->program());
        if (m_audio) m_audio->close();
        setState(State::Failed);
        emit failed(m_lastError);
    });
    connect(m_proc, &QProcess::started, this, [this] { Platform::tieToApp(m_proc, m_started.lowResource); });

#ifdef Q_OS_LINUX
    // If Clipstash is killed or crashes, take the recorder down with it
    // instead of leaving an orphan capturing in the background.
    m_proc->setChildProcessModifier([] { ::prctl(PR_SET_PDEATHSIG, SIGTERM); });
#endif
    setState(State::Starting);
    m_replaySeconds = qBound(5, s.replaySeconds, 86400);
    m_libraryDir = s.libraryDir;
    m_queue.clear();
    m_saving = false;
    setFullRecording(false);
    recoverIncoming();
    collectRecordings();

    QString program = s.recorderBinary;
    m_trackNames.clear();
    if (m_ffmpeg) {
        const QString dir = SegmentBuffer::defaultDir();
        QDir(dir).removeRecursively();
        QDir().mkpath(dir);
        const QString family = SegmentBuffer::resolveEncoder(s);
        const QList<QStringList> audio = m_audio->open(s, &m_trackNames);
        const QStringList testVideo = QProcess::splitCommand(qEnvironmentVariable("CLIPSTASH_TEST_VIDEO"));
        m_args = SegmentBuffer::buildArgs(s, dir, audio, m_trackNames, family,
                                          EncoderProbe::instance()->haveScaleD3D11(), testVideo);
        program = "ffmpeg";
        m_engineText = EncoderProbe::label(family) + (s.lowResource ? ", low resource mode" : "");
        m_seg->setTrackNames(m_trackNames);
        m_seg->begin(dir, m_replaySeconds + kHeadroomSeconds + 10);
    } else {
        m_args = buildArgs(s);
        const QList<AudioTrackCfg> tracks = s.effectiveTracks();
        for (const AudioTrackCfg &t : tracks)
            if (!t.sources.trimmed().isEmpty()) m_trackNames << t.name;
        m_engineText = QString("gpu-screen-recorder") + (s.lowResource ? ", low resource mode" : "");
    }
    log("start: " + program + " " + m_args.join(' '));
    m_since.start();
    m_proc->start(program, m_args);
    m_runningTimer.start();
}

void Recorder::stop()
{
    m_runningTimer.stop();
    if (m_waitingForProbe) {
        m_waitingForProbe = false;
        setState(State::Stopped);
        return;
    }
    if (!m_proc || m_proc->state() == QProcess::NotRunning) {
        m_restartPending = false;
        setState(State::Stopped);
        return;
    }
    m_stopping = true;
    if (m_ffmpeg) m_proc->write("q\n");  // ffmpeg finishes the current piece and exits
#ifndef Q_OS_WIN
    else ::kill(pid_t(m_proc->processId()), SIGINT);
#endif
    QProcess *p = m_proc;
    QTimer::singleShot(m_ffmpeg ? 5000 : 3000, p, [p] {
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

QPair<double, double> Recorder::cutWindow(double fileSeconds, double delaySeconds, int length)
{
    double end = fileSeconds - qMax(0.0, delaySeconds);
    if (end <= 0) end = fileSeconds;
    return {qMax(0.0, end - length), end};
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
    if (m_queue.size() >= 20) return refuse("Too many clips are still being saved.");

    Request r;
    r.pressed.start();
    r.pressedAt = QDateTime::currentDateTime();
    r.length = seconds > 0 ? qMin(seconds, m_replaySeconds) : m_replaySeconds;
    r.expected = qMin(r.length, bufferedSeconds());
    r.pressWallMs = m_seg->wallMs();
    m_queue.append(r);
    log(QString("clip requested: %1 s (buffer has %2 s, %3 waiting)")
            .arg(r.length).arg(bufferedSeconds()).arg(m_queue.size() - 1 + (m_saving ? 1 : 0)));
    if (!m_saving) dispatch();
    return true;
}

void Recorder::dispatch()
{
    if (m_ffmpeg) {
        dispatchFfmpeg();
        return;
    }
#ifndef Q_OS_WIN
    while (!m_saving && !m_queue.isEmpty() && m_proc && m_proc->state() == QProcess::Running) {
        Request r = m_queue.takeFirst();
        r.delayMs = r.pressed.elapsed();
        // Straight away and a length the recorder has a signal for: that file
        // is already the clip. Otherwise save everything and cut it to the
        // minute before the press.
        static const int windows[] = {10, 30, 60, 300, 600, 1800};
        int sig = SIGUSR1;
        r.exact = false;
        if (r.delayMs < 400) {
            for (int i = 0; i < 6; ++i) {
                if (windows[i] != r.length) continue;
                sig = SIGRTMIN + 1 + i;
                r.exact = true;
            }
        }
        if (::kill(pid_t(m_proc->processId()), sig) != 0) {
            log("save failed: could not signal the recorder");
            emit saveFailed("Could not signal the recorder.");
            continue;
        }
        r.dispatched.start();
        m_current = r;
        m_saving = true;
        m_saveWatch.start();
        log(QString("save sent: %1 s clip, %2 ms after the press%3")
                .arg(r.length).arg(r.delayMs).arg(r.exact ? "" : ", will cut"));
    }
#endif
}

void Recorder::dispatchFfmpeg()
{
    if (m_saving || m_queue.isEmpty() || !m_proc || m_proc->state() != QProcess::Running) return;
    Request r = m_queue.takeFirst();
    r.dispatched.start();
    m_current = r;
    m_saving = true;
    m_saveWatch.start();
    // The moment of the press in the recording's own time. Join the pieces
    // around it (a few seconds spare at the start) and trim to the press.
    const double at = m_seg->streamTimeAt(r.pressWallMs);
    const QString raw = incomingDir(m_libraryDir) + "/raw-" + QUuid::createUuid().toString(QUuid::Id128) + ".mp4";
    log(QString("save: %1 s clip ending at %2 s").arg(r.length).arg(at, 0, 'f', 2));
    QPointer<Recorder> self(this);
    m_seg->assemble(at, r.length + SegmentBuffer::kSegmentSeconds, raw,
                    [self, r, raw](bool ok, const QString &error, double delay) {
        if (!self || !self->m_saving) return;
        if (!ok) {
            self->failCurrent(error);
            return;
        }
        Request done = r;
        done.exact = false;
        done.delayMs = qint64(delay * 1000);
        self->m_saving = false;
        log(QString("joined pieces after %1 ms").arg(r.dispatched.elapsed()));
        self->finalize(raw, done);
        self->dispatch();
    });
}

void Recorder::failCurrent(const QString &message)
{
    if (!m_saving) return;
    m_saving = false;
    log("save failed: " + message);
    emit saveFailed(message);
    dispatch();
}

// Names handed to clips that are still being cut, so two presses in the same
// second never end up writing the same file.
static QSet<QString> g_reservedNames;

static QString uniqueClipPath(const QString &dir, const QDateTime &at)
{
    const QString base = dir + "/Replay_" + at.toString("yyyy-MM-dd_HH-mm-ss");
    QString out = base + ".mp4";
    for (int n = 2; QFileInfo::exists(out) || g_reservedNames.contains(out); ++n)
        out = QString("%1_%2.mp4").arg(base).arg(n);
    return out;
}

static bool moveFile(const QString &from, const QString &to)
{
    if (QFile::rename(from, to)) return true;
    // Different filesystem (should not happen, but never lose a clip).
    if (!QFile::copy(from, to)) return false;
    QFile::remove(from);
    return true;
}

static void stampTime(const QString &path, const QDateTime &at)
{
    QFile f(path);
    if (f.open(QIODevice::ReadWrite)) f.setFileTime(at, QFileDevice::FileModificationTime);
}

void Recorder::recoverIncoming()
{
    // Saves left behind by a crash: hand them over untouched.
    const QDir in(incomingDir(m_libraryDir));
    const QFileInfoList files = in.entryInfoList({"*.mp4"}, QDir::Files);
    for (const QFileInfo &fi : files) {
        if (fi.fileName().startsWith("cut-")) {
            QFile::remove(fi.absoluteFilePath());
            continue;
        }
        const QString to = uniqueClipPath(m_libraryDir, fi.lastModified());
        if (moveFile(fi.absoluteFilePath(), to)) log("recovered " + to);
    }
}

void Recorder::finalize(const QString &raw, const Request &r)
{
    const QString final = uniqueClipPath(m_libraryDir, r.pressedAt);
    g_reservedNames.insert(final);
    auto deliver = [this, r, final](const QString &how) {
        g_reservedNames.remove(final);
        stampTime(final, r.pressedAt);
        log(QString("saved %1 (%2, %3 ms after the press, %4 MB)").arg(final, how).arg(r.pressed.elapsed())
                .arg(QFileInfo(final).size() / (1024 * 1024)));
        emit clipSaved(final, r.expected, r.pressed.elapsed());
    };

    if (r.exact) {
        if (moveFile(raw, final)) deliver("as recorded");
        else {
            g_reservedNames.remove(final);
            emit saveFailed("Could not move the clip into the clip folder.");
        }
        return;
    }

    QPointer<Recorder> self(this);
    MediaTools::probe(raw, [self, raw, r, final, deliver](const ProbeResult &p) {
        if (!self) {
            g_reservedNames.remove(final);
            return;
        }
        const double delay = r.delayMs / 1000.0;
        if (!p.ok) {
            if (moveFile(raw, final)) deliver("uncut, could not read it");
            return;
        }
        const auto [start, end] = cutWindow(p.duration, delay, r.length);
        if (start <= 0.05 && end >= p.duration - 0.05) {
            if (moveFile(raw, final)) deliver("as recorded");
            return;
        }
        const QString tmp = QFileInfo(raw).absolutePath() + "/cut-" + QFileInfo(raw).fileName();
        auto *ff = new QProcess(self);
        QObject::connect(ff, &QProcess::finished, self, [=](int code, QProcess::ExitStatus st) {
            ff->deleteLater();
            if (st == QProcess::NormalExit && code == 0 && QFileInfo(tmp).size() > 0 && moveFile(tmp, final)) {
                QFile::remove(raw);
                deliver(QString("cut %1-%2 s of %3 s").arg(start, 0, 'f', 1).arg(end, 0, 'f', 1).arg(p.duration, 0, 'f', 1));
            } else {
                QFile::remove(tmp);
                if (moveFile(raw, final)) deliver("uncut, cutting failed");
            }
        });
        // Stream copy: no quality loss, and only as slow as copying the file.
        ff->start("ffmpeg", {"-y", "-v", "error", "-ss", QString::number(start, 'f', 3),
                             "-to", QString::number(end, 'f', 3), "-i", raw, "-map", "0", "-c", "copy",
                             "-avoid_negative_ts", "make_zero", "-movflags", "+faststart", tmp});
    });
}

void Recorder::setFullRecording(bool on)
{
    if (m_fullRecording == on) return;
    m_fullRecording = on;
    if (on) {
        m_fullSince.start();
        m_fullStartedAt = QDateTime::currentDateTime();
    }
    emit fullRecordingChanged(on);
}

bool Recorder::toggleFullRecording(QString *why)
{
    auto refuse = [&](const QString &reason) {
        if (why) *why = reason;
        log("recording refused: " + reason);
        return false;
    };
    if (!m_proc || m_proc->state() != QProcess::Running || !isActive())
        return refuse("The recorder is not running.");
    if (m_state == State::Starting || m_since.elapsed() < 2000)
        return refuse("The recorder is still starting. Try again in a moment.");
    const bool starting = !m_fullRecording;
    if (m_ffmpeg) {
        const double at = m_seg->streamTimeAt(m_seg->wallMs());
        if (starting) {
            m_seg->startCapture(at);
            setFullRecording(true);
            log("full recording started");
            return true;
        }
        log(QString("full recording stopped after %1 s").arg(m_fullSince.elapsed() / 1000));
        const QDateTime startedAt = m_fullStartedAt;
        setFullRecording(false);
        const QString out = recordingDir(m_libraryDir) + "/Recording_"
                            + startedAt.toString("yyyy-MM-dd_HH-mm-ss") + ".mp4.part";
        QPointer<Recorder> self(this);
        m_seg->stopCapture(at, out, [self, out](bool ok, const QString &error) {
            if (!self) return;
            if (!ok) {
                log("recording failed: " + error);
                emit self->saveFailed("The recording could not be saved: " + error);
                return;
            }
            const QString final = out.chopped(5);  // drop ".part"
            QFile::rename(out, final);
            self->collectRecordings(final);
        });
        return true;
    }
#ifndef Q_OS_WIN
    if (::kill(pid_t(m_proc->processId()), SIGRTMIN) != 0) return refuse("Could not signal the recorder.");
#endif
    if (starting) {
        m_recordingSweep.stop();
        setFullRecording(true);
        log("full recording started");
    } else {
        log(QString("full recording stopped after %1 s").arg(m_fullSince.elapsed() / 1000));
        setFullRecording(false);
        m_recordingSweep.start();
    }
    return true;
}

void Recorder::collectRecordings(const QString &announcedPath)
{
    if (m_fullRecording || m_libraryDir.isEmpty()) return;  // still being written
    QStringList files;
    if (!announcedPath.isEmpty()) files << announcedPath;
    const QFileInfoList found = QDir(recordingDir(m_libraryDir)).entryInfoList(
        {"*.mp4", "*.mkv", "*.mov", "*.webm", "*.flv"}, QDir::Files, QDir::Time | QDir::Reversed);
    for (const QFileInfo &fi : found)
        if (!files.contains(fi.absoluteFilePath())) files << fi.absoluteFilePath();
    for (const QString &f : std::as_const(files)) {
        if (!QFileInfo::exists(f)) continue;
        const QFileInfo fi(f);
        const QDateTime at = fi.birthTime().isValid() ? fi.birthTime() : fi.lastModified();
        const QString base = m_libraryDir + "/Recording_" + at.toString("yyyy-MM-dd_HH-mm-ss");
        QString to = base + "." + fi.suffix();
        for (int n = 2; QFileInfo::exists(to); ++n) to = QString("%1_%2.%3").arg(base).arg(n).arg(fi.suffix());
        if (!moveFile(f, to)) continue;
        log(QString("recording saved %1 (%2 MB)").arg(to).arg(QFileInfo(to).size() / (1024 * 1024)));
        emit recordingSaved(to);
    }
    m_recordingSweep.stop();
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
        if (QFileInfo(line).absolutePath() == QFileInfo(recordingDir(m_libraryDir)).absoluteFilePath()) {
            // A full recording. Only move it once we have asked for it to stop,
            // in case the recorder also announces the file when it begins.
            if (!m_fullRecording) collectRecordings(line);
            continue;
        }
        if (!m_saving) {
            // Not ours (e.g. saved with gsr's own tools): keep it as it is.
            const QString to = uniqueClipPath(m_libraryDir, QDateTime::currentDateTime());
            if (moveFile(line, to)) emit clipSaved(to, 0, -1);
            continue;
        }
        const Request r = m_current;
        m_saving = false;
        log(QString("recorder wrote %1 after %2 ms").arg(QFileInfo(line).fileName()).arg(r.dispatched.elapsed()));
        finalize(line, r);
        dispatch();  // the next queued press, while this one is being cut
    }
}

void Recorder::onFinished(int code, QProcess::ExitStatus status)
{
    log(QString("exited: code %1%2").arg(code).arg(status == QProcess::CrashExit ? " (crashed)" : ""));
    if (m_ffmpeg) {
        m_audio->close();
        // A recording in progress keeps what was written so far.
        if (m_fullRecording && !m_seg->segments().isEmpty()) {
            const double at = m_seg->segments().last().end;
            const QString out = recordingDir(m_libraryDir) + "/Recording_"
                                + m_fullStartedAt.toString("yyyy-MM-dd_HH-mm-ss") + ".mp4.part";
            m_fullRecording = false;
            emit fullRecordingChanged(false);
            QPointer<Recorder> self(this);
            m_seg->stopCapture(at, out, [self, out](bool ok, const QString &) {
                if (!self || !ok) return;
                QFile::rename(out, out.chopped(5));
                self->collectRecordings(out.chopped(5));
            });
        }
        m_seg->end();
    }
    if (m_saving || !m_queue.isEmpty()) {
        m_queue.clear();
        m_saving = false;
        emit saveFailed("The recorder stopped before the clip was saved.");
    }
    if (m_fullRecording) {
        // The recorder finishes the file when it stops; collect it.
        setFullRecording(false);
        QTimer::singleShot(500, this, [this] { collectRecordings(); });
    }
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
        ? QString("%1 exited (%2)").arg(m_ffmpeg ? "ffmpeg" : "gpu-screen-recorder",
                                         status == QProcess::CrashExit ? "crashed" : QString::number(code))
        : tail.join('\n');
    setState(State::Failed);
    emit failed(m_lastError);
}
