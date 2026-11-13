#include "core/ClipLibrary.h"

#include "core/MediaTools.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLocale>
#include <QSaveFile>

static const QStringList kExtensions = {"mp4", "mkv", "webm", "mov", "m4v", "avi", "ts", "flv"};

ClipLibrary::ClipLibrary(QObject *parent) : QAbstractListModel(parent)
{
    m_rescanTimer.setSingleShot(true);
    m_rescanTimer.setInterval(800);
    connect(&m_rescanTimer, &QTimer::timeout, this, &ClipLibrary::rescan);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this,
            [this] { m_rescanTimer.start(); });
}

QString ClipLibrary::metaDir() const { return m_dir + "/.clipstash"; }

QString ClipLibrary::keyFor(const QString &path) const
{
    QString rel = QDir(m_dir).relativeFilePath(path);
    return rel.replace('/', "__");
}

QString ClipLibrary::sidecarPath(const QString &path) const
{
    return metaDir() + "/meta/" + keyFor(path) + ".json";
}

QString ClipLibrary::thumbPath(const QString &path) const
{
    return metaDir() + "/thumbs/" + keyFor(path) + ".jpg";
}

QString ClipLibrary::filmstripPath(const Clip &c) const
{
    return metaDir() + "/thumbs/" + keyFor(c.path) + ".strip.jpg";
}

void ClipLibrary::setDirectory(const QString &dir)
{
    if (!m_watcher.directories().isEmpty()) m_watcher.removePaths(m_watcher.directories());
    m_dir = QDir::cleanPath(dir);
    QDir().mkpath(metaDir() + "/meta");
    QDir().mkpath(metaDir() + "/thumbs");
    if (QFileInfo(m_dir).isDir()) m_watcher.addPath(m_dir);
    beginResetModel();
    m_clips.clear();
    m_thumbs.clear();
    endResetModel();
    rescan();
}

int ClipLibrary::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_clips.size();
}

QVariant ClipLibrary::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_clips.size()) return {};
    const Clip &c = m_clips[index.row()];
    switch (role) {
    case Qt::DisplayRole: return c.title;
    case PathRole: return c.path;
    case ClipRole: return QVariant::fromValue(c);
    case ThumbRole: {
        auto it = m_thumbs.find(c.path);
        if (it == m_thumbs.end()) {
            QPixmap pm;
            const QString tp = thumbPath(c.path);
            if (QFileInfo::exists(tp)) pm.load(tp);
            it = m_thumbs.insert(c.path, pm);
        }
        return *it;
    }
    }
    return {};
}

int ClipLibrary::rowForPath(const QString &path) const
{
    for (int i = 0; i < m_clips.size(); ++i)
        if (m_clips[i].path == path) return i;
    return -1;
}

const Clip *ClipLibrary::clip(const QString &path) const
{
    const int row = rowForPath(path);
    return row < 0 ? nullptr : &m_clips[row];
}

qint64 ClipLibrary::totalSize() const
{
    qint64 total = 0;
    for (const Clip &c : m_clips) total += c.size;
    return total;
}

void ClipLibrary::loadSidecar(Clip &c) const
{
    QFile f(sidecarPath(c.path));
    if (!f.open(QIODevice::ReadOnly)) return;
    c.applyJson(QJsonDocument::fromJson(f.readAll()).object());
}

void ClipLibrary::saveSidecar(const Clip &c) const
{
    QDir().mkpath(metaDir() + "/meta");
    QSaveFile f(sidecarPath(c.path));
    if (!f.open(QIODevice::WriteOnly)) return;
    f.write(QJsonDocument(c.toJson()).toJson(QJsonDocument::Compact));
    f.commit();
}

Clip ClipLibrary::makeClip(const QString &path) const
{
    const QFileInfo fi(path);
    Clip c;
    c.path = fi.absoluteFilePath();
    c.created = fi.lastModified();
    c.size = fi.size();
    c.title = QLocale().toString(c.created, "d MMM yyyy, HH:mm:ss");
    loadSidecar(c);
    return c;
}

void ClipLibrary::rescan()
{
    if (m_dir.isEmpty()) return;
    QSet<QString> onDisk;
    QDirIterator it(m_dir, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString p = it.next();
        if (p.contains("/.clipstash/")) continue;
        if (!kExtensions.contains(QFileInfo(p).suffix().toLower())) continue;
        onDisk.insert(p);
    }
    for (int i = m_clips.size() - 1; i >= 0; --i) {
        if (onDisk.contains(m_clips[i].path)) continue;
        beginRemoveRows({}, i, i);
        m_thumbs.remove(m_clips[i].path);
        m_clips.removeAt(i);
        endRemoveRows();
    }
    for (const QString &p : std::as_const(onDisk))
        if (rowForPath(p) < 0) addFile(p);
}

void ClipLibrary::addFile(const QString &path, const QStringList &trackNames)
{
    const QString abs = QFileInfo(path).absoluteFilePath();
    int row = rowForPath(abs);
    if (row < 0) {
        Clip c = makeClip(abs);
        if (c.tracks.isEmpty())
            for (const QString &n : trackNames) c.tracks.append({n, true, 1.0});
        // newest first
        row = 0;
        while (row < m_clips.size() && m_clips[row].created > c.created) ++row;
        beginInsertRows({}, row, row);
        m_clips.insert(row, c);
        endInsertRows();
        emit clipAdded(abs);
    } else {
        // Already picked up by the folder watcher while it was being written.
        m_probeAttempts.remove(abs);
        Clip &c = m_clips[row];
        bool changed = false;
        for (int i = 0; i < trackNames.size(); ++i) {
            if (i >= c.tracks.size()) {
                if (c.probed) break;
                c.tracks.append({trackNames[i], true, 1.0});
                changed = true;
            } else if (c.tracks[i].name.isEmpty() || c.tracks[i].name.startsWith("Track ")) {
                c.tracks[i].name = trackNames[i];
                changed = true;
            }
        }
        if (changed) {
            saveSidecar(c);
            emit dataChanged(index(row), index(row));
            emit clipChanged(abs);
        }
    }
    ensureMedia(abs);
}

void ClipLibrary::ensureMedia(const QString &path)
{
    const Clip *c = clip(path);
    if (!c) return;

    if (!c->probed && !m_probing.contains(path) && m_probeAttempts.value(path) < 3) {
        m_probing.insert(path);
        ++m_probeAttempts[path];
        MediaTools::probe(path, [this, path](const ProbeResult &r) {
            m_probing.remove(path);
            if (!r.ok) {
                // File may still be being written (e.g. copied in); retry shortly.
                QTimer::singleShot(2000, this, [this, path] { ensureMedia(path); });
                return;
            }
            update(path, [&](Clip &c) {
                c.duration = r.duration;
                c.width = r.width;
                c.height = r.height;
                c.probed = true;
                c.size = QFileInfo(path).size();
                QList<ClipTrack> tracks;
                for (int i = 0; i < r.audioTitles.size(); ++i) {
                    ClipTrack t = i < c.tracks.size() ? c.tracks[i] : ClipTrack{};
                    if (t.name.isEmpty())
                        t.name = !r.audioTitles[i].isEmpty() ? r.audioTitles[i]
                                                             : QString("Track %1").arg(i + 1);
                    tracks.append(t);
                }
                c.tracks = tracks;
            });
            ensureMedia(path);
        });
        return;
    }

    if (c->probed && !m_thumbing.contains(path) && !QFileInfo::exists(thumbPath(path))) {
        m_thumbing.insert(path);
        QDir().mkpath(metaDir() + "/thumbs");
        const double at = qMin(1.0, c->duration / 2);
        MediaTools::thumbnail(path, thumbPath(path), at, [this, path](bool ok) {
            // Keep the path in m_thumbing on failure so we don't retry forever.
            if (!ok) return;
            m_thumbing.remove(path);
            m_thumbs.remove(path);
            const int row = rowForPath(path);
            if (row >= 0) emit dataChanged(index(row), index(row), {ThumbRole});
        });
    }
}

void ClipLibrary::update(const QString &path, const std::function<void(Clip &)> &fn)
{
    const int row = rowForPath(path);
    if (row < 0) return;
    fn(m_clips[row]);
    saveSidecar(m_clips[row]);
    emit dataChanged(index(row), index(row));
    emit clipChanged(path);
}

bool ClipLibrary::remove(const QString &path)
{
    const int row = rowForPath(path);
    if (row < 0) return false;
    if (QFileInfo::exists(path) && !QFile::remove(path)) return false;
    QFile::remove(sidecarPath(path));
    QFile::remove(thumbPath(path));
    QFile::remove(filmstripPath(m_clips[row]));
    beginRemoveRows({}, row, row);
    m_thumbs.remove(path);
    m_clips.removeAt(row);
    endRemoveRows();
    return true;
}
