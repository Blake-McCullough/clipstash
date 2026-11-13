#include "hotkeys/PortalHotkeys.h"
#include "ui/MainWindow.h"
#include "ui/Theme.h"

#include "core/ClipLibrary.h"
#include "core/Exporter.h"
#include "core/Recorder.h"
#include "core/Settings.h"
#include <unistd.h>
#include <QDirIterator>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTextStream>
#include <QLoggingCategory>
#include <QApplication>
#include <QCommandLineParser>
#include <QLocalServer>
#include <QLocalSocket>

#include <clocale>
#include <unistd.h>

static const char *kAppId = "io.github.blake_mccullough.clipstash";

static QString socketName() { return QString("clipstash-%1").arg(getuid()); }

// Returns true if a running instance took the command.
static bool sendToRunning(const QString &command)
{
    QLocalSocket sock;
    sock.connectToServer(socketName());
    if (!sock.waitForConnected(300)) return false;
    sock.write(command.toUtf8() + '\n');
    sock.waitForBytesWritten(500);
    sock.disconnectFromServer();
    return true;
}

// Plain-text report for troubleshooting; never prints the API key.
static int diagnose()
{
    QTextStream out(stdout);
    const Settings s = Settings::load();
    out << "clipstash " << CLIPSTASH_VERSION << " (Qt " << qVersion() << ", "
        << QGuiApplication::platformName() << ", session " << qEnvironmentVariable("XDG_SESSION_TYPE", "?")
        << ", desktop " << qEnvironmentVariable("XDG_CURRENT_DESKTOP", "?") << ")\n";

    const QFileInfo dir(s.libraryDir);
    out << "clip folder: " << s.libraryDir << "\n"
        << "  exists: " << (dir.isDir() ? "yes" : "NO") << ", writable: " << (dir.isWritable() ? "yes" : "NO") << "\n";
    QMap<QString, int> byExt;
    QStringList sample;
    int total = 0;
    QDirIterator it(s.libraryDir, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString p = it.next();
        if (p.contains("/.clipstash/")) continue;
        ++total;
        ++byExt[QFileInfo(p).suffix().toLower()];
        if (sample.size() < 8) sample << QDir(s.libraryDir).relativeFilePath(p);
    }
    out << "  files: " << total;
    for (auto e = byExt.cbegin(); e != byExt.cend(); ++e) out << "  ." << e.key() << "=" << e.value();
    out << "\n";
    for (const QString &f : std::as_const(sample)) out << "    " << f << "\n";

    ClipLibrary lib;
    lib.setDirectory(s.libraryDir);
    out << "  shown in library: " << lib.rowCount() << "\n";

    auto tool = [&](const QString &name) {
        const QString path = QStandardPaths::findExecutable(name);
        out << name << ": " << (path.isEmpty() ? "NOT FOUND" : path) << "\n";
    };
    tool(s.recorderBinary);
    tool("ffmpeg");
    tool("ffprobe");
    out << "recorder args: " << Recorder::buildArgs(s).join(' ') << "\n";
    out << "hotkeys: " << s.hotkeyBackend << "\n";
    out << "pixeldrain key: " << (s.pixeldrainKey.isEmpty() ? "not set" : "set") << "\n";
    out << "export: mode=" << s.exportVideoMode << " encoder=" << s.exportEncoder
        << " (auto=" << ExportOptions::autoEncoder() << ") convert-to-h264=" << (s.exportCompat ? "on" : "off") << "\n";
    out.flush();
    _exit(0);  // skip teardown: the library has background probes in flight
}

int main(int argc, char **argv)
{
    // Set before the application object exists so Qt's own portal
    // registration (newer Qt versions) uses the same app ID as ours.
    QGuiApplication::setDesktopFileName(kAppId);
    // We register with the portal ourselves for older Qt; when Qt then tries
    // again it logs a harmless "already associated" warning. Keep it quiet.
    QLoggingCategory::setFilterRules(QStringLiteral("qt.qpa.services.warning=false"));
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("clipstash");
    QCoreApplication::setApplicationName("clipstash");
    QCoreApplication::setApplicationVersion(CLIPSTASH_VERSION);
    app.setQuitOnLastWindowClosed(false);
    std::setlocale(LC_NUMERIC, "C");

    QCommandLineParser parser;
    parser.setApplicationDescription("Instant-replay clips with gpu-screen-recorder");
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption saveClip("save-clip", "Save a clip from the replay buffer.");
    QCommandLineOption seconds("seconds", "With --save-clip: only keep the last <n> seconds (10, 30, 60, 300, 600, 1800).", "n");
    QCommandLineOption toggle("toggle-recording", "Start or stop the background recorder.");
    QCommandLineOption minimized("minimized", "Start hidden in the system tray.");
    QCommandLineOption quit("quit", "Quit the running instance.");
    QCommandLineOption diag("diagnose", "Print a troubleshooting report and exit.");
    parser.addOptions({saveClip, seconds, toggle, minimized, quit, diag});
    parser.process(app);
    if (parser.isSet(diag)) return diagnose();

    QString command = "show";
    if (parser.isSet(saveClip)) command = "save-clip " + parser.value(seconds);
    else if (parser.isSet(toggle)) command = "toggle-recording";
    else if (parser.isSet(quit)) command = "quit";

    if (sendToRunning(command)) return 0;
    if (parser.isSet(quit)) return 0;
    if (parser.isSet(saveClip) || parser.isSet(toggle)) {
        fprintf(stderr, "clipstash is not running\n");
        return 1;
    }

    PortalHotkeys::registerApp(kAppId);
    app.setStyleSheet(Theme::styleSheet());
    app.setWindowIcon(Theme::appIcon());

    MainWindow window;

    QLocalServer::removeServer(socketName());
    QLocalServer server;
    server.setSocketOptions(QLocalServer::UserAccessOption);
    server.listen(socketName());
    QObject::connect(&server, &QLocalServer::newConnection, &window, [&] {
        while (QLocalSocket *c = server.nextPendingConnection()) {
            QObject::connect(c, &QLocalSocket::readyRead, &window, [c, &window] {
                while (c->canReadLine()) window.handleCommand(QString::fromUtf8(c->readLine()));
            });
            QObject::connect(c, &QLocalSocket::disconnected, c, &QObject::deleteLater);
        }
    });

    if (!parser.isSet(minimized)) window.show();
    return app.exec();
}
