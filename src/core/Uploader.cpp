#include "core/Uploader.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QUrl>

Uploader::Uploader(QObject *parent) : QObject(parent) {}

QString Uploader::sanitizeName(const QString &title)
{
    QString n = title.trimmed();
    n.replace(QRegularExpression(R"([\\/:*?"<>|\x00-\x1f])"), "-");
    n.replace(QRegularExpression(R"(\s+)"), " ");
    if (n.isEmpty()) n = "clip";
    n.truncate(200);
    return n + ".mp4";
}

void Uploader::upload(const QString &filePath, const QString &remoteName, const QString &apiKey)
{
    if (isRunning()) return;
    m_cancelled = false;
    if (apiKey.trimmed().isEmpty()) {
        emit finished(false, {}, "No Pixeldrain API key set. Add one in Settings → Sharing.");
        return;
    }
    auto *file = new QFile(filePath);
    if (!file->open(QIODevice::ReadOnly)) {
        const QString err = file->errorString();
        delete file;
        emit finished(false, {}, "Cannot read clip: " + err);
        return;
    }

    QUrl url(m_base + "/file/" + QString::fromUtf8(QUrl::toPercentEncoding(remoteName)));
    QNetworkRequest req(url);
    req.setRawHeader("Authorization", "Basic " + (":" + apiKey.trimmed()).toUtf8().toBase64());
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/octet-stream");
    req.setHeader(QNetworkRequest::ContentLengthHeader, file->size());
    req.setHeader(QNetworkRequest::UserAgentHeader, "clipstash/" CLIPSTASH_VERSION);

    m_reply = m_nam.put(req, file);
    file->setParent(m_reply);
    connect(m_reply, &QNetworkReply::uploadProgress, this, [this](qint64 sent, qint64 total) {
        if (total > 0) emit progress(double(sent) / double(total));
    });
    connect(m_reply, &QNetworkReply::finished, this, [this] {
        QNetworkReply *r = m_reply;
        m_reply.clear();
        r->deleteLater();
        const int status = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QJsonObject body = QJsonDocument::fromJson(r->readAll()).object();
        const QString id = body["id"].toString();
        if (m_cancelled) {
            emit finished(false, {}, "Cancelled");
        } else if (status >= 200 && status < 300 && !id.isEmpty()) {
            emit finished(true, id, {});
        } else {
            QString msg = body["message"].toString();
            if (msg.isEmpty()) msg = body["value"].toString();
            if (status == 401 || status == 403)
                msg = "Pixeldrain rejected the API key" + (msg.isEmpty() ? QString() : " (" + msg + ")");
            if (msg.isEmpty()) msg = r->errorString();
            emit finished(false, {}, "Upload failed: " + msg);
        }
    });
}

void Uploader::cancel()
{
    if (!m_reply) return;
    m_cancelled = true;
    m_reply->abort();
}
