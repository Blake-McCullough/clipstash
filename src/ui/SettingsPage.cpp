#include "ui/SettingsPage.h"

#include "ui/HotkeyEdit.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QStorageInfo>
#include <QTimer>
#include <QVBoxLayout>

namespace {
QLabel *muted(const QString &text)
{
    auto *l = new QLabel(text);
    l->setObjectName("muted");
    l->setWordWrap(true);
    l->setTextInteractionFlags(Qt::TextBrowserInteraction);
    l->setOpenExternalLinks(true);
    return l;
}

QFormLayout *section(QVBoxLayout *parent, const QString &title, const QString &hint = {})
{
    auto *frame = new QFrame;
    frame->setObjectName("panel");
    auto *v = new QVBoxLayout(frame);
    v->setContentsMargins(18, 14, 18, 16);
    v->setSpacing(10);
    auto *h = new QLabel(title);
    h->setObjectName("h2");
    v->addWidget(h);
    if (!hint.isEmpty()) v->addWidget(muted(hint));
    auto *form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    form->setHorizontalSpacing(18);
    form->setVerticalSpacing(10);
    form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    v->addLayout(form);
    parent->addWidget(frame);
    return form;
}

QComboBox *combo(std::initializer_list<QPair<QString, QString>> items)
{
    auto *c = new QComboBox;
    for (const auto &[label, value] : items) c->addItem(label, value);
    return c;
}

void selectData(QComboBox *c, const QString &value)
{
    int i = c->findData(value);
    if (i < 0) {
        c->addItem(value, value);
        i = c->count() - 1;
    }
    c->setCurrentIndex(i);
}

QSpinBox *spin(int min, int max, const QString &suffix = {})
{
    auto *s = new QSpinBox;
    s->setRange(min, max);
    s->setSuffix(suffix);
    s->setFixedWidth(130);
    return s;
}
}

SettingsPage::SettingsPage(QWidget *parent) : QWidget(parent)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    auto *content = new QWidget;
    auto *root = new QVBoxLayout(content);
    root->setContentsMargins(22, 18, 22, 18);
    root->setSpacing(14);
    scroll->setWidget(content);
    outer->addWidget(scroll, 1);

    auto *title = new QLabel("Settings");
    title->setObjectName("h1");
    root->addWidget(title);

    // --- storage
    auto *storage = section(root, "Storage", "Clips are written straight to this folder. Titles, edits and links are kept in a hidden .clipstash folder beside them, so the library moves with the drive.");
    m_libraryDir = new QLineEdit;
    auto *browse = new QPushButton("Browse…");
    auto *dirRow = new QHBoxLayout;
    dirRow->addWidget(m_libraryDir, 1);
    dirRow->addWidget(browse);
    storage->addRow("Clip folder", dirRow);
    auto *free = muted({});
    storage->addRow("", free);
    auto updateFree = [this, free] {
        QString dir = m_libraryDir->text();
        while (!dir.isEmpty() && !QFileInfo::exists(dir)) dir = QFileInfo(dir).path() == dir ? QString() : QFileInfo(dir).path();
        const QStorageInfo info(dir);
        free->setText(info.isValid() ? QString("%1 GB free on %2")
                          .arg(info.bytesAvailable() / 1073741824.0, 0, 'f', 1).arg(info.rootPath())
                                     : QString());
    };
    connect(m_libraryDir, &QLineEdit::textChanged, this, updateFree);
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString d = QFileDialog::getExistingDirectory(this, "Choose clip folder", m_libraryDir->text());
        if (!d.isEmpty()) m_libraryDir->setText(d);
    });

    // --- capture
    auto *cap = section(root, "Capture", "gpu-screen-recorder keeps the last moments in memory; nothing is written until you clip.");
    m_source = new QComboBox;
    m_source->setEditable(true);
    cap->addRow("Source", m_source);
    m_replay = spin(5, 1200, " s");
    cap->addRow("Replay length", m_replay);
    m_fps = spin(10, 360, " fps");
    cap->addRow("Frame rate", m_fps);
    m_resolution = new QComboBox;
    m_resolution->setEditable(true);
    m_resolution->addItems({"Native", "3840x2160", "2560x1440", "1920x1080", "1280x720"});
    cap->addRow("Resolution", m_resolution);
    m_codec = combo({{"H.264 (plays everywhere, best for sharing)", "h264"}, {"HEVC", "hevc"}, {"AV1", "av1"},
                     {"HEVC HDR", "hevc_hdr"}, {"AV1 HDR", "av1_hdr"}});
    cap->addRow("Video codec", m_codec);
    m_bitrateMode = combo({{"Constant quality", "qp"}, {"Variable bitrate", "vbr"}, {"Constant bitrate", "cbr"}});
    cap->addRow("Rate control", m_bitrateMode);
    m_quality = combo({{"Medium", "medium"}, {"High", "high"}, {"Very high", "very_high"}, {"Ultra", "ultra"}});
    cap->addRow("Quality", m_quality);
    m_bitrate = spin(500, 200000, " kbps");
    cap->addRow("Bitrate", m_bitrate);
    m_replayStorage = combo({{"RAM (default)", "ram"}, {"Disk (for very long buffers)", "disk"}});
    cap->addRow("Buffer kept in", m_replayStorage);
    m_cursor = new QCheckBox("Record the cursor");
    cap->addRow("", m_cursor);
    m_restorePortal = new QCheckBox("Remember the screen picked in the portal dialog");
    cap->addRow("", m_restorePortal);
    m_extraArgs = new QLineEdit;
    m_extraArgs->setPlaceholderText("Extra gpu-screen-recorder arguments (optional)");
    cap->addRow("Advanced", m_extraArgs);
    connect(m_bitrateMode, &QComboBox::currentIndexChanged, this, &SettingsPage::updateQualityUi);

    // --- audio
    auto *audio = section(root, "Audio tracks",
        "Each track is recorded separately, so you can mute or rebalance it per clip in the editor. "
        "Join sources with | to merge them into one track.");
    auto *trackHost = new QWidget;
    trackHost->setStyleSheet("background: transparent;");
    m_trackLayout = new QVBoxLayout(trackHost);
    m_trackLayout->setContentsMargins(0, 0, 0, 0);
    m_trackLayout->setSpacing(8);
    audio->addRow(trackHost);
    auto *trackButtons = new QHBoxLayout;
    auto *addTrack = new QPushButton("Add track");
    auto *split = new QPushButton("Split an app into its own track…");
    split->setToolTip("Records the app on a new track and removes it from your desktop track.\n"
                      "Handy for Spotify or Discord.");
    trackButtons->addWidget(addTrack);
    trackButtons->addWidget(split);
    trackButtons->addStretch();
    audio->addRow(trackButtons);
    connect(addTrack, &QPushButton::clicked, this,
            [this] { addTrackRow({QString("Track %1").arg(m_tracks.size() + 1), ""}); });
    connect(split, &QPushButton::clicked, this, [this, split] { showSplitMenu(split); });

    // --- hotkeys
    auto *keys = section(root, "Hotkeys", "Click a box and press the combination. Backspace clears it.");
    m_hotkeyBackend = combo({{"Automatic", "auto"}, {"Desktop portal (KDE, Hyprland, GNOME)", "portal"},
                             {"Direct keyboard access (evdev)", "evdev"}, {"Off", "none"}});
    keys->addRow("Method", m_hotkeyBackend);
    for (const QString &id : HotkeyIds::all()) {
        auto *edit = new HotkeyEdit;
        m_hotkeys[id] = edit;
        auto *row = new QHBoxLayout;
        row->addWidget(edit);
        row->addStretch();
        keys->addRow(HotkeyIds::label(id), row);
    }
    m_hotkeyStatus = muted({});
    auto *cliHint = muted("Any compositor can also bind the command <b>clipstash --save-clip</b> "
                          "(add <b>--seconds 30</b> for a shorter clip).");
    // Wrapped labels collapse to a sliver inside a form layout unless they
    // are told how wide they may get.
    for (QLabel *l : {m_hotkeyStatus, cliHint}) {
        l->setMinimumWidth(560);
        l->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    }
    keys->addRow("Status", m_hotkeyStatus);
    keys->addRow("", cliHint);

    // --- sharing
    auto *share = section(root, "Sharing",
        "Uploads go to your Pixeldrain account. Create an API key at "
        "<a style='color:#9d87ff' href='https://pixeldrain.com/user/api_keys'>pixeldrain.com/user/api_keys</a>.");
    m_pixeldrainKey = new QLineEdit;
    m_pixeldrainKey->setEchoMode(QLineEdit::PasswordEchoOnEdit);
    m_pixeldrainKey->setPlaceholderText("Pixeldrain API key");
    share->addRow("API key", m_pixeldrainKey);
    m_copyLink = new QCheckBox("Copy the link to the clipboard when an upload finishes");
    share->addRow("", m_copyLink);

    // --- export
    auto *exp = section(root, "Export", "Used by “Save as new clip” and when sharing.");
    m_exportCompat = new QCheckBox("Convert HEVC / AV1 clips to H.264 so they play in Discord and browsers");
    exp->addRow("", m_exportCompat);
    m_exportMode = combo({{"Fast: copy video (cuts snap to keyframes)", "copy"},
                          {"Precise: re-encode video (frame-accurate cuts)", "reencode"}});
    exp->addRow("Video", m_exportMode);
    m_exportEncoder = combo({{"Automatic", "auto"}, {"x264 (CPU)", "libx264"}, {"NVENC (NVIDIA)", "h264_nvenc"}, {"VAAPI (AMD / Intel)", "h264_vaapi"}});
    exp->addRow("Encoder", m_exportEncoder);
    m_exportQuality = spin(10, 40);
    m_exportQuality->setToolTip("Lower is better quality and a bigger file.");
    exp->addRow("Quality (CRF)", m_exportQuality);
    m_exportHeight = combo({{"Keep original", "0"}, {"1440p", "1440"}, {"1080p", "1080"}, {"720p", "720"}});
    exp->addRow("Max height", m_exportHeight);
    auto updateExport = [this] {
        const bool re = m_exportMode->currentData().toString() == "reencode" || m_exportCompat->isChecked();
        m_exportEncoder->setEnabled(re);
        m_exportQuality->setEnabled(re);
        m_exportHeight->setEnabled(re);
    };
    connect(m_exportMode, &QComboBox::currentIndexChanged, this, updateExport);
    connect(m_exportCompat, &QCheckBox::toggled, this, updateExport);

    // --- app
    auto *app = section(root, "App");
    m_recordOnLaunch = new QCheckBox("Start the recorder when Clipstash opens");
    m_closeToTray = new QCheckBox("Keep running in the tray when the window is closed");
    m_notify = new QCheckBox("Show a notification when a clip is saved");
    m_autostart = new QCheckBox("Launch Clipstash at login (minimised to tray)");
    m_sound = new QCheckBox("Play a sound when a clip is saved");
    m_soundVolume = new QSlider(Qt::Horizontal);
    m_soundVolume->setRange(0, 100);
    m_soundVolume->setFixedWidth(180);
    auto *testSound = new QPushButton("Test");
    connect(testSound, &QPushButton::clicked, this, [this] { emit testSoundRequested(m_soundVolume->value()); });
    connect(m_sound, &QCheckBox::toggled, m_soundVolume, &QWidget::setEnabled);
    connect(m_sound, &QCheckBox::toggled, testSound, &QWidget::setEnabled);
    auto *soundRow = new QHBoxLayout;
    soundRow->setContentsMargins(0, 0, 0, 0);
    soundRow->addWidget(m_sound);
    soundRow->addSpacing(12);
    soundRow->addWidget(m_soundVolume);
    soundRow->addWidget(testSound);
    soundRow->addStretch();
    for (QCheckBox *c : {m_recordOnLaunch, m_closeToTray}) app->addRow(c);
    app->addRow(soundRow);
    for (QCheckBox *c : {m_notify, m_autostart}) app->addRow(c);

    root->addStretch();

    // footer
    auto *footer = new QWidget;
    footer->setObjectName("sidebar");
    footer->setStyleSheet("QWidget#sidebar { border-right: none; border-top: 1px solid #2a2f3a; }");
    auto *fl = new QHBoxLayout(footer);
    fl->setContentsMargins(22, 10, 22, 10);
    m_saved = muted({});
    auto *revert = new QPushButton("Revert");
    auto *save = new QPushButton("Save settings");
    save->setObjectName("primary");
    fl->addWidget(m_saved, 1);
    fl->addWidget(revert);
    fl->addWidget(save);
    outer->addWidget(footer);
    connect(revert, &QPushButton::clicked, this, [this] { setSettings(m_base); });
    connect(save, &QPushButton::clicked, this, [this] {
        const Settings s = settings();
        const bool restart = s.recorderConfigDiffers(m_base);
        m_base = s;
        emit saveRequested(s);
        m_saved->setText(restart ? "Saved. The recorder was restarted with the new settings." : "Saved.");
        QTimer::singleShot(4000, m_saved, [this] { m_saved->clear(); });
    });
}

void SettingsPage::updateQualityUi()
{
    const bool cbr = m_bitrateMode->currentData().toString() == "cbr";
    m_quality->setEnabled(!cbr);
    m_bitrate->setEnabled(cbr);
}

void SettingsPage::addTrackRow(const AudioTrackCfg &t)
{
    TrackRow row;
    row.widget = new QWidget;
    auto *h = new QHBoxLayout(row.widget);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(8);
    row.name = new QLineEdit(t.name);
    row.name->setPlaceholderText("Name");
    row.name->setFixedWidth(150);
    row.sources = new QLineEdit(t.sources);
    row.sources->setPlaceholderText("default_output, default_input, app:spotify, device:…");
    auto *add = new QPushButton("Add source");
    auto *remove = new QPushButton("Remove");
    remove->setObjectName("danger");
    h->addWidget(row.name);
    h->addWidget(row.sources, 1);
    h->addWidget(add);
    h->addWidget(remove);
    m_trackLayout->addWidget(row.widget);
    m_tracks.append(row);

    QLineEdit *target = row.sources;
    QWidget *w = row.widget;
    connect(add, &QPushButton::clicked, this, [this, target, add] { showSourceMenu(target, add); });
    connect(remove, &QPushButton::clicked, this, [this, w] {
        for (int i = 0; i < m_tracks.size(); ++i) {
            if (m_tracks[i].widget != w) continue;
            m_tracks.removeAt(i);
            w->deleteLater();
            break;
        }
    });
}

static void appendSource(QLineEdit *target, const QString &src)
{
    QStringList parts = target->text().split('|', Qt::SkipEmptyParts);
    for (QString &p : parts) p = p.trimmed();
    if (!parts.contains(src)) parts << src;
    target->setText(parts.join('|'));
}

void SettingsPage::showSourceMenu(QLineEdit *target, QWidget *anchor)
{
    QMenu menu(this);
    menu.addAction("Default output (everything you hear)", this, [=] { appendSource(target, "default_output"); });
    menu.addAction("Default input (microphone)", this, [=] { appendSource(target, "default_input"); });
    QMenu *devices = menu.addMenu("Device");
    for (const auto &[value, desc] : std::as_const(m_devices)) {
        if (value == "default_output" || value == "default_input") continue;
        devices->addAction(desc.isEmpty() ? value : desc, this, [=] { appendSource(target, "device:" + value); });
    }
    devices->setEnabled(!devices->isEmpty());
    QMenu *apps = menu.addMenu("Application");
    QMenu *except = menu.addMenu("Everything except application");
    for (const auto &entry : std::as_const(m_apps)) {
        const QString name = entry.first;
        apps->addAction(name, this, [=] { appendSource(target, "app:" + name); });
        except->addAction(name, this, [=] { appendSource(target, "app-inverse:" + name); });
    }
    apps->setEnabled(!apps->isEmpty());
    except->setEnabled(!except->isEmpty());
    menu.exec(anchor->mapToGlobal(QPoint(0, anchor->height() + 2)));
}

void SettingsPage::showSplitMenu(QWidget *anchor)
{
    QMenu menu(this);
    for (const auto &entry : std::as_const(m_apps)) {
        const QString name = entry.first;
        menu.addAction(name, this, [=, this] { splitApp(name); });
    }
    if (!m_apps.isEmpty()) menu.addSeparator();
    menu.addAction("Other app (type its name)…", this, [this] {
        bool ok = false;
        const QString name = QInputDialog::getText(this, "Split an app", "Application name (as PipeWire shows it, e.g. spotify)",
                                                   QLineEdit::Normal, {}, &ok).trimmed();
        if (ok && !name.isEmpty()) splitApp(name);
    });
    menu.exec(anchor->mapToGlobal(QPoint(0, anchor->height() + 2)));
}

void SettingsPage::splitApp(const QString &app)
{
    // Take the app out of the first desktop-wide track so it is not recorded twice.
    for (const TrackRow &row : std::as_const(m_tracks)) {
        QStringList parts = row.sources->text().split('|', Qt::SkipEmptyParts);
        bool desktopWide = false;
        for (QString &p : parts) {
            p = p.trimmed();
            if (p == "default_output") {
                p = "app-inverse:" + app;
                desktopWide = true;
            } else if (p.startsWith("app-inverse:")) {
                desktopWide = true;
            }
        }
        if (!desktopWide) continue;
        const QString inverse = "app-inverse:" + app;
        bool has = false;
        for (const QString &p : std::as_const(parts)) has |= p.compare(inverse, Qt::CaseInsensitive) == 0;
        if (!has) parts << inverse;
        row.sources->setText(parts.join('|'));
        break;
    }
    QString name = app;
    name[0] = name[0].toUpper();
    addTrackRow({name, "app:" + app});
}

void SettingsPage::setSettings(const Settings &s)
{
    m_base = s;
    m_libraryDir->setText(s.libraryDir);
    m_source->setCurrentText(s.captureSource);
    m_fps->setValue(s.fps);
    m_replay->setValue(s.replaySeconds);
    selectData(m_codec, s.codec);
    selectData(m_bitrateMode, s.bitrateMode);
    selectData(m_quality, s.quality);
    m_bitrate->setValue(s.bitrateKbps);
    m_resolution->setCurrentText(s.resolution.isEmpty() ? "Native" : s.resolution);
    selectData(m_replayStorage, s.replayStorage);
    m_cursor->setChecked(s.recordCursor);
    m_restorePortal->setChecked(s.restorePortalSession);
    m_extraArgs->setText(s.extraArgs);

    for (const TrackRow &row : std::as_const(m_tracks)) row.widget->deleteLater();
    m_tracks.clear();
    for (const AudioTrackCfg &t : s.audioTracks) addTrackRow(t);

    selectData(m_hotkeyBackend, s.hotkeyBackend);
    for (auto it = m_hotkeys.cbegin(); it != m_hotkeys.cend(); ++it)
        it.value()->setBinding(s.hotkeys.value(it.key()));

    m_pixeldrainKey->setText(s.pixeldrainKey);
    m_copyLink->setChecked(s.copyLinkAfterUpload);

    m_exportCompat->setChecked(s.exportCompat);
    selectData(m_exportMode, s.exportVideoMode);
    selectData(m_exportEncoder, s.exportEncoder);
    m_exportQuality->setValue(s.exportQuality);
    selectData(m_exportHeight, QString::number(s.exportMaxHeight));
    const bool re = s.exportVideoMode == "reencode" || s.exportCompat;
    m_exportEncoder->setEnabled(re);
    m_exportQuality->setEnabled(re);
    m_exportHeight->setEnabled(re);

    m_recordOnLaunch->setChecked(s.recordOnLaunch);
    m_closeToTray->setChecked(s.closeToTray);
    m_notify->setChecked(s.notifyOnSave);
    m_sound->setChecked(s.soundOnSave);
    m_soundVolume->setValue(s.soundVolume);
    m_soundVolume->setEnabled(s.soundOnSave);
    m_autostart->setChecked(s.autostart);
    updateQualityUi();
}

Settings SettingsPage::settings() const
{
    Settings s = m_base;
    s.libraryDir = Settings::normalizeDir(m_libraryDir->text());
    // The combo shows "DP-1 (2560x1440)"; the recorder wants just "DP-1".
    const int idx = m_source->findText(m_source->currentText());
    s.captureSource = idx >= 0 && m_source->itemData(idx).isValid()
        ? m_source->itemData(idx).toString() : m_source->currentText().trimmed();
    s.fps = m_fps->value();
    s.replaySeconds = m_replay->value();
    s.codec = m_codec->currentData().toString();
    s.bitrateMode = m_bitrateMode->currentData().toString();
    s.quality = m_quality->currentData().toString();
    s.bitrateKbps = m_bitrate->value();
    const QString res = m_resolution->currentText().trimmed();
    s.resolution = res.compare("Native", Qt::CaseInsensitive) == 0 ? QString() : res;
    s.replayStorage = m_replayStorage->currentData().toString();
    s.recordCursor = m_cursor->isChecked();
    s.restorePortalSession = m_restorePortal->isChecked();
    s.extraArgs = m_extraArgs->text().trimmed();

    s.audioTracks.clear();
    for (const TrackRow &row : m_tracks) {
        const QString src = row.sources->text().trimmed();
        if (src.isEmpty()) continue;
        QString name = row.name->text().trimmed();
        if (name.isEmpty()) name = QString("Track %1").arg(s.audioTracks.size() + 1);
        s.audioTracks.append({name, src});
    }

    s.hotkeyBackend = m_hotkeyBackend->currentData().toString();
    for (auto it = m_hotkeys.cbegin(); it != m_hotkeys.cend(); ++it)
        s.hotkeys[it.key()] = it.value()->binding();

    s.pixeldrainKey = m_pixeldrainKey->text().trimmed();
    s.copyLinkAfterUpload = m_copyLink->isChecked();

    s.exportCompat = m_exportCompat->isChecked();
    s.exportVideoMode = m_exportMode->currentData().toString();
    s.exportEncoder = m_exportEncoder->currentData().toString();
    s.exportQuality = m_exportQuality->value();
    s.exportMaxHeight = m_exportHeight->currentData().toString().toInt();

    s.recordOnLaunch = m_recordOnLaunch->isChecked();
    s.closeToTray = m_closeToTray->isChecked();
    s.notifyOnSave = m_notify->isChecked();
    s.soundOnSave = m_sound->isChecked();
    s.soundVolume = m_soundVolume->value();
    s.autostart = m_autostart->isChecked();
    return s;
}

void SettingsPage::setHotkeyStatus(const QString &status) { m_hotkeyStatus->setText(status); }

void SettingsPage::showEvent(QShowEvent *e)
{
    QWidget::showEvent(e);
    refreshDevices();
}

void SettingsPage::refreshDevices()
{
    const QString bin = m_base.recorderBinary;
    MediaTools::gsrList(bin, "--list-audio-devices", [this](const MediaTools::NamedList &l) { m_devices = l; });
    MediaTools::gsrList(bin, "--list-application-audio", [this](const MediaTools::NamedList &l) { m_apps = l; });
    MediaTools::gsrList(bin, "--list-capture-options", [this](const MediaTools::NamedList &l) {
        const QString current = m_source->currentText();
        const int curIdx = m_source->findText(current);
        const QString currentValue = curIdx >= 0 && m_source->itemData(curIdx).isValid()
            ? m_source->itemData(curIdx).toString() : current;
        m_source->clear();
        m_source->addItem("screen", "screen");
        for (const auto &[value, desc] : l) {
            if (value == "screen" || value == "region" || value == "window" || value == "focused") continue;
            if (value == "portal")
                m_source->addItem("portal", "portal");
            else
                m_source->addItem(desc.isEmpty() ? value : QString("%1 (%2)").arg(value, desc), value);
        }
        const int i = m_source->findData(currentValue);
        if (i >= 0) m_source->setCurrentIndex(i);
        else m_source->setCurrentText(currentValue);
    });
}
