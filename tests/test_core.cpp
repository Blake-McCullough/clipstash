#include "core/Clip.h"
#include "core/ClipJobs.h"
#include "core/ClipLibrary.h"
#include "core/Exporter.h"
#include "core/MediaTools.h"
#include "core/Recorder.h"
#include "core/Settings.h"
#include "core/Updater.h"
#include "core/Uploader.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSet>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

class TestCore : public QObject {
    Q_OBJECT

    static Settings baseSettings()
    {
        Settings s;
        s.libraryDir = "/clips";
        s.audioTracks = {{"Game", "app-inverse:spotify"}, {"Music", "app:spotify"}, {"Mic", "default_input"}};
        return s;
    }

    static Clip threeTrackClip(const QString &path = "/clips/a.mp4")
    {
        Clip c;
        c.path = path;
        c.duration = 6;
        c.probed = true;
        c.tracks = {{"Game", true, 1.0}, {"Music", true, 1.0}, {"Mic", true, 1.0}};
        return c;
    }

    static bool haveFfmpeg() { return !QStandardPaths::findExecutable("ffmpeg").isEmpty(); }

private slots:
    void recorderArgs()
    {
        const QStringList a = Recorder::buildArgs(baseSettings());
        QCOMPARE(a.value(a.indexOf("-w") + 1), QString("screen"));
        // clip length plus headroom for presses that have to wait their turn
        QCOMPARE(a.value(a.indexOf("-r") + 1), QString::number(60 + Recorder::kHeadroomSeconds));
        QCOMPARE(a.value(a.indexOf("-c") + 1), QString("mp4"));
        QCOMPARE(a.last(), QString("/clips/.clipstash/incoming"));
        QCOMPARE(a.at(a.size() - 2), QString("-o"));
        // one -a per track, in order, so stream N in the file is track N
        QStringList audio;
        for (int i = 0; i < a.size() - 1; ++i)
            if (a[i] == "-a") audio << a[i + 1];
        QCOMPARE(audio, QStringList({"app-inverse:spotify", "app:spotify", "default_input"}));
        QVERIFY(!a.contains("-restore-portal-session"));
    }

    void recorderArgsCbrAndPortal()
    {
        Settings s = baseSettings();
        s.bitrateMode = "cbr";
        s.bitrateKbps = 15000;
        s.captureSource = "portal";
        s.audioTracks = {{"Empty", "  "}};
        const QStringList a = Recorder::buildArgs(s);
        QCOMPARE(a.value(a.indexOf("-bm") + 1), QString("cbr"));
        QCOMPARE(a.value(a.indexOf("-q") + 1), QString("15000"));
        QCOMPARE(a.value(a.indexOf("-restore-portal-session") + 1), QString("yes"));
        QVERIFY(!a.contains("-a"));
    }

    void exportArgsUntouchedSingleTrack()
    {
        Clip c = threeTrackClip();
        c.tracks = {{"Only", true, 1.0}};
        const QStringList a = Exporter::buildArgs(c, {}, "/out.mp4");
        QVERIFY(!a.contains("-ss"));
        QVERIFY(!a.contains("-filter_complex"));
        QCOMPARE(a.value(a.indexOf("-c:v") + 1), QString("copy"));
        QCOMPARE(a.value(a.indexOf("-c:a") + 1), QString("copy"));
    }

    void exportArgsMixAndTrim()
    {
        Clip c = threeTrackClip();
        c.tracks[1].enabled = false;  // drop the music
        c.tracks[2].volume = 0.5;
        c.trimStart = 1.5;
        c.trimEnd = 4;
        const QStringList a = Exporter::buildArgs(c, {}, "/out.mp4");
        QCOMPARE(a.value(a.indexOf("-ss") + 1), QString("1.500"));
        QCOMPARE(a.value(a.indexOf("-to") + 1), QString("4.000"));
        QVERIFY(a.indexOf("-ss") < a.indexOf("-i"));
        const QString graph = a.value(a.indexOf("-filter_complex") + 1);
        QVERIFY2(graph.contains("[0:a:0]volume=1.00"), qPrintable(graph));
        QVERIFY2(graph.contains("[0:a:2]volume=0.50"), qPrintable(graph));
        QVERIFY2(!graph.contains("[0:a:1]"), qPrintable(graph));
        QVERIFY2(graph.contains("amix=inputs=2"), qPrintable(graph));
        QVERIFY(a.contains("[aout]"));
    }

    void exportArgsAllMuted()
    {
        Clip c = threeTrackClip();
        for (ClipTrack &t : c.tracks) t.enabled = false;
        const QStringList a = Exporter::buildArgs(c, {}, "/out.mp4");
        QVERIFY(a.contains("-an"));
        QVERIFY(!a.contains("-filter_complex"));
    }

    void exportArgsReencode()
    {
        ExportOptions o;
        o.reencodeVideo = true;
        o.maxHeight = 720;
        const QStringList a = Exporter::buildArgs(threeTrackClip(), o, "/out.mp4");
        QCOMPARE(a.value(a.indexOf("-c:v") + 1), QString("libx264"));
        QVERIFY(a.value(a.indexOf("-vf") + 1).contains("720"));
    }

    void compatConvertsHevc()
    {
        Settings s = baseSettings();
        s.exportEncoder = "libx264";
        Clip c = threeTrackClip();
        c.tracks = {{"Only", true, 1.0}};
        c.videoCodec = "hevc";
        // even a share, which otherwise copies video, converts HEVC
        ExportOptions o = ExportOptions::forClip(s, c, true);
        QVERIFY(o.reencodeVideo);
        QCOMPARE(Exporter::buildArgs(c, o, "/o.mp4").value(Exporter::buildArgs(c, o, "/o.mp4").indexOf("-c:v") + 1),
                 QString("libx264"));
        c.videoCodec = "h264";
        QVERIFY(!ExportOptions::forClip(s, c, true).reencodeVideo);
        c.videoCodec = "hevc";
        s.exportCompat = false;
        QVERIFY(!ExportOptions::forClip(s, c, true).reencodeVideo);
        s.exportEncoder = "auto";
        QVERIFY(ExportOptions::forClip(s, c, false).encoder != "auto");
    }

    void folderNormalisation()
    {
        QCOMPARE(Settings::normalizeDir("~/Videos/clips/"), QDir::homePath() + "/Videos/clips");
        QCOMPARE(Settings::normalizeDir("  '/mnt/games/My Clips'  "), QString("/mnt/games/My Clips"));
        QCOMPARE(Settings::normalizeDir("file:///mnt/d/clips"), QString("/mnt/d/clips"));
        QCOMPARE(Settings::normalizeDir(""), Settings::defaultLibraryDir());
    }

    void updaterParsesRelease()
    {
        // Built with QJsonObject: moc on older Qt trips over "//" inside raw strings.
        const QJsonArray assets{
            QJsonObject{{"name", "clipstash-x86_64.sha256"}, {"browser_download_url", "https://example/sha"}},
            QJsonObject{{"name", "clipstash-x86_64"}, {"browser_download_url", "https://example/bin"},
                        {"digest", "sha256:ABCDEF"}}};
        const QByteArray json = QJsonDocument(QJsonObject{{"tag_name", "v0.3.1"}, {"html_url", "https://example/r"},
                                                          {"body", "notes"}, {"assets", assets}}).toJson();
        const UpdateInfo info = Updater::parseRelease(json, "0.2.0");
        QCOMPARE(info.version, QString("0.3.1"));
        QCOMPARE(info.downloadUrl, QString("https://example/bin"));
        QCOMPARE(info.sha256, QString("abcdef"));
        QVERIFY(info.newer);
        QVERIFY(!Updater::parseRelease(json, "0.3.1").newer);
        QVERIFY(!Updater::parseRelease(json, "0.10.0").newer);  // numeric, not string, comparison
        QVERIFY(Updater::isNewer("v0.10.0", "0.9.9"));
        QVERIFY(!Updater::isNewer("garbage", "0.1.0"));
        QVERIFY(!Updater::parseRelease("{}", "0.1.0").newer);
    }

    void autoSplitTracks()
    {
        Settings s = baseSettings();
        s.autoSplitAudio = true;
        QCOMPARE(s.effectiveTracks().size(), 2);  // nothing seen yet: desktop + mic
        s.knownApps = {"spotify", "Discord"};
        const auto t = s.effectiveTracks();
        QCOMPARE(t.size(), 4);
        QCOMPARE(t[0].sources, QString("app-inverse:spotify|app-inverse:Discord"));
        QCOMPARE(t[1].sources, QString("app:spotify"));
        QCOMPARE(t[2].name, QString("Discord"));
        QCOMPARE(t[3].sources, QString("default_input"));
        Settings before = s;
        s.knownApps << "firefox";
        QVERIFY(s.recorderConfigDiffers(before));  // a new app means a recorder restart
        QVERIFY(Recorder::buildArgs(s).contains("app:firefox"));
    }

    void autoSplitFilter()
    {
        const QStringList listed = {"spotify", "plasmashell", "Clipstash", "Discord", "  ", "kwin_wayland",
                                    "SPOTIFY", "speech-dispatcher", "MarvelRivals.exe"};
        QCOMPARE(Settings::usefulAudioApps(listed, {}), QStringList({"spotify", "Discord", "MarvelRivals.exe"}));
        QCOMPARE(Settings::usefulAudioApps(listed, {"discord"}), QStringList({"spotify", "MarvelRivals.exe"}));
    }

    void discordBudget()
    {
        Settings s = baseSettings();
        s.exportEncoder = "libx264";
        s.discordLimitMb = 10;
        Clip c = threeTrackClip();
        c.duration = 60;
        ExportOptions o = ExportOptions::forDiscord(s, c);
        // 10 MB over 60 s is ~1.3 Mbit/s in total: 720p30 territory
        QVERIFY2(o.targetVideoKbps > 900 && o.targetVideoKbps < 1300, qPrintable(QString::number(o.targetVideoKbps)));
        QCOMPARE(o.maxHeight, 720);
        QCOMPARE(o.maxFps, 30);
        const QStringList a = Exporter::buildArgs(c, o, "/o.mp4");
        QVERIFY(a.contains("-maxrate"));
        QCOMPARE(a.value(a.indexOf("-b:a") + 1), QString("96k"));
        c.duration = 8;  // short clip: plenty of budget, but capped
        o = ExportOptions::forDiscord(s, c);
        QCOMPARE(o.maxHeight, 1440);
        QVERIFY(o.targetVideoKbps <= 20000);
        s.discordLimitMb = 500;
        c.duration = 60;
        QVERIFY(ExportOptions::forDiscord(s, c).targetVideoKbps <= 20000);
    }

    // The real thing: a noisy 20 s clip must come out under the limit.
    void discordFitsLimit()
    {
        if (!haveFfmpeg()) QSKIP("ffmpeg not installed");
        QTemporaryDir dir;
        const QString src = dir.filePath("Replay_big.mp4");
        QCOMPARE(QProcess::execute("ffmpeg", {"-v", "error", "-y",
            "-f", "lavfi", "-i", "testsrc2=size=1280x720:rate=60:duration=20",
            "-f", "lavfi", "-i", "anoisesrc=duration=20:amplitude=0.3",
            "-vf", "noise=alls=40:allf=t", "-c:v", "libx264", "-preset", "ultrafast", "-crf", "18",
            "-pix_fmt", "yuv420p", "-c:a", "aac", src}), 0);
        QVERIFY(QFileInfo(src).size() > 4 * 1024 * 1024);

        ClipLibrary lib;
        lib.setDirectory(dir.path());
        QTRY_VERIFY_WITH_TIMEOUT(lib.rowCount() == 1 && lib.clipAt(0).probed, 15000);
        Settings s;
        s.exportEncoder = "libx264";
        s.discordLimitMb = 2;
        ClipJobs jobs(&lib);
        QSignalSpy ready(&jobs, &ClipJobs::discordReady);
        QSignalSpy failed(&jobs, &ClipJobs::failed);
        QVERIFY(jobs.discordClip(lib.clipAt(0), s));
        QVERIFY2(ready.wait(120000), failed.isEmpty() ? "timeout" : qPrintable(failed[0][1].toString()));
        const QString out = ready[0][1].toString();
        QVERIFY2(out.startsWith(ClipJobs::discordDir()), qPrintable(out));  // a clipboard copy, not a clip
        QCOMPARE(lib.rowCount(), 1);
        const qint64 size = QFileInfo(out).size();
        QVERIFY2(size <= 2 * 1024 * 1024, qPrintable(QString::number(size)));
        QVERIFY2(size > 1024 * 1024, qPrintable(QString::number(size)));  // and it used the budget
    }

    void cutWindow()
    {
        // pressed as it was signalled: the last 60 s of an 80 s file
        auto w = Recorder::cutWindow(80, 0, 60);
        QCOMPARE(w.first, 20.0);
        QCOMPARE(w.second, 80.0);
        // signalled 7 s after the press: drop the 7 s that came after it
        w = Recorder::cutWindow(80, 7, 60);
        QCOMPARE(w.first, 13.0);
        QCOMPARE(w.second, 73.0);
        // buffer not full yet: keep what there is
        w = Recorder::cutWindow(30, 2, 60);
        QCOMPARE(w.first, 0.0);
        QCOMPARE(w.second, 28.0);
    }

    // Three presses in about a second against a recorder that, like the real
    // one, takes a while to save and ignores requests while it is saving.
    // Every press must still give its own full-length clip.
    void rapidSaves()
    {
        if (!haveFfmpeg()) QSKIP("ffmpeg not installed");
        QTemporaryDir dir;
        const QString source = dir.filePath("source.mp4");
        QCOMPARE(QProcess::execute("ffmpeg", {"-v", "error", "-y",
            "-f", "lavfi", "-i", "testsrc=size=320x180:rate=30:duration=40",
            "-f", "lavfi", "-i", "sine=frequency=440:duration=40",
            "-c:v", "libx264", "-preset", "ultrafast", "-g", "30", "-pix_fmt", "yuv420p",
            "-c:a", "aac", source}), 0);

        const QString fake = dir.filePath("fake-gsr");
        {
            QFile f(fake);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(QString(R"SH(#!/bin/bash
out=""; while [ $# -gt 0 ]; do [ "$1" = "-o" ] && out="$2"; shift; done
busy=0; n=0
save() {
  if [ $busy = 1 ]; then return; fi   # a save is running: ignored, like the real recorder
  busy=1; sleep 1.5; n=$((n+1)); f="$out/Replay_$n.mp4"; cp '%1' "$f"; echo "$f"; busy=0
}
trap save USR1 RTMIN+1 RTMIN+2 RTMIN+3
trap 'exit 0' INT TERM
while true; do sleep 0.1; done
)SH").arg(source).toUtf8());
        }
        QFile::setPermissions(fake, QFile::permissions(fake) | QFileDevice::ExeOwner);

        Settings s;
        s.recorderBinary = fake;
        s.libraryDir = dir.filePath("Clips");
        s.replaySeconds = 12;  // no matching signal, so every save is cut
        Recorder rec;
        QSignalSpy saved(&rec, &Recorder::clipSaved);
        QSignalSpy failed(&rec, &Recorder::saveFailed);
        rec.start(s);
        QTRY_VERIFY_WITH_TIMEOUT(rec.state() == Recorder::State::Running, 5000);
        QTest::qWait(1000);

        for (int i = 0; i < 3; ++i) {
            QVERIFY(rec.saveReplay());
            QTest::qWait(400);
        }
        QTRY_VERIFY_WITH_TIMEOUT(saved.size() + failed.size() == 3, 30000);
        QVERIFY2(failed.isEmpty(), failed.isEmpty() ? "" : qPrintable(failed[0][0].toString()));

        QSet<QString> paths;
        for (const auto &args : saved) {
            const QString path = args[0].toString();
            QVERIFY2(path.startsWith(s.libraryDir + "/Replay_"), qPrintable(path));
            QVERIFY(!path.contains("/.clipstash/"));
            paths.insert(path);
            QProcess probe;
            probe.start("ffprobe", {"-v", "error", "-print_format", "json", "-show_format", "-show_streams", path});
            QVERIFY(probe.waitForFinished(10000));
            const ProbeResult r = MediaTools::parseProbeJson(probe.readAllStandardOutput());
            // full length; stream-copy cuts may start up to one keyframe (1 s) early
            QVERIFY2(r.duration > 11.5 && r.duration < 13.6, qPrintable(QString::number(r.duration)));
            QCOMPARE(r.audioTitles.size(), 1);
        }
        QCOMPARE(paths.size(), 3);
        QVERIFY(QDir(Recorder::incomingDir(s.libraryDir)).entryList({"*.mp4"}, QDir::Files).isEmpty());
        rec.stop();
        QTRY_VERIFY_WITH_TIMEOUT(rec.state() == Recorder::State::Stopped, 5000);
    }

    void renameMovesFile()
    {
        QTemporaryDir dir;
        for (const char *n : {"Replay_a.mp4", "Replay_b.mp4"}) {
            QFile f(dir.filePath(n));
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("x");
        }
        ClipLibrary lib;
        lib.setDirectory(dir.path());
        QCOMPARE(lib.rowCount(), 2);
        lib.update(dir.filePath("Replay_a.mp4"), [](Clip &c) { c.favorite = true; c.trimStart = 2; });
        QSignalSpy renamed(&lib, &ClipLibrary::clipRenamed);

        const QString p = lib.rename(dir.filePath("Replay_a.mp4"), "  Clutch 1v5: ace?  ");
        QCOMPARE(p, dir.filePath("Clutch 1v5- ace-.mp4"));
        QVERIFY(QFileInfo::exists(p));
        QVERIFY(!QFileInfo::exists(dir.filePath("Replay_a.mp4")));
        QCOMPARE(renamed.size(), 1);
        const Clip *c = lib.clip(p);
        QVERIFY(c);
        QCOMPARE(c->title, QString("Clutch 1v5: ace?"));  // the title keeps what was typed
        QVERIFY(c->favorite);
        QCOMPARE(c->trimStart, 2.0);

        // same name again: kept unique
        const QString p2 = lib.rename(dir.filePath("Replay_b.mp4"), "Clutch 1v5: ace?");
        QCOMPARE(p2, dir.filePath("Clutch 1v5- ace- (2).mp4"));
        // renaming to its own name changes nothing on disk
        QCOMPARE(lib.rename(p, "Clutch 1v5: ace?"), p);

        // edits survive a fresh load from disk
        ClipLibrary again;
        again.setDirectory(dir.path());
        QVERIFY(again.clip(p) && again.clip(p)->favorite && again.clip(p)->title == "Clutch 1v5: ace?");

        QCOMPARE(ClipLibrary::safeFileName(" ..hidden. "), QString("hidden"));
        QCOMPARE(ClipLibrary::safeFileName("a/b\\c|d"), QString("a-b-c-d"));
        QCOMPARE(ClipLibrary::safeFileName("   "), QString("Clip"));
    }

    // Start/stop recording through the recorder, alongside the replay buffer.
    void fullRecording()
    {
        if (!haveFfmpeg()) QSKIP("ffmpeg not installed");
        QTemporaryDir dir;
        const QString source = dir.filePath("source.mp4");
        QCOMPARE(QProcess::execute("ffmpeg", {"-v", "error", "-y", "-f", "lavfi", "-i",
            "testsrc=size=160x90:rate=10:duration=3", "-c:v", "libx264", "-preset", "ultrafast",
            "-pix_fmt", "yuv420p", source}), 0);
        const QString fake = dir.filePath("fake-gsr");
        {
            QFile f(fake);
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(QString(R"SH(#!/bin/bash
ro=""; while [ $# -gt 0 ]; do [ "$1" = "-ro" ] && ro="$2"; shift; done
rec=0
toggle() {
  if [ $rec = 0 ]; then rec=1; cp '%1' "$ro/Video_in_progress.mp4"; echo "$ro/Video_in_progress.mp4";
  else rec=0; mv "$ro/Video_in_progress.mp4" "$ro/Video_done.mp4"; echo "$ro/Video_done.mp4"; fi
}
trap toggle RTMIN
trap 'exit 0' INT TERM
while true; do sleep 0.1; done
)SH").arg(source).toUtf8());
        }
        QFile::setPermissions(fake, QFile::permissions(fake) | QFileDevice::ExeOwner);

        Settings s;
        s.recorderBinary = fake;
        s.libraryDir = dir.filePath("Clips");
        Recorder rec;
        QSignalSpy saved(&rec, &Recorder::recordingSaved);
        rec.start(s);
        QTRY_VERIFY_WITH_TIMEOUT(rec.state() == Recorder::State::Running, 5000);
        QTest::qWait(1000);
        QVERIFY(rec.toggleFullRecording());
        QVERIFY(rec.isFullRecording());
        QTest::qWait(800);
        QVERIFY(saved.isEmpty());  // a file announced at the start must not be moved
        QVERIFY(rec.toggleFullRecording());
        QVERIFY(!rec.isFullRecording());
        QTRY_COMPARE_WITH_TIMEOUT(saved.size(), 1, 5000);
        const QString out = saved[0][0].toString();
        QVERIFY2(QFileInfo(out).fileName().startsWith("Recording_"), qPrintable(out));
        QCOMPARE(QFileInfo(out).absolutePath(), QFileInfo(s.libraryDir).absoluteFilePath());
        QVERIFY(QDir(Recorder::recordingDir(s.libraryDir)).entryList(QDir::Files).isEmpty());
        rec.stop();
        QTRY_VERIFY_WITH_TIMEOUT(rec.state() == Recorder::State::Stopped, 5000);
    }

    // The Windows engine (ffmpeg writing a ring of small pieces), driven here
    // by a test pattern instead of the desktop: rapid clips and a long recording.
    void ffmpegEngine()
    {
        if (!haveFfmpeg()) QSKIP("ffmpeg not installed");
        QTemporaryDir dir;
        qputenv("CLIPSTASH_TEST_VIDEO", "-re -f lavfi -i testsrc2=size=320x180:rate=30");
        Settings s;
        s.recorderEngine = "ffmpeg";
        s.videoEncoder = "cpu";
        s.lowResource = true;
        s.libraryDir = dir.filePath("Clips");
        s.replaySeconds = 8;
        s.audioTracks = {{"Game", "default_output"}, {"Mic", "default_input"}};
        Recorder rec;
        QSignalSpy saved(&rec, &Recorder::clipSaved);
        QSignalSpy failed(&rec, &Recorder::saveFailed);
        QSignalSpy recorded(&rec, &Recorder::recordingSaved);
        rec.start(s);
        QTRY_VERIFY_WITH_TIMEOUT(rec.state() == Recorder::State::Running, 15000);
        QTest::qWait(11000);

        for (int i = 0; i < 3; ++i) {
            QVERIFY(rec.saveReplay());
            QTest::qWait(700);
        }
        QTRY_VERIFY_WITH_TIMEOUT(saved.size() + failed.size() == 3, 30000);
        QVERIFY2(failed.isEmpty(), failed.isEmpty() ? "" : qPrintable(failed[0][0].toString()));
        QSet<QString> paths;
        for (const auto &args : saved) {
            const QString path = args[0].toString();
            paths.insert(path);
            QProcess probe;
            probe.start("ffprobe", {"-v", "error", "-print_format", "json", "-show_format", "-show_streams", path});
            QVERIFY(probe.waitForFinished(10000));
            const ProbeResult r = MediaTools::parseProbeJson(probe.readAllStandardOutput());
            QVERIFY2(r.duration > 7.0 && r.duration < 11.0, qPrintable(QString::number(r.duration)));
            QCOMPARE(r.audioTitles, (QStringList{"Game", "Mic"}));
        }
        QCOMPARE(paths.size(), 3);

        QVERIFY(rec.toggleFullRecording());
        QTest::qWait(5000);
        QVERIFY(rec.toggleFullRecording());
        QTRY_COMPARE_WITH_TIMEOUT(recorded.size(), 1, 20000);
        const QString out = recorded[0][0].toString();
        QVERIFY2(QFileInfo(out).fileName().startsWith("Recording_"), qPrintable(out));
        QProcess probe;
        probe.start("ffprobe", {"-v", "error", "-print_format", "json", "-show_format", "-show_streams", out});
        QVERIFY(probe.waitForFinished(10000));
        const double d = MediaTools::parseProbeJson(probe.readAllStandardOutput()).duration;
        QVERIFY2(d > 4.0 && d < 8.5, qPrintable(QString::number(d)));

        rec.stop();
        QTRY_VERIFY_WITH_TIMEOUT(rec.state() == Recorder::State::Stopped, 8000);
        qunsetenv("CLIPSTASH_TEST_VIDEO");
    }

    // "Save" / "Save as new clip": sources stay separate tracks, the dropped
    // one is gone, and "Save" rewrites the clip itself.
    void saveKeepsSeparateTracks()
    {
        if (!haveFfmpeg()) QSKIP("ffmpeg not installed");
        QTemporaryDir dir;
        const QString src = dir.filePath("Replay_x.mp4");
        QCOMPARE(QProcess::execute("ffmpeg", {"-v", "error", "-y",
            "-f", "lavfi", "-i", "testsrc=size=320x180:rate=30:duration=6",
            "-f", "lavfi", "-i", "sine=frequency=300:duration=6",
            "-f", "lavfi", "-i", "sine=frequency=1000:duration=6",
            "-f", "lavfi", "-i", "sine=frequency=3000:duration=6",
            "-map", "0:v", "-map", "1:a", "-map", "2:a", "-map", "3:a",
            "-c:v", "libx264", "-preset", "ultrafast", "-g", "15", "-pix_fmt", "yuv420p", "-c:a", "aac", src}), 0);
        ClipLibrary lib;
        lib.setDirectory(dir.path());
        QTRY_VERIFY_WITH_TIMEOUT(lib.rowCount() == 1 && lib.clipAt(0).probed, 15000);
        lib.update(src, [](Clip &c) {
            c.tracks = {{"Game", true, 1.0}, {"Spotify", false, 1.0}, {"Mic", true, 0.5}};
            c.trimStart = 1;
            c.trimEnd = 4;
        });
        auto probeFile = [](const QString &f) {
            QProcess p;
            p.start("ffprobe", {"-v", "error", "-print_format", "json", "-show_format", "-show_streams", f});
            p.waitForFinished(10000);
            return MediaTools::parseProbeJson(p.readAllStandardOutput());
        };
        Settings s;
        s.exportEncoder = "libx264";
        ClipJobs jobs(&lib);

        QSignalSpy exported(&jobs, &ClipJobs::exported);
        QVERIFY(jobs.exportClip(*lib.clip(src), s, "Ace"));
        QVERIFY(exported.wait(30000));
        const ProbeResult copy = probeFile(exported[0][1].toString());
        QCOMPARE(copy.audioTitles, QStringList({"Game", "Mic"}));
        QVERIFY(qAbs(copy.duration - 3) < 1.2);

        QSignalSpy overwritten(&jobs, &ClipJobs::overwritten);
        QVERIFY(jobs.overwriteClip(*lib.clip(src), s));
        QVERIFY(overwritten.wait(30000));
        const ProbeResult same = probeFile(src);
        QCOMPARE(same.audioTitles, QStringList({"Game", "Mic"}));
        QVERIFY(qAbs(same.duration - 3) < 1.2);
        const Clip *c = lib.clip(src);
        QVERIFY(c && !c->isTrimmed() && c->tracks.size() == 2);
        QCOMPARE(c->tracks[1].volume, 1.0);  // baked into the file
        QTRY_VERIFY_WITH_TIMEOUT(lib.clip(src)->probed && lib.clip(src)->duration < 4.5, 15000);
    }

    void needsRender()
    {
        Clip c = threeTrackClip();
        QVERIFY(c.needsRender());  // several tracks: browsers would only play the first
        c.tracks = {{"Only", true, 1.0}};
        QVERIFY(!c.needsRender());
        c.trimEnd = 3;
        QVERIFY(c.needsRender());
    }

    void clipJsonRoundTrip()
    {
        Clip c = threeTrackClip();
        c.title = "Clutch";
        c.trimStart = 1;
        c.trimEnd = 5;
        c.tracks[2].enabled = false;
        c.shareUrl = "https://pixeldrain.com/u/abc";
        c.favorite = true;
        Clip d;
        d.applyJson(c.toJson());
        QCOMPARE(d.title, c.title);
        QCOMPARE(d.trimEnd, 5.0);
        QCOMPARE(d.tracks.size(), 3);
        QVERIFY(!d.tracks[2].enabled);
        QCOMPARE(d.shareUrl, c.shareUrl);
        QVERIFY(d.favorite);
    }

    void parsers()
    {
        const auto list = MediaTools::parseGsrList("screen\nDP-1|2560x1440\n\nportal\n");
        QCOMPARE(list.size(), 3);
        QCOMPARE(list[1].first, QString("DP-1"));
        QCOMPARE(list[1].second, QString("2560x1440"));

        const QByteArray json = R"({"streams":[
            {"codec_type":"video","width":1920,"height":1080},
            {"codec_type":"audio","tags":{"title":"Desktop"}},
            {"codec_type":"audio"}],
            "format":{"duration":"30.5"}})";
        const ProbeResult r = MediaTools::parseProbeJson(json);
        QVERIFY(r.ok);
        QCOMPARE(r.width, 1920);
        QCOMPARE(r.duration, 30.5);
        QCOMPARE(r.audioTitles, QStringList({"Desktop", ""}));
        const QByteArray mp4 = R"({"streams":[{"codec_type":"video","width":64,"height":64},
            {"codec_type":"audio","tags":{"handler_name":"Desktop + Microphone"}},
            {"codec_type":"audio","tags":{"handler_name":"SoundHandler"}}],"format":{"duration":"1"}})";
        QCOMPARE(MediaTools::parseProbeJson(mp4).audioTitles, QStringList({"Desktop + Microphone", ""}));
        QVERIFY(!MediaTools::parseProbeJson("not json").ok);
    }

    void uploadNames()
    {
        QCOMPARE(Uploader::sanitizeName("12 Nov 2026, 12:46:57"), QString("12 Nov 2026, 12-46-57.mp4"));
        QCOMPARE(Uploader::sanitizeName("  a/b\\c  "), QString("a-b-c.mp4"));
        QCOMPARE(Uploader::sanitizeName(""), QString("clip.mp4"));
        QCOMPARE(Uploader::viewUrl("abc"), QString("https://pixeldrain.com/u/abc"));
    }

    // End to end: make a clip with three distinct tones, export with one
    // track dropped and a trim, and check what ffmpeg actually produced.
    void realExport()
    {
        if (!haveFfmpeg()) QSKIP("ffmpeg not installed");
        QTemporaryDir dir;
        const QString src = dir.filePath("src.mp4");
        const int rc = QProcess::execute("ffmpeg", {"-v", "error", "-y",
            "-f", "lavfi", "-i", "testsrc=size=320x180:rate=30:duration=6",
            "-f", "lavfi", "-i", "sine=frequency=300:duration=6",
            "-f", "lavfi", "-i", "sine=frequency=1000:duration=6",
            "-f", "lavfi", "-i", "sine=frequency=3000:duration=6",
            "-map", "0:v", "-map", "1:a", "-map", "2:a", "-map", "3:a",
            "-c:v", "libx264", "-preset", "ultrafast", "-g", "30", "-pix_fmt", "yuv420p",
            "-c:a", "aac", src});
        QCOMPARE(rc, 0);

        Clip c = threeTrackClip(src);
        c.tracks[1].enabled = false;
        c.trimStart = 1;
        c.trimEnd = 4;

        Exporter exporter;
        QSignalSpy done(&exporter, &Exporter::finished);
        const QString out = dir.filePath("out.mp4");
        exporter.start(c, {}, out);
        QVERIFY(done.wait(30000));
        QVERIFY2(done[0][0].toBool(), qPrintable(done[0][2].toString()));

        QProcess probe;
        probe.start("ffprobe", {"-v", "error", "-print_format", "json", "-show_format", "-show_streams", out});
        QVERIFY(probe.waitForFinished(10000));
        const ProbeResult r = MediaTools::parseProbeJson(probe.readAllStandardOutput());
        QVERIFY(r.ok);
        QCOMPARE(r.audioTitles.size(), 1);  // mixed down to a single track
        QVERIFY2(qAbs(r.duration - 3.0) < 0.3, qPrintable(QString::number(r.duration)));

        QCOMPARE(ClipJobs::uniqueOutputPath(src), dir.filePath("src_edit.mp4"));
    }

    // Batch-convert HEVC clips in place and check the files really changed
    // codec while keeping their tracks, dates and edit state.
    void convertInPlace()
    {
        if (!haveFfmpeg()) QSKIP("ffmpeg not installed");
        QProcess enc;
        enc.start("ffmpeg", {"-hide_banner", "-encoders"});
        enc.waitForFinished(10000);
        if (!enc.readAllStandardOutput().contains("libx265")) QSKIP("no HEVC encoder to make test clips");

        QTemporaryDir dir;
        QStringList files;
        for (int i = 0; i < 2; ++i) {
            const QString f = dir.filePath(QString("Replay_%1.mp4").arg(i));
            QCOMPARE(QProcess::execute("ffmpeg", {"-v", "error", "-y",
                "-f", "lavfi", "-i", "testsrc=size=320x180:rate=30:duration=2",
                "-f", "lavfi", "-i", "sine=frequency=300:duration=2",
                "-f", "lavfi", "-i", "sine=frequency=900:duration=2",
                "-map", "0:v", "-map", "1:a", "-map", "2:a",
                "-c:v", "libx265", "-preset", "ultrafast", "-x265-params", "log-level=error",
                "-pix_fmt", "yuv420p", "-c:a", "aac", f}), 0);
            files << f;
        }
        const QDateTime stamp = QDateTime::fromSecsSinceEpoch(1700000000);
        {
            QFile f(files[0]);
            QVERIFY(f.open(QIODevice::ReadWrite));
            QVERIFY(f.setFileTime(stamp, QFileDevice::FileModificationTime));
        }

        ClipLibrary lib;
        lib.setDirectory(dir.path());
        QTRY_VERIFY_WITH_TIMEOUT(lib.rowCount() == 2 && lib.clipAt(0).videoCodec == "hevc"
                                     && lib.clipAt(1).videoCodec == "hevc", 15000);
        lib.update(files[0], [](Clip &c) { c.trimStart = 0.5; c.title = "Keep me"; });

        Settings s;
        s.exportEncoder = "libx264";
        s.exportMaxHeight = 90;  // must be ignored for in-place conversion
        ClipJobs jobs(&lib);
        QSignalSpy done(&jobs, &ClipJobs::batchFinished);
        jobs.convertClips(files, s);
        QVERIFY(done.wait(60000));
        QCOMPARE(done[0][1].toInt(), 2);
        QCOMPARE(done[0][2].toInt(), 0);

        for (const QString &f : std::as_const(files)) {
            QProcess probe;
            probe.start("ffprobe", {"-v", "error", "-print_format", "json", "-show_format", "-show_streams", f});
            QVERIFY(probe.waitForFinished(10000));
            const ProbeResult r = MediaTools::parseProbeJson(probe.readAllStandardOutput());
            QCOMPARE(r.videoCodec, QString("h264"));
            QCOMPARE(r.height, 180);
            QCOMPARE(r.audioTitles.size(), 2);  // both tracks survive
        }
        QCOMPARE(QFileInfo(files[0]).lastModified().toSecsSinceEpoch(), stamp.toSecsSinceEpoch());
        const Clip *c = lib.clip(files[0]);
        QVERIFY(c);
        QCOMPARE(c->videoCodec, QString("h264"));
        QCOMPARE(c->title, QString("Keep me"));
        QCOMPARE(c->trimStart, 0.5);
        QCOMPARE(lib.rowCount(), 2);
        QVERIFY(QDir(dir.path() + "/.clipstash/tmp").isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestCore)
#include "test_core.moc"
