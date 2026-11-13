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
        QCOMPARE(a.value(a.indexOf("-r") + 1), QString("60"));
        QCOMPARE(a.value(a.indexOf("-c") + 1), QString("mp4"));
        QCOMPARE(a.last(), QString("/clips"));
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
