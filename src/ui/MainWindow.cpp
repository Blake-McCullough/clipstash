#include "ui/MainWindow.h"

#include "ui/EditorPage.h"
#include "ui/LibraryPage.h"
#include "ui/SettingsPage.h"
#include "ui/Theme.h"

#include <QMimeData>
#include "core/MediaTools.h"
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
#include <QHideEvent>
#include <QLocale>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QUrl>
#include <QVBoxLayout>

enum Page { PageLibrary, PageEditor, PageSettings };

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent)
{
    m_settings = Settings::load();
    setWindowTitle("Clipstash");
    setWindowIcon(Theme::appIcon());
    resize(1240, 780);
    setMinimumSize(940, 600);

    buildUi();
    buildTray();

    m_library.setDirectory(m_settings.libraryDir);
    m_settingsPage->setSettings(m_settings);

    // recorder
    connect(&m_recorder, &Recorder::stateChanged, this, [this](Recorder::State st) {
        if (st == Recorder::State::Starting) m_recordedTracks = m_settings.effectiveTracks();
    });
    connect(&m_recorder, &Recorder::stateChanged, this, &MainWindow::updateRecorderUi);
    connect(&m_recorder, &Recorder::clipSaved, this, [this](const QString &path, int expected, qint64) {
        QStringList names;
        const QList<AudioTrackCfg> tracks = m_recordedTracks;
        for (const AudioTrackCfg &t : tracks)
            if (!t.sources.trimmed().isEmpty()) names << t.name;
        m_expectedLength[QFileInfo(path).absoluteFilePath()] = expected;
        m_library.addFile(path, names);
        if (m_settings.notifyOnSave) notify("Clip saved", QFileInfo(path).fileName());
    });
    connect(&m_recorder, &Recorder::saveFailed, this, [this](const QString &msg) {
        notify("Clip not saved", msg);
    });
    // Once a new clip is probed, check it is as long as it should be, so a
    // short or truncated save is reported instead of discovered later.
    connect(&m_library, &ClipLibrary::clipChanged, this, [this](const QString &path) {
        auto it = m_expectedLength.find(path);
        if (it == m_expectedLength.end()) return;
        const Clip *c = m_library.clip(path);
        if (!c || !c->probed) return;
        const int expected = *it;
        m_expectedLength.erase(it);
        Recorder::log(QString("probed %1: %2 s (expected %3 s)").arg(QFileInfo(path).fileName())
                          .arg(c->duration, 0, 'f', 1).arg(expected));
        if (expected > 0 && c->duration < expected - 4)
            notify("Clip is shorter than expected",
                   QString("%1 is %2 long; about %3 was expected. Details are in the recorder log.")
                       .arg(c->title, formatDuration(c->duration), formatDuration(expected)));
    });
    connect(&m_recorder, &Recorder::failed, this, [this](const QString &msg) {
        notify("Recorder stopped", msg);
    });

    // hotkeys
    connect(&m_hotkeys, &HotkeyManager::activated, this, [this](const QString &id) {
        if (id == HotkeyIds::SaveClip) saveClip();
        else if (id == HotkeyIds::Save30) saveClip(30);
        else if (id == HotkeyIds::Save60) saveClip(60);
        else if (id == HotkeyIds::ToggleRecording) toggleRecorder();
        else if (id == HotkeyIds::ToggleFullRecording) toggleFullRecording();
    });
    connect(&m_hotkeys, &HotkeyManager::statusChanged, m_settingsPage, &SettingsPage::setHotkeyStatus);
    m_hotkeys.apply(m_settings);

    // jobs
    connect(&m_jobs, &ClipJobs::progress, this, [this](const QString &path, const QString &stage, double f) {
        m_libraryPage->setJobProgress(path, stage, f);
        m_editor->setJobProgress(path, stage, f);
    });
    connect(&m_jobs, &ClipJobs::exported, this, [this](const QString &src, const QString &out) {
        m_libraryPage->clearJobProgress();
        m_editor->setJobIdle("Saved as " + QFileInfo(out).fileName());
        // "Save as new clip" from the editor: carry on in the new clip, as
        // long as the user is still looking at the one it was made from.
        const bool follow = m_openAfterExport == src && m_stack->currentWidget() == m_editor
                            && m_editor->currentPath() == src;
        m_openAfterExport.clear();
        if (!follow) return;
        m_pendingOpen = out;
        const Clip *c = m_library.clip(out);
        if (c && c->probed) {
            m_pendingOpen.clear();
            openClip(out);
            m_editor->setJobIdle("Now editing the new clip. The original is unchanged.");
            return;
        }
        // Normally it opens as soon as it has been read (below); never leave
        // the user waiting on the old clip if that takes unusually long.
        QTimer::singleShot(6000, this, [this, out] {
            if (m_pendingOpen != out) return;
            m_pendingOpen.clear();
            if (m_stack->currentWidget() != m_editor || !m_library.clip(out)) return;
            openClip(out);
            m_editor->setJobIdle("Now editing the new clip. The original is unchanged.");
        });
    });
    // A new clip opens once its length and audio tracks are known.
    connect(&m_library, &ClipLibrary::clipChanged, this, [this](const QString &path) {
        if (m_pendingOpen.isEmpty() || path != m_pendingOpen) return;
        const Clip *c = m_library.clip(path);
        if (!c || !c->probed) return;
        m_pendingOpen.clear();
        if (m_stack->currentWidget() != m_editor) return;  // user moved on meanwhile
        const QString note = m_pendingOpenNote.isEmpty() ? QString("Now editing the new clip. The original is unchanged.")
                                                         : m_pendingOpenNote;
        m_pendingOpenNote.clear();
        openClip(path);
        m_editor->setJobIdle(note);
    });
    connect(&m_jobs, &ClipJobs::shared, this, [this](const QString &, const QString &url) {
        m_libraryPage->clearJobProgress();
        if (m_settings.copyLinkAfterUpload) QApplication::clipboard()->setText(url);
        m_editor->setJobIdle(m_settings.copyLinkAfterUpload ? "Uploaded. Link copied to the clipboard." : "Uploaded.");
        notify("Clip shared", url);
    });
    connect(&m_jobs, &ClipJobs::converted, this, [this](const QString &) {
        m_libraryPage->clearJobProgress();
        m_editor->setJobIdle("Converted to H.264.");
    });
    connect(&m_jobs, &ClipJobs::discordReady, this, [this](const QString &, const QString &out) {
        m_discordFiles << out;
        if (m_jobs.isBatchRunning()) return;  // copied together once the batch is done
        copyDiscordFiles();
    });
    connect(m_editor, &EditorPage::overwriteRequested, this, [this](const QString &path) {
        const Clip *c = m_library.clip(path);
        if (!c) return;
        if (m_jobs.busy()) {
            m_editor->setJobIdle("Another export or upload is still running.", true);
            return;
        }
        m_jobs.overwriteClip(*c, m_settings);
    });
    connect(&m_jobs, &ClipJobs::overwritten, this, [this](const QString &path) {
        m_libraryPage->clearJobProgress();
        // Reload the editor from the rewritten file once it has been read.
        if (m_stack->currentWidget() == m_editor && m_editor->currentPath() == path) {
            m_editor->discardPending();
            m_editor->close();
            m_pendingOpen = path;
            m_pendingOpenNote = "Saved.";
            QTimer::singleShot(6000, this, [this, path] {
                if (m_pendingOpen != path) return;
                m_pendingOpen.clear();
                if (m_stack->currentWidget() == m_editor) openClip(path);
            });
        }
        notify("Clip saved", QFileInfo(path).fileName());
    });
    connect(&m_jobs, &ClipJobs::batchFinished, this,
            [this](const QString &what, int ok, int failed, const QString &lastError) {
        m_libraryPage->clearJobProgress();
        if (!m_discordFiles.isEmpty()) copyDiscordFiles();
        if (ok + failed < 2 && failed == 0) return;  // a single clip already reported itself
        QString text = QString("%1 %2 clip%3.").arg(what).arg(ok).arg(ok == 1 ? "" : "s");
        if (failed > 0) text += QString(" %1 failed: %2").arg(failed).arg(lastError);
        notify("Clipstash", text);
        if (failed > 0) QMessageBox::warning(this, "Clipstash", text);
    });
    connect(&m_jobs, &ClipJobs::failed, this, [this](const QString &, const QString &msg) {
        m_libraryPage->clearJobProgress();
        m_editor->setJobIdle(msg, true);
        if (m_stack->currentIndex() != PageEditor && msg != "Cancelled")
            QMessageBox::warning(this, "Clipstash", msg);
    });

    updateRecorderUi();
    if (m_settings.recordOnLaunch) startRecorderWhenReady();
    if (m_settings.autostart) writeAutostart(true);  // keep the entry current across updates
    setupUpdater();

    // Auto split: notice apps that start playing sound and give them a track.
    m_appWatch.setInterval(10000);
    connect(&m_appWatch, &QTimer::timeout, this, &MainWindow::checkForNewAudioApps);
    m_appWatch.start();
}

void MainWindow::buildUi()
{
    auto *central = new QWidget;
    auto *root = new QHBoxLayout(central);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    setCentralWidget(central);

    // sidebar
    auto *sidebar = new QWidget;
    sidebar->setObjectName("sidebar");
    sidebar->setFixedWidth(212);
    auto *sl = new QVBoxLayout(sidebar);
    sl->setContentsMargins(12, 16, 12, 14);
    sl->setSpacing(4);

    auto *brandRow = new QHBoxLayout;
    auto *logo = new QLabel;
    logo->setPixmap(Theme::appIcon().pixmap(26, 26));
    auto *brand = new QLabel("Clipstash");
    brand->setObjectName("brand");
    brandRow->addWidget(logo);
    brandRow->addWidget(brand, 1);
    brandRow->setContentsMargins(6, 0, 0, 12);
    sl->addLayout(brandRow);

    m_navClips = new QPushButton("Clips");
    m_navSettings = new QPushButton("Settings");
    for (QPushButton *b : {m_navClips, m_navSettings}) {
        b->setObjectName("nav");
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        sl->addWidget(b);
    }
    sl->addStretch();

    m_updateNav = new QPushButton("Update available");
    m_updateNav->setObjectName("primary");
    m_updateNav->setCursor(Qt::PointingHandCursor);
    m_updateNav->hide();
    sl->addWidget(m_updateNav);
    sl->addSpacing(6);

    auto *recBox = new QFrame;
    recBox->setObjectName("panel");
    recBox->setStyleSheet("QFrame#panel { background: #1c2029; }");
    auto *rl = new QVBoxLayout(recBox);
    rl->setContentsMargins(12, 11, 12, 12);
    rl->setSpacing(7);
    auto *statusRow = new QHBoxLayout;
    m_recDot = new QLabel;
    m_recDot->setFixedSize(9, 9);
    m_recText = new QLabel;
    m_recText->setStyleSheet("font-weight: 600;");
    statusRow->addWidget(m_recDot);
    statusRow->addWidget(m_recText, 1);
    rl->addLayout(statusRow);
    m_recDetail = new QLabel;
    m_recDetail->setObjectName("muted");
    m_recDetail->setWordWrap(true);
    m_recDetail->setStyleSheet("font-size: 11px;");
    rl->addWidget(m_recDetail);
    m_clipNow = new QPushButton("Clip now");
    m_clipNow->setObjectName("primary");
    m_recToggle = new QPushButton;
    m_fullRec = new QPushButton("Start recording");
    m_fullRec->setToolTip("Record everything from now until you press it again, like a normal screen recording.\n"
                          "The replay buffer keeps running, so you can still clip at the same time.");
    rl->addWidget(m_clipNow);
    rl->addWidget(m_fullRec);
    rl->addWidget(m_recToggle);
    sl->addWidget(recBox);
    root->addWidget(sidebar);

    // pages
    m_stack = new QStackedWidget;
    m_libraryPage = new LibraryPage(&m_library);
    m_editor = new EditorPage(&m_library);
    m_settingsPage = new SettingsPage;
    m_stack->addWidget(m_libraryPage);
    m_stack->addWidget(m_editor);
    m_stack->addWidget(m_settingsPage);
    root->addWidget(m_stack, 1);

    connect(m_navClips, &QPushButton::clicked, this, [this] { showPage(PageLibrary); });
    connect(m_navSettings, &QPushButton::clicked, this, [this] { showPage(PageSettings); });
    connect(m_clipNow, &QPushButton::clicked, this, [this] { saveClip(); });
    connect(m_recToggle, &QPushButton::clicked, this, &MainWindow::toggleRecorder);
    connect(m_fullRec, &QPushButton::clicked, this, &MainWindow::toggleFullRecording);
    connect(&m_recorder, &Recorder::fullRecordingChanged, this, [this](bool on) {
        if (on) m_fullRecTick.start();
        else m_fullRecTick.stop();
        if (on && m_settings.soundOnSave) m_sound.play(m_settings.soundVolume);
        updateRecorderUi();
    });
    m_fullRecTick.setInterval(1000);
    connect(&m_fullRecTick, &QTimer::timeout, this, &MainWindow::updateRecorderUi);
    connect(&m_recorder, &Recorder::recordingSaved, this, [this](const QString &path) {
        QStringList names;
        for (const AudioTrackCfg &t : std::as_const(m_recordedTracks))
            if (!t.sources.trimmed().isEmpty()) names << t.name;
        m_library.addFile(path, names);
        const QDateTime at = QFileInfo(path).lastModified();
        m_library.update(path, [&](Clip &c) {
            if (c.title.isEmpty() || c.title.contains(':')) c.title = "Recording " + QLocale().toString(at, "d MMM, HH:mm");
        });
        notify("Recording saved", QFileInfo(path).fileName());
    });

    connect(m_libraryPage, &LibraryPage::openRequested, this, &MainWindow::openClip);
    connect(m_libraryPage, &LibraryPage::shareRequested, this, &MainWindow::shareClip);
    connect(m_libraryPage, &LibraryPage::deleteRequested, this, &MainWindow::deleteClips);
    connect(m_libraryPage, &LibraryPage::exportRequested, this, [this](const QStringList &paths) {
        if (m_jobs.busy()) {
            QMessageBox::information(this, "Clipstash", "Another export or upload is still running.");
            return;
        }
        m_jobs.exportClips(paths, m_settings);
    });
    connect(m_libraryPage, &LibraryPage::convertRequested, this, &MainWindow::convertClips);

    connect(m_editor, &EditorPage::backRequested, this, [this] { showPage(PageLibrary); });
    connect(m_editor, &EditorPage::shareRequested, this, &MainWindow::shareClip);
    connect(m_editor, &EditorPage::exportRequested, this, &MainWindow::exportClip);
    auto discord = [this](const QStringList &paths) {
        if (m_jobs.busy()) {
            m_editor->setJobIdle("Another export or upload is still running.", true);
            return;
        }
        m_jobs.discordClips(paths, m_settings);
    };
    connect(m_editor, &EditorPage::discordRequested, this, [discord](const QString &path) { discord({path}); });
    connect(m_libraryPage, &LibraryPage::discordRequested, this, discord);
    connect(m_editor, &EditorPage::cancelJobRequested, &m_jobs, &ClipJobs::cancel);
    connect(m_editor, &EditorPage::deleteRequested, this, [this](const QString &p) { deleteClips({p}); });

    connect(m_settingsPage, &SettingsPage::saveRequested, this, &MainWindow::applySettings);
    connect(m_settingsPage, &SettingsPage::testSoundRequested, &m_sound, &ClipSound::play);
    showPage(PageLibrary);
}

void MainWindow::buildTray()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable()) return;
    m_tray = new QSystemTrayIcon(Theme::appIcon(), this);
    auto *menu = new QMenu(this);
    m_trayClip = menu->addAction("Clip now", this, [this] { saveClip(); });
    m_trayFullRec = menu->addAction("Start recording", this, &MainWindow::toggleFullRecording);
    m_trayToggle = menu->addAction("Turn recorder off", this, &MainWindow::toggleRecorder);
    menu->addSeparator();
    menu->addAction("Open Clipstash", this, &MainWindow::showWindow);
    menu->addAction("Quit", this, &MainWindow::quit);
    m_tray->setContextMenu(menu);
    connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason r) {
        if (r != QSystemTrayIcon::Trigger) return;
        if (isVisible() && !isMinimized()) hide();
        else showWindow();
    });
    connect(m_tray, &QSystemTrayIcon::messageClicked, this, [this] {
        if (!m_lastNotifiedLink.isEmpty()) QDesktopServices::openUrl(QUrl(m_lastNotifiedLink));
        else showWindow();
    });
    m_tray->show();
}

void MainWindow::showWindow()
{
    show();
    setWindowState(windowState() & ~Qt::WindowMinimized);
    raise();
    activateWindow();
}

void MainWindow::showPage(int index)
{
    if (m_stack->currentIndex() == PageEditor && index != PageEditor) m_editor->close();
    m_stack->setCurrentIndex(index);
    m_navClips->setChecked(index == PageLibrary || index == PageEditor);
    m_navSettings->setChecked(index == PageSettings);
}

void MainWindow::openClip(const QString &path)
{
    if (!m_library.clip(path)) return;
    if (path != m_pendingOpen) m_pendingOpen.clear();
    showPage(PageEditor);
    m_editor->open(path);
    if (m_jobs.busy()) m_editor->setJobProgress(m_jobs.activeClip(), "Working", 0);
    else m_editor->setJobIdle();
}

void MainWindow::saveClip(int seconds)
{
    QString why;
    if (!m_recorder.saveReplay(seconds, &why)) {
        notify("Clip not saved", why);
        return;
    }
    // Chime the moment the clip is taken, like Medal. Writing the file can
    // take a second or two on a slow drive; a failure is reported separately.
    if (m_settings.soundOnSave) m_sound.play(m_settings.soundVolume);
}

void MainWindow::toggleRecorder()
{
    m_folderWait.stop();
    if (m_recorder.isActive()) m_recorder.stop();
    else m_recorder.start(m_settings);
}

void MainWindow::toggleFullRecording()
{
    const bool stopping = m_recorder.isFullRecording();
    QString why;
    if (!m_recorder.toggleFullRecording(&why)) {
        notify("Recording not started", why);
        return;
    }
    if (stopping) notify("Recording stopped", "Saving it to your clip folder…");
}

void MainWindow::startRecorderWhenReady()
{
    // At login a clip folder on another drive may not be mounted yet. Starting
    // the recorder then would fail, so wait for the folder to show up.
    if (QFileInfo(m_settings.libraryDir).isDir() || QDir().mkpath(m_settings.libraryDir)) {
        m_folderWait.stop();
        if (m_library.rowCount() == 0) m_library.setDirectory(m_settings.libraryDir);
        if (!m_recorder.isActive()) m_recorder.start(m_settings);
        return;
    }
    if (m_folderWait.isActive()) return;
    m_folderWaitTries = 0;
    m_folderWait.setInterval(3000);
    m_folderWait.disconnect(this);
    connect(&m_folderWait, &QTimer::timeout, this, [this] {
        if (++m_folderWaitTries > 200) {  // ten minutes
            m_folderWait.stop();
            notify("Clipstash", "The clip folder never appeared, so the recorder was not started.");
            return;
        }
        startRecorderWhenReady();
    });
    m_folderWait.start();
    m_recText->setText("Waiting for clip folder…");
    m_recDetail->setText(m_settings.libraryDir + " is not available yet.");
}

void MainWindow::ensureRecording()
{
    if (!m_recorder.isActive()) startRecorderWhenReady();
}

void MainWindow::checkForNewAudioApps()
{
    if (!m_settings.autoSplitAudio || m_recorder.state() != Recorder::State::Running) return;
    MediaTools::gsrList(m_settings.recorderBinary, "--list-application-audio",
                        [this](const MediaTools::NamedList &apps) {
        if (!m_settings.autoSplitAudio) return;
        QStringList added;
        for (const auto &entry : apps) {
            const QString name = entry.first.trimmed();
            // Our own chime and the recorder are not interesting.
            if (name.isEmpty() || name.compare("Clipstash", Qt::CaseInsensitive) == 0
                || name.contains("gpu-screen-recorder", Qt::CaseInsensitive) || name.contains('|'))
                continue;
            if (m_settings.knownApps.contains(name, Qt::CaseInsensitive)) continue;
            if (m_settings.knownApps.size() >= 12) break;  // keep clips to a sane number of tracks
            m_settings.knownApps << name;
            added << name;
        }
        if (added.isEmpty()) return;
        // Restarting the recorder would throw away the replay buffer, so the
        // new tracks are used from the next time the recorder starts.
        Recorder::log("auto split: noticed " + added.join(", ") + " (used from next recorder start)");
        m_settings.save();
        m_settingsPage->setSettings(m_settings);
    });
}

void MainWindow::updateRecorderUi()
{
    using S = Recorder::State;
    const S st = m_recorder.state();
    QColor dot = Theme::Muted;
    QString text, detail;
    switch (st) {
    case S::Running:
        dot = Theme::Bad;
        text = "Recording";
        detail = QString("Keeping the last %1 · %2 audio track%3")
                     .arg(formatDuration(m_settings.replaySeconds)).arg(m_settings.effectiveTracks().size())
                     .arg(m_settings.effectiveTracks().size() == 1 ? "" : "s");
        break;
    case S::Starting:
        dot = Theme::Warn;
        text = "Starting…";
        detail = m_settings.captureSource == "portal" ? "Pick a screen in the dialog if asked." : QString();
        break;
    case S::Failed:
        dot = Theme::Warn;
        text = "Recorder error";
        detail = m_recorder.lastError();
        break;
    case S::Stopped:
        text = "Recorder off";
        detail = "Nothing is being captured.";
        break;
    }
    m_recDot->setStyleSheet(QString("background: %1; border-radius: 4px;").arg(dot.name()));
    m_recText->setText(text);
    m_recDetail->setText(detail);
    m_recDetail->setVisible(!detail.isEmpty());
    m_recDetail->setToolTip(st == S::Failed ? detail : QString());
    const bool active = m_recorder.isActive();
    m_clipNow->setEnabled(st == S::Running);
    m_fullRec->setEnabled(st == S::Running);
    if (m_recorder.isFullRecording()) {
        m_fullRec->setText("Stop recording · " + formatDuration(m_recorder.fullRecordingMs() / 1000.0));
        m_fullRec->setStyleSheet("QPushButton { color: #ff5c6c; border-color: #ff5c6c; font-weight: 600; }");
        m_recText->setText("Recording everything");
        m_recDetail->setText("Started " + formatDuration(m_recorder.fullRecordingMs() / 1000.0)
                             + " ago. Clipping still works.");
    } else {
        m_fullRec->setText("Start recording");
        m_fullRec->setStyleSheet({});
    }
    if (m_trayFullRec) {
        m_trayFullRec->setText(m_recorder.isFullRecording() ? "Stop recording" : "Start recording");
        m_trayFullRec->setEnabled(st == S::Running);
    }
    m_recToggle->setText(active ? "Turn recorder off" : "Turn recorder on");
    if (m_tray) {
        m_tray->setIcon(Theme::appIcon(st == S::Running));
        m_tray->setToolTip("Clipstash – " + text);
        m_trayToggle->setText(active ? "Turn recorder off" : "Turn recorder on");
        m_trayClip->setEnabled(st == S::Running);
    }
}

void MainWindow::applySettings(const Settings &s)
{
    const Settings old = m_settings;
    m_settings = s;
    m_settings.save();
    m_settingsPage->setSettings(m_settings);  // show the cleaned-up values

    if (old.libraryDir != s.libraryDir) {
        if (m_stack->currentIndex() == PageEditor) showPage(PageLibrary);
        m_library.setDirectory(s.libraryDir);
    }
    // Compare with what the recorder is actually running, so apps noticed by
    // auto split since it started are picked up on this save too.
    const bool differs = m_recorder.isActive() ? Recorder::buildArgs(m_settings) != m_recorder.runningArgs()
                                               : s.recorderConfigDiffers(old);
    if (differs && m_recorder.isActive() && m_recorder.isFullRecording())
        notify("Recording stopped", "The recorder restarted for the new settings, so the recording was ended and saved.");
    if (differs && m_recorder.isActive()) m_recorder.restart(m_settings);
    else if (differs && m_recorder.state() == Recorder::State::Failed) m_recorder.start(m_settings);
    if (old.hotkeys != s.hotkeys || old.hotkeyBackend != s.hotkeyBackend) m_hotkeys.apply(m_settings);
    if (old.autostart != s.autostart) writeAutostart(s.autostart);
    updateRecorderUi();
}

void MainWindow::deleteClips(const QStringList &paths)
{
    if (paths.isEmpty()) return;
    const QString what = paths.size() == 1
        ? QString("“%1”").arg(m_library.clip(paths.first()) ? m_library.clip(paths.first())->title : paths.first())
        : QString("%1 clips").arg(paths.size());
    QMessageBox box(QMessageBox::Question, "Delete clip",
                    QString("Delete %1 from disk? This cannot be undone.").arg(what),
                    QMessageBox::Cancel, this);
    box.setInformativeText("Links already shared on Pixeldrain keep working.");
    QPushButton *del = box.addButton("Delete", QMessageBox::DestructiveRole);
    box.exec();
    if (box.clickedButton() != del) return;

    for (const QString &p : paths) {
        if (m_jobs.busy() && m_jobs.activeClip() == p) m_jobs.cancel();
        if (m_editor->currentPath() == p) showPage(PageLibrary);
        if (!m_library.remove(p)) QMessageBox::warning(this, "Clipstash", "Could not delete " + p);
    }
}

void MainWindow::shareClip(const QString &path)
{
    const Clip *c = m_library.clip(path);
    if (!c) return;
    if (m_jobs.busy()) {
        m_editor->setJobIdle("Another export or upload is still running.", true);
        return;
    }
    if (m_settings.pixeldrainKey.trimmed().isEmpty()) {
        QMessageBox::information(this, "Pixeldrain API key needed",
            "Add your Pixeldrain API key under Settings → Sharing to upload clips.");
        showPage(PageSettings);
        return;
    }
    m_jobs.shareClip(*c, m_settings);
}

void MainWindow::exportClip(const QString &path, const QString &name, bool openWhenDone)
{
    const Clip *c = m_library.clip(path);
    if (!c) return;
    if (m_jobs.busy()) {
        m_editor->setJobIdle("Another export or upload is still running.", true);
        return;
    }
    if (m_jobs.exportClip(*c, m_settings, name)) m_openAfterExport = openWhenDone ? path : QString();
}

void MainWindow::copyDiscordFiles()
{
    QList<QUrl> urls;
    QStringList lines;
    qint64 total = 0;
    for (const QString &f : std::as_const(m_discordFiles)) {
        urls << QUrl::fromLocalFile(f);
        lines << QString::fromUtf8(QUrl::fromLocalFile(f).toEncoded());
        total += QFileInfo(f).size();
    }
    // The file itself goes on the clipboard, in the formats file managers use,
    // so Ctrl+V in Discord attaches it.
    auto *mime = new QMimeData;
    mime->setUrls(urls);
    mime->setData("x-special/gnome-copied-files", ("copy\n" + lines.join('\n')).toUtf8());
    QApplication::clipboard()->setMimeData(mime);
    const QString what = m_discordFiles.size() == 1 ? QFileInfo(m_discordFiles.first()).fileName()
                                                    : QString("%1 clips").arg(m_discordFiles.size());
    const QString text = QString("%1 (%2) copied. Paste it into Discord with Ctrl+V.").arg(what, formatSize(total));
    m_discordFiles.clear();
    m_editor->setJobIdle(text);
    notify("Ready for Discord", text);
}

void MainWindow::convertClips(const QStringList &paths)
{
    if (paths.isEmpty()) return;
    if (m_jobs.busy()) {
        QMessageBox::information(this, "Clipstash", "Another export or upload is still running.");
        return;
    }
    const QString what = paths.size() == 1 ? QString("this clip") : QString("%1 clips").arg(paths.size());
    QMessageBox box(QMessageBox::Question, "Convert to H.264",
                    QString("Convert %1 to H.264 so the files play in Discord and browsers?").arg(what),
                    QMessageBox::Cancel, this);
    box.setInformativeText("The original files are replaced. Names, dates, audio tracks and edits are kept. "
                           "Re-encoding loses a tiny amount of quality and cannot be undone.");
    QPushButton *go = box.addButton("Convert", QMessageBox::AcceptRole);
    box.exec();
    if (box.clickedButton() != go) return;
    m_jobs.convertClips(paths, m_settings);
}

void MainWindow::notify(const QString &title, const QString &text)
{
    m_lastNotifiedLink = text.startsWith("https://") ? text : QString();
    if (m_tray) m_tray->showMessage(title, text, Theme::appIcon(), 3500);
}

void MainWindow::writeAutostart(bool enabled)
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/autostart";
    const QString file = dir + "/io.github.blake_mccullough.clipstash.desktop";
    if (!enabled) {
        QFile::remove(file);
        return;
    }
    QDir().mkpath(dir);
    QFile f(file);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return;
    f.write(QString("[Desktop Entry]\nType=Application\nName=Clipstash\nExec=\"%1\" --minimized --record\n"
                    "Icon=io.github.blake_mccullough.clipstash\nX-GNOME-Autostart-enabled=true\n")
                .arg(QCoreApplication::applicationFilePath()).toUtf8());
}

void MainWindow::handleCommand(const QString &command)
{
    const QStringList parts = command.trimmed().split(' ', Qt::SkipEmptyParts);
    if (parts.isEmpty()) return;
    const QString cmd = parts[0];
    if (cmd == "show") showWindow();
    else if (cmd == "save-clip") saveClip(parts.value(1).toInt());
    else if (cmd == "toggle-recording") toggleRecorder();
    else if (cmd == "toggle-full-recording") toggleFullRecording();
    else if (cmd == "quit") quit();
}

void MainWindow::setupUpdater()
{
    const QString current = QString("Version %1").arg(CLIPSTASH_VERSION);
    connect(m_updateNav, &QPushButton::clicked, this, [this] { showPage(PageSettings); });

    connect(m_settingsPage, &SettingsPage::updateActionRequested, this, [this, current] {
        if (m_updateState == UpdateState::Installed) {
            restartApp();
        } else if (m_updateState == UpdateState::Available) {
            m_settingsPage->setUpdateState("Downloading…", "Download and install", false);
            m_updater.downloadAndInstall();
        } else {
            m_updateCheckManual = true;
            m_settingsPage->setUpdateState(current + " · checking…", "Check for updates", false);
            m_updater.check();
        }
    });

    connect(&m_updater, &Updater::checked, this, [this, current](const UpdateInfo &info) {
        if (!info.newer) {
            m_updateState = UpdateState::Idle;
            m_updateNav->hide();
            m_settingsPage->setUpdateState(current + " · up to date", "Check for updates", true);
            return;
        }
        m_updateState = UpdateState::Available;
        m_updateNav->setText(QString("Update to %1").arg(info.version));
        m_updateNav->show();
        m_settingsPage->setUpdateState(QString("Version %1 is available (you have %2).").arg(info.version, CLIPSTASH_VERSION),
                                       "Download and install", true);
        if (!m_updateCheckManual)
            notify("Clipstash update", QString("Version %1 is available. Open Settings → Updates to install it.").arg(info.version));
    });
    connect(&m_updater, &Updater::progress, this, [this](const QString &stage, double fraction) {
        const QString text = stage == "Downloading" ? QString("Downloading… %1%").arg(int(fraction * 100)) : stage + "…";
        m_settingsPage->setUpdateState(text, "Download and install", false);
    });
    connect(&m_updater, &Updater::installed, this, [this](const QString &version) {
        m_updateState = UpdateState::Installed;
        m_updateNav->setText("Restart to update");
        m_updateNav->show();
        m_settingsPage->setUpdateState(QString("Version %1 is installed. Restart Clipstash to use it.").arg(version),
                                       "Restart now", true);
    });
    connect(&m_updater, &Updater::failed, this, [this](const QString &message) {
        // Keep an offered update on the table so the button retries the install.
        const bool available = m_updateState == UpdateState::Available;
        if (m_updateCheckManual || available)
            m_settingsPage->setUpdateState(message, available ? "Download and install" : "Check for updates", true);
        else
            m_settingsPage->setUpdateState(QString("Version %1").arg(CLIPSTASH_VERSION), "Check for updates", true);
    });

    if (m_settings.checkUpdates) QTimer::singleShot(8000, &m_updater, &Updater::check);
}

void MainWindow::restartApp()
{
    // Start the new binary once this instance has let go of its socket.
    const QString self = QFileInfo(QCoreApplication::applicationFilePath()).canonicalFilePath();
    QProcess::startDetached("/bin/sh", {"-c", "sleep 1; exec \"$0\"", self});
    quit();
}

void MainWindow::quit()
{
    m_quitting = true;
    m_editor->commit();
    m_recorder.stop();
    qApp->quit();
}

void MainWindow::hideEvent(QHideEvent *e)
{
    // Closed to the tray (or hidden from the tray icon): nothing should keep
    // playing where it can't be seen or stopped. Minimising is left alone.
    if (!e->spontaneous() || !isMinimized()) m_editor->pause();
    QMainWindow::hideEvent(e);
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    if (!m_quitting && m_settings.closeToTray && m_tray) {
        m_editor->commit();
        hide();
        e->ignore();
        return;
    }
    e->accept();
    quit();
}
