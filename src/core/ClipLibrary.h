#pragma once
#include "core/Clip.h"

#include <QAbstractListModel>
#include <QFileSystemWatcher>
#include <QHash>
#include <QPixmap>
#include <QSet>
#include <QTimer>
#include <functional>

// The clip folder as a list model. Media files stay plain files; titles,
// edit state and share links live in <library>/.clipstash/ next to them so the
// whole library can move with the drive.
class ClipLibrary : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles { ClipRole = Qt::UserRole + 1, ThumbRole, PathRole };

    explicit ClipLibrary(QObject *parent = nullptr);

    void setDirectory(const QString &dir);
    QString directory() const { return m_dir; }

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;

    int rowForPath(const QString &path) const;
    const Clip *clip(const QString &path) const;
    const Clip &clipAt(int row) const { return m_clips[row]; }
    qint64 totalSize() const;

    // Adds (or refreshes) a clip; trackNames label the audio streams of a
    // freshly recorded clip using the recorder's track configuration.
    void addFile(const QString &path, const QStringList &trackNames = {});
    void update(const QString &path, const std::function<void(Clip &)> &fn);
    bool remove(const QString &path);
    QString filmstripPath(const Clip &c) const;
    QString metaDir() const;

public slots:
    void rescan();

signals:
    void clipAdded(const QString &path);
    void clipChanged(const QString &path);

private:
    QString keyFor(const QString &path) const;
    QString sidecarPath(const QString &path) const;
    QString thumbPath(const QString &path) const;
    void loadSidecar(Clip &c) const;
    void saveSidecar(const Clip &c) const;
    void ensureMedia(const QString &path);
    Clip makeClip(const QString &path) const;

    QString m_dir;
    QList<Clip> m_clips;
    mutable QHash<QString, QPixmap> m_thumbs;
    QSet<QString> m_probing;
    QSet<QString> m_thumbing;
    QHash<QString, int> m_probeAttempts;
    QFileSystemWatcher m_watcher;
    QTimer m_rescanTimer;
};
