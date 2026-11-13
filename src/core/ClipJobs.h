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
    // Re-encode the file itself to H.264 in place (same name, date, tracks and
    // edit state), as if it had been recorded that way.
    bool convertClip(const Clip &clip, const Settings &s);
    // Small H.264 copy sized to fit Discord's upload limit (trim and mix applied).
    bool discordClip(const Clip &clip, const Settings &s);
    void discordClips(const QStringList &paths, const Settings &s);
    // Run exportClip / convertClip over several clips, one after another.
    void exportClips(const QStringList &paths, const Settings &s);
    void convertClips(const QStringList &paths, const Settings &s);
    void cancel();

    static QString uniqueOutputPath(const QString &sourcePath);

signals:
    void busyChanged(bool busy);
    void progress(const QString &clipPath, const QString &stage, double fraction);
    void exported(const QString &clipPath, const QString &outPath);
    void shared(const QString &clipPath, const QString &url);
    void failed(const QString &clipPath, const QString &message);
    void converted(const QString &clipPath);
    void discordReady(const QString &clipPath, const QString &outPath);
    // A multi-clip run ended. `what` is "Exported" or "Converted".
    void batchFinished(const QString &what, int succeeded, int failed, const QString &lastError);

private:
    void setBusy(bool b);
    void startUpload(const QString &file);
    void finish();
    void fail(const QString &path, const QString &message);
    enum class Kind { Export, Convert, Discord };
    void startBatch(const QStringList &paths, const Settings &s, Kind kind);
    static QString uniquePath(const QString &sourcePath, const QString &suffix);
    void nextInBatch();
    QString stageLabel(const QString &base) const;
    bool replaceOriginal(const QString &rendered, const QString &original, QString *error);

    ClipLibrary *m_library;
    Exporter m_exporter;
    Uploader m_uploader;
    bool m_busy = false;
    bool m_sharing = false;
    bool m_converting = false;
    // batch state
    QStringList m_queue;
    Settings m_batchSettings;
    bool m_batch = false;
    Kind m_batchKind = Kind::Export;
    bool m_discord = false;
    int m_discordAttempt = 0;
    Clip m_discordClip;
    Settings m_discordSettings;
    int m_batchTotal = 0;
    int m_batchOk = 0;
    int m_batchFailed = 0;
    QString m_batchError;
    QString m_clipPath;
    QString m_title;
    QString m_tempFile;
    QString m_apiKey;
    QStringList m_trackNames;
};
