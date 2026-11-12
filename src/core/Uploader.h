#pragma once
#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>

class QFile;
class QNetworkReply;

// Pixeldrain uploads: PUT /api/file/{name}, API key as the Basic-auth password.
class Uploader : public QObject {
    Q_OBJECT
public:
    explicit Uploader(QObject *parent = nullptr);

    void upload(const QString &filePath, const QString &remoteName, const QString &apiKey);
    void cancel();
    bool isRunning() const { return !m_reply.isNull(); }

    static QString viewUrl(const QString &id) { return "https://pixeldrain.com/u/" + id; }
    static QString sanitizeName(const QString &title);
    // For tests / self-hosting; defaults to https://pixeldrain.com/api
    void setApiBase(const QString &base) { m_base = base; }

signals:
    void progress(double fraction);
    void finished(bool ok, const QString &id, const QString &error);

private:
    QNetworkAccessManager m_nam;
    QPointer<QNetworkReply> m_reply;
    QString m_base = "https://pixeldrain.com/api";
    bool m_cancelled = false;
};
