#include "core/ClipJobs.h"

#include "core/ClipLibrary.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUuid>

ClipJobs::ClipJobs(ClipLibrary *library, QObject *parent) : QObject(parent), m_library(library)
{
    connect(&m_exporter, &Exporter::progress, this, [this](double f) {
        emit progress(m_clipPath, m_sharing ? "Preparing" : "Exporting", f);
    });
    connect(&m_exporter, &Exporter::finished, this,
            [this](bool ok, const QString &out, const QString &error) {
        if (!ok) {
            const QString path = m_clipPath;
            finish();
            emit failed(path, error);
            return;
        }
        if (m_sharing) {
            startUpload(out);
            return;
        }
        const QString src = m_clipPath;
        const QString title = m_title;
        finish();
        m_library->addFile(out);
        m_library->update(out, [&](Clip &c) { c.title = title + " (edit)"; });
        emit exported(src, out);
    });
    connect(&m_uploader, &Uploader::progress, this,
            [this](double f) { emit progress(m_clipPath, "Uploading", f); });
    connect(&m_uploader, &Uploader::finished, this,
            [this](bool ok, const QString &id, const QString &error) {
        const QString path = m_clipPath;
        finish();
        if (!ok) {
            emit failed(path, error);
            return;
        }
        const QString url = Uploader::viewUrl(id);
        m_library->update(path, [&](Clip &c) {
            c.shareId = id;
            c.shareUrl = url;
        });
        emit shared(path, url);
    });
}

QString ClipJobs::uniqueOutputPath(const QString &sourcePath)
{
    const QFileInfo fi(sourcePath);
    const QString base = fi.absolutePath() + "/" + fi.completeBaseName() + "_edit";
    QString out = base + ".mp4";
    for (int n = 2; QFileInfo::exists(out); ++n) out = QString("%1%2.mp4").arg(base).arg(n);
    return out;
}

void ClipJobs::setBusy(bool b)
{
    if (m_busy == b) return;
    m_busy = b;
    emit busyChanged(b);
}

void ClipJobs::finish()
{
    if (!m_tempFile.isEmpty()) QFile::remove(m_tempFile);
    m_tempFile.clear();
    m_clipPath.clear();
    m_sharing = false;
    setBusy(false);
}

bool ClipJobs::exportClip(const Clip &clip, const Settings &s)
{
    if (m_busy) return false;
    m_clipPath = clip.path;
    m_title = clip.title;
    m_sharing = false;
    setBusy(true);
    emit progress(m_clipPath, "Exporting", 0);
    m_exporter.start(clip, ExportOptions::fromSettings(s), uniqueOutputPath(clip.path));
    return true;
}

bool ClipJobs::shareClip(const Clip &clip, const Settings &s)
{
    if (m_busy) return false;
    m_clipPath = clip.path;
    m_title = clip.title;
    m_apiKey = s.pixeldrainKey;
    m_sharing = true;
    setBusy(true);
    if (s.pixeldrainKey.trimmed().isEmpty()) {
        const QString path = m_clipPath;
        finish();
        emit failed(path, "No Pixeldrain API key set. Add one in Settings → Sharing.");
        return false;
    }
    if (clip.needsRender()) {
        // Render next to the library's metadata so the temp file lands on the
        // same (usually roomy) drive as the clips rather than a small tmpfs.
        const QString dir = m_library->metaDir() + "/tmp";
        QDir().mkpath(dir);
        m_tempFile = dir + "/share-" + QUuid::createUuid().toString(QUuid::Id128) + ".mp4";
        emit progress(m_clipPath, "Preparing", 0);
        // Sharing always stream-copies video: it is fast and lossless.
        ExportOptions o = ExportOptions::fromSettings(s);
        o.reencodeVideo = false;
        m_exporter.start(clip, o, m_tempFile);
    } else {
        startUpload(clip.path);
    }
    return true;
}

void ClipJobs::startUpload(const QString &file)
{
    emit progress(m_clipPath, "Uploading", 0);
    m_uploader.upload(file, Uploader::sanitizeName(m_title), m_apiKey);
}

void ClipJobs::cancel()
{
    if (!m_busy) return;
    if (m_exporter.isRunning()) m_exporter.cancel();
    else m_uploader.cancel();
}
