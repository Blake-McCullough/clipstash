#include "ui/MainWindow.h"

#include "ui/EditorPage.h"
#include "ui/LibraryPage.h"
#include "ui/SettingsPage.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QLabel>
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
    connect(&m_recorder, &Recorder::stateChanged, this, &MainWindow::updateRecorderUi);
    connect(&m_recorder, &Recorder::clipSaved, this, [this](const QString &path) {
        QStringList names;
        for (const AudioTrackCfg &t : std::as_const(m_settings.audioTracks))
            if (!t.sources.trimmed().isEmpty()) names << t.name;
        m_library.addFile(path, names);
        if (m_settings.soundOnSave) m_sound.play(m_settings.soundVolume);
        if (m_settings.notifyOnSave) notify("Clip saved", QFileInfo(path).fileName());
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
    });
    connect(&m_hotkeys, &HotkeyManager::statusChanged, m_settingsPage, &SettingsPage::setHotkeyStatus);
    m_hotkeys.apply(m_settings);

    // jobs
    connect(&m_jobs, &ClipJobs::progress, this, [this](const QString &path, const QString &stage, double f) {
        m_libraryPage->setJobProgress(path, stage, f);
        m_editor->setJobProgress(path, stage, f);
    });
    connect(&m_jobs, &ClipJobs::exported, this, [this](const QString &, const QString &out) {
        m_libraryPage->clearJobProgress();
        m_editor->setJobIdle("Saved as " + QFileInfo(out).fileName());
    });
    connect(&m_jobs, &ClipJobs::shared, this, [this](const QString &, const QString &url) {
        m_libraryPage->clearJobProgress();
        if (m_settings.copyLinkAfterUpload) QApplication::clipboard()->setText(url);
        m_editor->setJobIdle(m_settings.copyLinkAfterUpload ? "Uploaded. Link copied to the clipboard." : "Uploaded.");
        notify("Clip shared", url);
    });
    connect(&m_jobs, &ClipJobs::failed, this, [this](const QString &, const QString &msg) {
        m_libraryPage->clearJobProgress();
        m_editor->setJobIdle(msg, true);
        if (m_stack->currentIndex() != PageEditor && msg != "Cancelled")
            QMessageBox::warning(this, "Clipstash", msg);
    });

    updateRecorderUi();
    if (m_settings.recordOnLaunch) m_recorder.start(m_settings);
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
    rl->addWidget(m_clipNow);
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

    connect(m_libraryPage, &LibraryPage::openRequested, this, &MainWindow::openClip);
    connect(m_libraryPage, &LibraryPage::shareRequested, this, &MainWindow::shareClip);
    connect(m_libraryPage, &LibraryPage::deleteRequested, this, &MainWindow::deleteClips);

    connect(m_editor, &EditorPage::backRequested, this, [this] { showPage(PageLibrary); });
    connect(m_editor, &EditorPage::shareRequested, this, &MainWindow::shareClip);
    connect(m_editor, &EditorPage::exportRequested, this, &MainWindow::exportClip);
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
    m_trayToggle = menu->addAction("Stop recorder", this, &MainWindow::toggleRecorder);
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
    showPage(PageEditor);
    m_editor->open(path);
    if (m_jobs.busy()) m_editor->setJobProgress(m_jobs.activeClip(), "Working", 0);
    else m_editor->setJobIdle();
}

void MainWindow::saveClip(int seconds)
{
    if (m_recorder.state() != Recorder::State::Running && m_recorder.state() != Recorder::State::Starting) {
        notify("Recorder is not running", "Start the recorder to be able to clip.");
        return;
    }
    if (!m_recorder.saveReplay(seconds)) notify("Could not save clip", "The recorder did not accept the request.");
}

void MainWindow::toggleRecorder()
{
    if (m_recorder.isActive()) m_recorder.stop();
    else m_recorder.start(m_settings);
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
                     .arg(formatDuration(m_settings.replaySeconds)).arg(m_settings.audioTracks.size())
                     .arg(m_settings.audioTracks.size() == 1 ? "" : "s");
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
    m_recToggle->setText(active ? "Stop recorder" : "Start recorder");
    if (m_tray) {
        m_tray->setIcon(Theme::appIcon(st == S::Running));
        m_tray->setToolTip("Clipstash – " + text);
        m_trayToggle->setText(active ? "Stop recorder" : "Start recorder");
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
    if (s.recorderConfigDiffers(old) && m_recorder.isActive()) m_recorder.restart(m_settings);
    else if (s.recorderConfigDiffers(old) && m_recorder.state() == Recorder::State::Failed) m_recorder.start(m_settings);
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

void MainWindow::exportClip(const QString &path)
{
    const Clip *c = m_library.clip(path);
    if (!c) return;
    if (m_jobs.busy()) {
        m_editor->setJobIdle("Another export or upload is still running.", true);
        return;
    }
    m_jobs.exportClip(*c, m_settings);
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
    f.write(QString("[Desktop Entry]\nType=Application\nName=Clipstash\nExec=\"%1\" --minimized\n"
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
    else if (cmd == "quit") quit();
}

void MainWindow::quit()
{
    m_quitting = true;
    m_editor->commit();
    m_recorder.stop();
    qApp->quit();
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
