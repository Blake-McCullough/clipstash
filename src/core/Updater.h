#pragma once
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QString>

class QNetworkReply;
class QFile;

struct UpdateInfo {
    QString version;      // "0.3.0"
    QString notes;        // release body
    QString pageUrl;      // release page
    QString downloadUrl;  // prebuilt binary asset, empty if the release has none
    QString sha256;       // hex digest of the asset when GitHub provides one
    bool newer = false;   // newer than the running build
};

// Self-update from GitHub releases: check the latest release, download the
// prebuilt binary, verify it, and put it in place of the running executable
// (asking for admin rights through polkit when it lives in a system folder).
class Updater : public QObject {
    Q_OBJECT
public:
    explicit Updater(QObject *parent = nullptr);

    static UpdateInfo parseRelease(const QByteArray &json, const QString &currentVersion);
    static bool isNewer(const QString &candidate, const QString &current);
    static QString assetName() { return "clipstash-x86_64"; }

    bool busy() const { return m_busy; }
    const UpdateInfo &latest() const { return m_latest; }
    // For tests / forks; defaults to the project's GitHub repository.
    void setApiUrl(const QString &url) { m_apiUrl = url; }

public slots:
    void check();
    void downloadAndInstall();

signals:
    void checked(const UpdateInfo &info);
    void progress(const QString &stage, double fraction);
    void installed(const QString &version);
    void failed(const QString &message);

private:
    void fail(const QString &message);
    void verifyAndInstall(const QString &file);
    void install(const QString &file);

    QNetworkAccessManager m_nam;
    QPointer<QNetworkReply> m_reply;
    QString m_apiUrl;
    UpdateInfo m_latest;
    bool m_busy = false;
};
