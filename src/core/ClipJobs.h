#pragma once
#include "core/Clip.h"
#include "core/Exporter.h"
#include "core/Settings.h"
#include "core/Uploader.h"

#include <QObject>

class ClipLibrary;

// Runs one export or share job at a time and reports progress for the UI.
class ClipJobs : public QObject {
    Q_OBJECT
public:
    ClipJobs(ClipLibrary *library, QObject *parent = nullptr);

    bool busy() const { return m_busy; }
    QString activeClip() const { return m_clipPath; }
    Uploader *uploader() { return &m_uploader; }

    // Render the clip's edits to a new file in the library.
    bool exportClip(const Clip &clip, const Settings &s);
    // Render if needed (so every browser plays the intended mix), then upload.
    bool shareClip(const Clip &clip, const Settings &s);
    void cancel();

    static QString uniqueOutputPath(const QString &sourcePath);

signals:
    void busyChanged(bool busy);
    void progress(const QString &clipPath, const QString &stage, double fraction);
    void exported(const QString &clipPath, const QString &outPath);
    void shared(const QString &clipPath, const QString &url);
    void failed(const QString &clipPath, const QString &message);

private:
    void setBusy(bool b);
    void startUpload(const QString &file);
    void finish();

    ClipLibrary *m_library;
    Exporter m_exporter;
    Uploader m_uploader;
    bool m_busy = false;
    bool m_sharing = false;
    QString m_clipPath;
    QString m_title;
    QString m_tempFile;
    QString m_apiKey;
    QStringList m_trackNames;
};
