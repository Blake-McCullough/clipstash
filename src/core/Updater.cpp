#include "core/Updater.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QStandardPaths>
#include <QVersionNumber>

#include <cstdio>

Updater::Updater(QObject *parent)
    : QObject(parent), m_apiUrl("https://api.github.com/repos/Blake-McCullough/clipstash/releases/latest")
{
    // Lets a fork or a test point the updater somewhere else.
    const QString override = qEnvironmentVariable("CLIPSTASH_UPDATE_API");
    if (!override.isEmpty()) m_apiUrl = override;
}

bool Updater::isNewer(const QString &candidate, const QString &current)
{
    auto parse = [](QString v) {
        if (v.startsWith('v') || v.startsWith('V')) v.remove(0, 1);
        return QVersionNumber::fromString(v);
    };
    const QVersionNumber a = parse(candidate), b = parse(current);
    return !a.isNull() && QVersionNumber::compare(a, b) > 0;
}

UpdateInfo Updater::parseRelease(const QByteArray &json, const QString &currentVersion)
{
    UpdateInfo info;
    const QJsonObject o = QJsonDocument::fromJson(json).object();
    QString tag = o["tag_name"].toString();
    if (tag.startsWith('v') || tag.startsWith('V')) tag.remove(0, 1);
    info.version = tag;
    info.notes = o["body"].toString();
    info.pageUrl = o["html_url"].toString();
    for (const QJsonValue &v : o["assets"].toArray()) {
        const QJsonObject a = v.toObject();
        if (a["name"].toString() != assetName()) continue;
        info.downloadUrl = a["browser_download_url"].toString();
        const QString digest = a["digest"].toString();
        if (digest.startsWith("sha256:")) info.sha256 = digest.mid(7).toLower();
    }
    info.newer = isNewer(info.version, currentVersion);
    return info;
}

void Updater::fail(const QString &message)
{
    m_busy = false;
    emit failed(message);
}

void Updater::check()
{
    if (m_busy) return;
    m_busy = true;
    QNetworkRequest req{QUrl(m_apiUrl)};
    req.setHeader(QNetworkRequest::UserAgentHeader, "clipstash/" CLIPSTASH_VERSION);
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setTransferTimeout(15000);
    QNetworkReply *reply = m_nam.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 404) {
            // Repository has no published release yet.
            m_busy = false;
            m_latest = UpdateInfo{};
            emit checked(m_latest);
            return;
        }
        if (reply->error() != QNetworkReply::NoError || status != 200) {
            fail("Could not check for updates: " + reply->errorString());
            return;
        }
        m_latest = parseRelease(reply->readAll(), CLIPSTASH_VERSION);
        m_busy = false;
        emit checked(m_latest);
    });
}

void Updater::downloadAndInstall()
{
    if (m_busy) return;
    if (!m_latest.newer || m_latest.downloadUrl.isEmpty()) {
        emit failed("This release has no downloadable build. Update with git pull instead.");
        return;
    }
    m_busy = true;

    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/update";
    QDir().mkpath(dir);
    const QString path = dir + "/clipstash-" + m_latest.version;
    auto *file = new QFile(path, this);
    if (!file->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        const QString err = file->errorString();
        file->deleteLater();
        fail("Cannot write the download: " + err);
        return;
    }

    QNetworkRequest req{QUrl(m_latest.downloadUrl)};
    req.setHeader(QNetworkRequest::UserAgentHeader, "clipstash/" CLIPSTASH_VERSION);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    m_reply = m_nam.get(req);
    emit progress("Downloading", 0);
    connect(m_reply, &QNetworkReply::readyRead, this, [this, file] { file->write(m_reply->readAll()); });
    connect(m_reply, &QNetworkReply::downloadProgress, this, [this](qint64 got, qint64 total) {
        if (total > 0) emit progress("Downloading", double(got) / double(total));
    });
    connect(m_reply, &QNetworkReply::finished, this, [this, file, path] {
        QNetworkReply *reply = m_reply;
        reply->deleteLater();
        file->write(reply->readAll());
        file->close();
        file->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError || status != 200) {
            QFile::remove(path);
            fail("Download failed: " + reply->errorString());
            return;
        }
        verifyAndInstall(path);
    });
}

void Updater::verifyAndInstall(const QString &file)
{
    emit progress("Verifying", 1);
    if (!m_latest.sha256.isEmpty()) {
        QFile f(file);
        QCryptographicHash hash(QCryptographicHash::Sha256);
        if (!f.open(QIODevice::ReadOnly) || !hash.addData(&f)
            || hash.result().toHex() != m_latest.sha256.toLatin1()) {
            QFile::remove(file);
            fail("The download did not match the published checksum. Nothing was installed.");
            return;
        }
    }
    QFile::setPermissions(file, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner
                                    | QFileDevice::ReadGroup | QFileDevice::ExeGroup
                                    | QFileDevice::ReadOther | QFileDevice::ExeOther);

    // Make sure the build actually runs on this system before replacing anything.
    auto *probe = new QProcess(this);
    connect(probe, &QProcess::finished, this, [this, probe, file](int code, QProcess::ExitStatus st) {
        const QString out = QString::fromUtf8(probe->readAllStandardOutput());
        probe->deleteLater();
        if (st != QProcess::NormalExit || code != 0 || !out.contains(m_latest.version)) {
            QFile::remove(file);
            fail("The downloaded build does not run on this system (library mismatch). "
                 "Update with git pull and rebuild instead.");
            return;
        }
        install(file);
    });
    connect(probe, &QProcess::errorOccurred, this, [this, probe, file](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart) return;
        probe->deleteLater();
        QFile::remove(file);
        fail("The downloaded build could not be started.");
    });
    probe->start(file, {"--version"});
}

void Updater::install(const QString &file)
{
    emit progress("Installing", 1);
    const QString target = QFileInfo(QCoreApplication::applicationFilePath()).canonicalFilePath();
    const QFileInfo targetDir(QFileInfo(target).absolutePath());
    const QString version = m_latest.version;

    if (targetDir.isWritable()) {
        // Our own folder (e.g. ~/.local/bin): swap the file in place.
        const QString staged = target + ".new";
        QFile::remove(staged);
        if (!QFile::copy(file, staged)
            || !QFile::setPermissions(staged, QFile::permissions(file))
            || ::rename(staged.toLocal8Bit().constData(), target.toLocal8Bit().constData()) != 0) {
            QFile::remove(staged);
            fail("Could not replace " + target);
            return;
        }
        QFile::remove(file);
        m_busy = false;
        emit installed(version);
        return;
    }

    // System folder (e.g. /usr/bin): ask for admin rights through polkit.
    auto *proc = new QProcess(this);
    connect(proc, &QProcess::finished, this, [this, proc, file, version](int code, QProcess::ExitStatus st) {
        proc->deleteLater();
        if (st == QProcess::NormalExit && code == 0) {
            QFile::remove(file);
            m_busy = false;
            emit installed(version);
        } else if (code == 126 || code == 127) {
            fail("Update cancelled: admin authorisation was not given.");
        } else {
            fail("Installing the update failed.");
        }
    });
    connect(proc, &QProcess::errorOccurred, this, [this, proc](QProcess::ProcessError e) {
        if (e != QProcess::FailedToStart) return;
        proc->deleteLater();
        fail("pkexec is not available, so the update cannot be installed to a system folder.");
    });
    proc->start("pkexec", {"install", "-m", "755", file, target});
}
