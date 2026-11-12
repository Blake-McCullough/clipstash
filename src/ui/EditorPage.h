#pragma once
#include "core/Clip.h"

#include <QTimer>
#include <QWidget>

class ClipLibrary;
class MpvWidget;
class Timeline;
class QCheckBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QSlider;
class QVBoxLayout;

class EditorPage : public QWidget {
    Q_OBJECT
public:
    explicit EditorPage(ClipLibrary *library, QWidget *parent = nullptr);

    void open(const QString &path);
    void close();
    QString currentPath() const { return m_clip.path; }
    void commit();  // flush pending edits to the library

    void setJobProgress(const QString &path, const QString &stage, double fraction);
    void setJobIdle(const QString &message = {}, bool error = false);

signals:
    void backRequested();
    void exportRequested(const QString &path);
    void shareRequested(const QString &path);
    void cancelJobRequested();
    void deleteRequested(const QString &path);

private:
    void buildTrackRows();
    void refreshInfo();
    void refreshShare();
    void scheduleSave();
    void togglePlay();
    void setTrimPoint(bool start);
    void loadFilmstrip();
    void onLibraryChanged(const QString &path);

    ClipLibrary *m_library;
    Clip m_clip;
    bool m_dirty = false;
    bool m_saving = false;
    QTimer m_saveTimer;

    MpvWidget *m_player;
    Timeline *m_timeline;
    QLineEdit *m_title;
    QPushButton *m_play;
    QPushButton *m_fav;
    QLabel *m_time;
    QLabel *m_info;
    QVBoxLayout *m_trackLayout;
    QWidget *m_trackHost;
    QLabel *m_noAudio;
    QPushButton *m_export;
    QPushButton *m_share;
    QPushButton *m_cancel;
    QWidget *m_linkRow;
    QLineEdit *m_link;
    QProgressBar *m_progress;
    QLabel *m_status;
};
