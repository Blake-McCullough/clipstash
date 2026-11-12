#include "hotkeys/PortalHotkeys.h"
#include "ui/MainWindow.h"
#include "ui/Theme.h"

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

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("clipstash");
    QCoreApplication::setApplicationName("clipstash");
    QCoreApplication::setApplicationVersion(CLIPSTASH_VERSION);
    QGuiApplication::setDesktopFileName(kAppId);
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
    parser.addOptions({saveClip, seconds, toggle, minimized, quit});
    parser.process(app);

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
