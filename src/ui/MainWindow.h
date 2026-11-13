#pragma once
#include "core/Updater.h"
#include "ui/ClipSound.h"
#include "core/ClipJobs.h"
#include "core/ClipLibrary.h"
#include "core/Recorder.h"
#include "core/Settings.h"
#include "hotkeys/HotkeyManager.h"

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

protected:
    void closeEvent(QCloseEvent *e) override;

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
    void exportClip(const QString &path);
    void notify(const QString &title, const QString &text);
    void writeAutostart(bool enabled);
    void showWindow();
    void setupUpdater();
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
    bool m_quitting = false;
};
