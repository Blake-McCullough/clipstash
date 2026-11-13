#include "core/ClipJobs.h"

#include "core/ClipLibrary.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUuid>

#include <cstdio>

ClipJobs::ClipJobs(ClipLibrary *library, QObject *parent) : QObject(parent), m_library(library)
{
    // Lets tests (or a self-hosted instance) point uploads somewhere else.
    const QString apiBase = qEnvironmentVariable("CLIPSTASH_PIXELDRAIN_API");
    if (!apiBase.isEmpty()) m_uploader.setApiBase(apiBase);

    connect(&m_exporter, &Exporter::progress, this, [this](double f) {
        emit progress(m_clipPath, stageLabel(m_sharing ? "Preparing" : m_converting ? "Converting" : "Exporting"), f);
    });
    connect(&m_exporter, &Exporter::finished, this,
            [this](bool ok, const QString &out, const QString &error) {
        if (!ok) {
            fail(m_clipPath, error);
            return;
        }
        if (m_sharing) {
            startUpload(out);
            return;
        }
        const QString src = m_clipPath;
        const QString title = m_title;
        if (m_converting) {
            QString why;
            if (!replaceOriginal(out, src, &why)) {
                fail(src, why);
                return;
            }
            m_tempFile.clear();  // it is the clip now
            finish();
            m_library->update(src, [&](Clip &c) {
                c.videoCodec = "h264";
                c.size = QFileInfo(src).size();
            });
            emit converted(src);
        } else {
            finish();
            m_library->addFile(out);
            m_library->update(out, [&](Clip &c) { c.title = title + " (edit)"; });
            emit exported(src, out);
        }
        if (m_batch) {
            ++m_batchOk;
            nextInBatch();
        }
    });
    connect(&m_uploader, &Uploader::progress, this,
            [this](double f) { emit progress(m_clipPath, "Uploading", f); });
    connect(&m_uploader, &Uploader::finished, this,
            [this](bool ok, const QString &id, const QString &error) {
        const QString path = m_clipPath;
        if (!ok) {
            fail(path, error);
            return;
        }
        finish();
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
    m_converting = false;
    setBusy(false);
}

void ClipJobs::fail(const QString &clipPath, const QString &message)
{
    const QString path = clipPath;  // callers pass m_clipPath, which finish() clears
    finish();
    if (!m_batch) {
        emit failed(path, message);
        return;
    }
    // In a batch one bad clip should not stop the rest or pop up a dialog each time.
    if (message == "Cancelled") {
        m_queue.clear();
    } else {
        ++m_batchFailed;
        m_batchError = message;
    }
    nextInBatch();
}

QString ClipJobs::stageLabel(const QString &base) const
{
    if (!m_batch || m_batchTotal < 2) return base;
    return QString("%1 %2/%3").arg(base).arg(m_batchTotal - m_queue.size()).arg(m_batchTotal);
}

bool ClipJobs::replaceOriginal(const QString &rendered, const QString &original, QString *error)
{
    if (QFileInfo(rendered).size() <= 0) {
        *error = "The converted file came out empty; the original was left alone.";
        return false;
    }
    const QDateTime modified = QFileInfo(original).lastModified();
    // rename() swaps the file in one step, so there is never a moment without a clip.
    if (::rename(rendered.toLocal8Bit().constData(), original.toLocal8Bit().constData()) != 0) {
        *error = "Could not replace the original file; it was left alone.";
        return false;
    }
    // Keep the recording date so the clip stays where it was in the library.
    QFile f(original);
    if (f.open(QIODevice::ReadWrite)) f.setFileTime(modified, QFileDevice::FileModificationTime);
    return true;
}

bool ClipJobs::convertClip(const Clip &clip, const Settings &s)
{
    if (m_busy) return false;
    m_clipPath = clip.path;
    m_title = clip.title;
    m_converting = true;
    setBusy(true);

    ExportOptions o = ExportOptions::fromSettings(s);
    o.reencodeVideo = true;
    o.wholeFile = true;
    o.maxHeight = 0;  // never shrink someone's originals
    if (o.encoder == "auto") o.encoder = ExportOptions::autoEncoder();

    const QString dir = m_library->metaDir() + "/tmp";
    QDir().mkpath(dir);
    m_tempFile = dir + "/convert-" + QUuid::createUuid().toString(QUuid::Id128) + ".mp4";
    emit progress(m_clipPath, stageLabel("Converting"), 0);
    m_exporter.start(clip, o, m_tempFile);
    return true;
}

void ClipJobs::exportClips(const QStringList &paths, const Settings &s) { startBatch(paths, s, false); }
void ClipJobs::convertClips(const QStringList &paths, const Settings &s) { startBatch(paths, s, true); }

void ClipJobs::startBatch(const QStringList &paths, const Settings &s, bool convert)
{
    if (m_busy || m_batch || paths.isEmpty()) return;
    m_queue = paths;
    m_batchSettings = s;
    m_batch = true;
    m_batchConvert = convert;
    m_batchTotal = paths.size();
    m_batchOk = m_batchFailed = 0;
    m_batchError.clear();
    nextInBatch();
}

void ClipJobs::nextInBatch()
{
    while (!m_queue.isEmpty()) {
        const QString path = m_queue.takeFirst();
        const Clip *c = m_library->clip(path);
        if (!c) continue;  // deleted meanwhile
        if (m_batchConvert ? convertClip(*c, m_batchSettings) : exportClip(*c, m_batchSettings)) return;
    }
    m_batch = false;
    emit batchFinished(m_batchConvert ? "Converted" : "Exported", m_batchOk, m_batchFailed, m_batchError);
}

bool ClipJobs::exportClip(const Clip &clip, const Settings &s)
{
    if (m_busy) return false;
    m_clipPath = clip.path;
    m_title = clip.title;
    m_sharing = false;
    setBusy(true);
    emit progress(m_clipPath, stageLabel("Exporting"), 0);
    m_exporter.start(clip, ExportOptions::forClip(s, clip, false), uniqueOutputPath(clip.path));
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
        fail(m_clipPath, "No Pixeldrain API key set. Add one in Settings → Sharing.");
        return false;
    }
    // Video is stream-copied (fast, lossless) unless the clip is HEVC/AV1,
    // which browsers and Discord cannot play; those are converted to H.264.
    const ExportOptions o = ExportOptions::forClip(s, clip, true);
    if (clip.needsRender() || o.reencodeVideo) {
        // Render next to the library's metadata so the temp file lands on the
        // same (usually roomy) drive as the clips rather than a small tmpfs.
        const QString dir = m_library->metaDir() + "/tmp";
        QDir().mkpath(dir);
        m_tempFile = dir + "/share-" + QUuid::createUuid().toString(QUuid::Id128) + ".mp4";
        emit progress(m_clipPath, "Preparing", 0);
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
    m_queue.clear();
    if (!m_busy) return;
    if (m_exporter.isRunning()) m_exporter.cancel();
    else m_uploader.cancel();
}
