#pragma once
#include "core/Updater.h"
#include "ui/ClipSound.h"
#include "core/ClipJobs.h"
#include "core/ClipLibrary.h"
#include "core/Recorder.h"
#include "core/Settings.h"
#include "hotkeys/HotkeyManager.h"

#include <QHash>
#include <QTimer>
#include <QMainWindow>
#include <QSystemTrayIcon>

class EditorPage;
class LibraryPage;
class SettingsPage;
class QAction;
class QLabel;
class QPushButton;
class QStackedWidget;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

    // Commands from the CLI / a second instance: show, save-clip [seconds],
    // toggle-recording, quit.
    void handleCommand(const QString &command);
    void quit();
    // Start the recorder even if "record when opened" is off (used by autostart).
    void ensureRecording();
    void toggleFullRecording();

protected:
    void closeEvent(QCloseEvent *e) override;
    void hideEvent(QHideEvent *e) override;

private:
    void buildUi();
    void buildTray();
    void showPage(int index);
    void openClip(const QString &path);
    void saveClip(int seconds = 0);
    void toggleRecorder();
    void updateRecorderUi();
    void applySettings(const Settings &s);
    void deleteClips(const QStringList &paths);
    void shareClip(const QString &path);
    void exportClip(const QString &path, const QString &name = {}, bool openWhenDone = false);
    void convertClips(const QStringList &paths);
    void copyDiscordFiles();
    void notify(const QString &title, const QString &text);
    void writeAutostart(bool enabled);
    void showWindow();
    void setupUpdater();
    void startRecorderWhenReady();
    void checkForNewAudioApps();
    void restartApp();

    Settings m_settings;
    Recorder m_recorder;
    ClipLibrary m_library;
    ClipJobs m_jobs{&m_library};
    HotkeyManager m_hotkeys;
    ClipSound m_sound;
    Updater m_updater;
    enum class UpdateState { Idle, Available, Installed } m_updateState = UpdateState::Idle;
    bool m_updateCheckManual = false;
    QPushButton *m_updateNav = nullptr;

    QStackedWidget *m_stack = nullptr;
    LibraryPage *m_libraryPage = nullptr;
    EditorPage *m_editor = nullptr;
    SettingsPage *m_settingsPage = nullptr;
    QPushButton *m_navClips = nullptr;
    QPushButton *m_navSettings = nullptr;
    QLabel *m_recDot = nullptr;
    QLabel *m_recText = nullptr;
    QLabel *m_recDetail = nullptr;
    QPushButton *m_clipNow = nullptr;
    QPushButton *m_recToggle = nullptr;
    QSystemTrayIcon *m_tray = nullptr;
    QAction *m_trayToggle = nullptr;
    QAction *m_trayClip = nullptr;
    QString m_lastNotifiedLink;
    QTimer m_folderWait;
    int m_folderWaitTries = 0;
    QTimer m_appWatch;
    QTimer m_fullRecTick;
    QString m_openAfterExport;  // source clip of an editor "Save as new clip"
    QString m_pendingOpen;      // new clip to open once it has been probed
    QString m_pendingOpenNote;  // status shown when it opens
    QStringList m_discordFiles; // Discord copies waiting to go on the clipboard
    QPushButton *m_fullRec = nullptr;
    QAction *m_trayFullRec = nullptr;
    QHash<QString, int> m_expectedLength;
    QList<AudioTrackCfg> m_recordedTracks;
    bool m_quitting = false;
};
