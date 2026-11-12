#include "ui/EditorPage.h"

#include "core/ClipLibrary.h"
#include "core/MediaTools.h"
#include "ui/MpvWidget.h"
#include "ui/Theme.h"
#include "ui/Timeline.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDesktopServices>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QShortcut>
#include <QSlider>
#include <QUrl>
#include <QVBoxLayout>

static QLabel *heading(const QString &text)
{
    auto *l = new QLabel(text);
    l->setObjectName("h2");
    return l;
}

EditorPage::EditorPage(ClipLibrary *library, QWidget *parent) : QWidget(parent), m_library(library)
{
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(350);
    connect(&m_saveTimer, &QTimer::timeout, this, &EditorPage::commit);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(18, 14, 18, 16);
    root->setSpacing(12);

    // header
    auto *header = new QHBoxLayout;
    auto *back = new QPushButton(QString::fromUtf8("←  Clips"));
    back->setObjectName("flat");
    m_title = new QLineEdit;
    m_title->setPlaceholderText("Clip title");
    m_title->setStyleSheet("QLineEdit { font-size: 16px; font-weight: 600; background: transparent; border-color: transparent; }"
                           "QLineEdit:hover { border-color: #2a2f3a; } QLineEdit:focus { border-color: #7c5cff; background: #12141a; }");
    m_fav = new QPushButton;
    m_fav->setObjectName("flat");
    m_fav->setToolTip("Favourite");
    auto *del = new QPushButton("Delete");
    del->setObjectName("danger");
    header->addWidget(back);
    header->addWidget(m_title, 1);
    header->addWidget(m_fav);
    header->addWidget(del);
    root->addLayout(header);

    auto *body = new QHBoxLayout;
    body->setSpacing(14);
    root->addLayout(body, 1);

    // left: player + transport + timeline
    auto *left = new QVBoxLayout;
    left->setSpacing(10);
    m_player = new MpvWidget;
    m_player->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    left->addWidget(m_player, 1);

    auto *transport = new QHBoxLayout;
    m_play = new QPushButton("Play");
    m_play->setObjectName("primary");
    m_play->setFixedWidth(84);
    m_time = new QLabel("0:00 / 0:00");
    m_time->setObjectName("muted");
    auto *setIn = new QPushButton("Set start");
    setIn->setToolTip("Trim start at the playhead (I)");
    auto *setOut = new QPushButton("Set end");
    setOut->setToolTip("Trim end at the playhead (O)");
    auto *reset = new QPushButton("Reset trim");
    auto *volLabel = new QLabel("Preview vol");
    volLabel->setObjectName("muted");
    auto *vol = new QSlider(Qt::Horizontal);
    vol->setRange(0, 100);
    vol->setValue(100);
    vol->setFixedWidth(90);
    transport->addWidget(m_play);
    transport->addWidget(m_time);
    transport->addStretch();
    transport->addWidget(setIn);
    transport->addWidget(setOut);
    transport->addWidget(reset);
    transport->addSpacing(12);
    transport->addWidget(volLabel);
    transport->addWidget(vol);
    left->addLayout(transport);

    m_timeline = new Timeline;
    left->addWidget(m_timeline);
    body->addLayout(left, 1);

    // right: side panel
    auto *side = new QFrame;
    side->setObjectName("panel");
    side->setFixedWidth(300);
    auto *sl = new QVBoxLayout(side);
    sl->setContentsMargins(16, 14, 16, 14);
    sl->setSpacing(10);

    sl->addWidget(heading("Audio sources"));
    auto *audioHint = new QLabel("Untick a source to drop it from the export and the shared link.");
    audioHint->setObjectName("muted");
    audioHint->setWordWrap(true);
    sl->addWidget(audioHint);
    m_trackHost = new QWidget;
    m_trackHost->setStyleSheet("background: transparent;");
    m_trackLayout = new QVBoxLayout(m_trackHost);
    m_trackLayout->setContentsMargins(0, 0, 0, 0);
    m_trackLayout->setSpacing(8);
    sl->addWidget(m_trackHost);
    m_noAudio = new QLabel("This clip has no audio.");
    m_noAudio->setObjectName("muted");
    sl->addWidget(m_noAudio);

    sl->addSpacing(8);
    sl->addWidget(heading("Clip"));
    m_info = new QLabel;
    m_info->setObjectName("muted");
    m_info->setWordWrap(true);
    sl->addWidget(m_info);
    m_export = new QPushButton("Save as new clip");
    m_export->setToolTip("Write the trimmed, mixed version as a new file. The original is kept.");
    sl->addWidget(m_export);

    sl->addSpacing(8);
    sl->addWidget(heading("Share"));
    m_share = new QPushButton("Share to Pixeldrain");
    m_share->setObjectName("primary");
    sl->addWidget(m_share);
    m_linkRow = new QWidget;
    m_linkRow->setStyleSheet("background: transparent;");
    auto *lr = new QHBoxLayout(m_linkRow);
    lr->setContentsMargins(0, 0, 0, 0);
    lr->setSpacing(6);
    m_link = new QLineEdit;
    m_link->setReadOnly(true);
    auto *copy = new QPushButton("Copy");
    auto *openLink = new QPushButton("Open");
    lr->addWidget(m_link, 1);
    lr->addWidget(copy);
    lr->addWidget(openLink);
    sl->addWidget(m_linkRow);

    m_progress = new QProgressBar;
    m_progress->setRange(0, 1000);
    m_progress->setTextVisible(false);
    m_progress->hide();
    sl->addWidget(m_progress);
    auto *statusRow = new QHBoxLayout;
    m_status = new QLabel;
    m_status->setObjectName("muted");
    m_status->setWordWrap(true);
    m_cancel = new QPushButton("Cancel");
    m_cancel->hide();
    statusRow->addWidget(m_status, 1);
    statusRow->addWidget(m_cancel, 0, Qt::AlignTop);
    sl->addLayout(statusRow);
    sl->addStretch();

    auto *keys = new QLabel("Space play/pause · I / O set trim\n← → seek 2 s · , . frame step");
    keys->setObjectName("muted");
    sl->addWidget(keys);
    body->addWidget(side);

    // wiring
    connect(back, &QPushButton::clicked, this, &EditorPage::backRequested);
    connect(del, &QPushButton::clicked, this, [this] { emit deleteRequested(m_clip.path); });
    connect(m_fav, &QPushButton::clicked, this, [this] {
        m_clip.favorite = !m_clip.favorite;
        m_fav->setText(QString::fromUtf8(m_clip.favorite ? "★" : "☆"));
        m_dirty = true;
        commit();
    });
    connect(m_title, &QLineEdit::editingFinished, this, [this] {
        const QString t = m_title->text().trimmed();
        if (t.isEmpty() || t == m_clip.title) return;
        m_clip.title = t;
        m_dirty = true;
        commit();
    });
    connect(m_play, &QPushButton::clicked, this, &EditorPage::togglePlay);
    connect(m_player, &MpvWidget::clicked, this, &EditorPage::togglePlay);
    connect(m_player, &MpvWidget::pausedChanged, this,
            [this](bool paused) { m_play->setText(paused ? "Play" : "Pause"); });
    connect(m_player, &MpvWidget::positionChanged, this, [this](double pos) {
        m_timeline->setPosition(pos);
        m_time->setText(formatDuration(pos) + " / " + formatDuration(m_clip.duration));
    });
    connect(m_player, &MpvWidget::durationChanged, this, [this](double d) {
        if (m_clip.duration <= 0 && d > 0) {
            m_clip.duration = d;
            m_timeline->setDuration(d);
            m_timeline->setTrim(m_clip.effectiveStart(), m_clip.effectiveEnd());
        }
    });
    connect(m_player, &MpvWidget::fileLoaded, this, [this] {
        m_player->setLoopRange(m_clip.effectiveStart(), m_clip.effectiveEnd());
        m_player->seek(m_clip.effectiveStart());
    });
    connect(vol, &QSlider::valueChanged, m_player, &MpvWidget::setVolume);

    connect(m_timeline, &Timeline::seekRequested, this, [this](double t, bool finished) {
        m_player->seek(t, finished);
    });
    connect(m_timeline, &Timeline::trimChanged, this, [this](double a, double b, bool finished) {
        const bool startMoved = qAbs(a - m_clip.trimStart) > 1e-6;
        m_clip.trimStart = a;
        m_clip.trimEnd = b >= m_clip.duration - 0.01 ? -1 : b;
        m_player->seek(startMoved ? a : b, finished);
        refreshInfo();
        if (finished) {
            m_player->setLoopRange(m_clip.effectiveStart(), m_clip.effectiveEnd());
            scheduleSave();
        }
    });
    connect(setIn, &QPushButton::clicked, this, [this] { setTrimPoint(true); });
    connect(setOut, &QPushButton::clicked, this, [this] { setTrimPoint(false); });
    connect(reset, &QPushButton::clicked, this, [this] {
        m_clip.trimStart = 0;
        m_clip.trimEnd = -1;
        m_timeline->setTrim(0, m_clip.duration);
        m_player->setLoopRange(0, m_clip.duration);
        refreshInfo();
        scheduleSave();
    });

    connect(m_export, &QPushButton::clicked, this, [this] {
        commit();
        emit exportRequested(m_clip.path);
    });
    connect(m_share, &QPushButton::clicked, this, [this] {
        commit();
        emit shareRequested(m_clip.path);
    });
    connect(m_cancel, &QPushButton::clicked, this, &EditorPage::cancelJobRequested);
    connect(copy, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(m_link->text());
        m_status->setText("Link copied.");
    });
    connect(openLink, &QPushButton::clicked, this,
            [this] { QDesktopServices::openUrl(QUrl(m_link->text())); });

    connect(library, &ClipLibrary::clipChanged, this, &EditorPage::onLibraryChanged);

    auto shortcut = [this](const QKeySequence &k, auto fn) {
        auto *s = new QShortcut(k, this);
        s->setContext(Qt::WidgetWithChildrenShortcut);
        connect(s, &QShortcut::activated, this, fn);
    };
    shortcut(Qt::Key_Space, [this] { togglePlay(); });
    shortcut(Qt::Key_I, [this] { setTrimPoint(true); });
    shortcut(Qt::Key_O, [this] { setTrimPoint(false); });
    shortcut(Qt::Key_Left, [this] { m_player->seek(m_player->position() - 2); });
    shortcut(Qt::Key_Right, [this] { m_player->seek(m_player->position() + 2); });
    shortcut(Qt::Key_Comma, [this] { m_player->frameStep(false); });
    shortcut(Qt::Key_Period, [this] { m_player->frameStep(true); });
    shortcut(Qt::Key_Escape, [this] { emit backRequested(); });
}

void EditorPage::open(const QString &path)
{
    commit();
    const Clip *c = m_library->clip(path);
    if (!c) return;
    m_clip = *c;
    m_dirty = false;

    m_title->setText(m_clip.title);
    m_fav->setText(QString::fromUtf8(m_clip.favorite ? "★" : "☆"));
    m_timeline->setFilmstrip({});
    m_timeline->setDuration(m_clip.duration);
    m_timeline->setTrim(m_clip.effectiveStart(), m_clip.effectiveEnd());
    m_timeline->setPosition(m_clip.effectiveStart());
    m_time->setText("0:00 / " + formatDuration(m_clip.duration));
    m_status->clear();
    buildTrackRows();
    refreshInfo();
    refreshShare();
    m_player->load(path, m_clip.tracks);
    loadFilmstrip();
    setFocus();
}

void EditorPage::close()
{
    commit();
    m_player->setPaused(true);
    m_player->unload();
    m_clip = Clip{};
}

void EditorPage::commit()
{
    m_saveTimer.stop();
    if (!m_dirty || m_clip.path.isEmpty()) return;
    m_dirty = false;
    m_saving = true;
    const Clip edited = m_clip;
    m_library->update(m_clip.path, [&](Clip &c) {
        c.title = edited.title;
        c.favorite = edited.favorite;
        c.trimStart = edited.trimStart;
        c.trimEnd = edited.trimEnd;
        c.tracks = edited.tracks;
    });
    m_saving = false;
}

void EditorPage::scheduleSave()
{
    m_dirty = true;
    m_saveTimer.start();
}

void EditorPage::onLibraryChanged(const QString &path)
{
    if (m_saving || path != m_clip.path) return;
    const Clip *c = m_library->clip(path);
    if (!c) return;
    const bool reload = !m_clip.probed && c->probed;
    if (reload) {
        open(path);  // media info arrived after the editor opened
        return;
    }
    m_clip.shareUrl = c->shareUrl;
    m_clip.shareId = c->shareId;
    refreshShare();
}

void EditorPage::buildTrackRows()
{
    while (QLayoutItem *item = m_trackLayout->takeAt(0)) {
        if (item->widget()) item->widget()->deleteLater();
        delete item;
    }
    m_noAudio->setVisible(m_clip.tracks.isEmpty() && m_clip.probed);
    for (int i = 0; i < m_clip.tracks.size(); ++i) {
        const ClipTrack &t = m_clip.tracks[i];
        auto *row = new QWidget;
        auto *v = new QVBoxLayout(row);
        v->setContentsMargins(0, 0, 0, 0);
        v->setSpacing(4);
        auto *check = new QCheckBox(t.name);
        check->setChecked(t.enabled);
        auto *h = new QHBoxLayout;
        auto *slider = new QSlider(Qt::Horizontal);
        slider->setRange(0, 200);
        slider->setValue(qRound(t.volume * 100));
        slider->setEnabled(t.enabled);
        auto *pct = new QLabel(QString("%1%").arg(qRound(t.volume * 100)));
        pct->setObjectName("muted");
        pct->setFixedWidth(40);
        pct->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        h->addWidget(slider, 1);
        h->addWidget(pct);
        v->addWidget(check);
        v->addLayout(h);
        m_trackLayout->addWidget(row);

        connect(check, &QCheckBox::toggled, this, [this, i, slider](bool on) {
            m_clip.tracks[i].enabled = on;
            slider->setEnabled(on);
            m_player->setAudioMix(m_clip.tracks);
            refreshInfo();
            scheduleSave();
        });
        connect(slider, &QSlider::valueChanged, this, [this, i, pct](int value) {
            m_clip.tracks[i].volume = value / 100.0;
            pct->setText(QString("%1%").arg(value));
            scheduleSave();
        });
        // Rebuilding mpv's filter graph glitches audio, so only do it on release.
        connect(slider, &QSlider::sliderReleased, this, [this] { m_player->setAudioMix(m_clip.tracks); });
        connect(slider, &QSlider::actionTriggered, this, [this, slider] {
            if (!slider->isSliderDown())
                QTimer::singleShot(0, this, [this] { m_player->setAudioMix(m_clip.tracks); });
        });
    }
}

void EditorPage::refreshInfo()
{
    QStringList lines;
    if (m_clip.isTrimmed())
        lines << QString("Trimmed to %1 of %2").arg(formatDuration(m_clip.editedDuration()), formatDuration(m_clip.duration));
    else
        lines << QString("Full length, %1").arg(formatDuration(m_clip.duration));
    if (m_clip.width > 0) lines << QString("%1×%2 · %3").arg(m_clip.width).arg(m_clip.height).arg(formatSize(m_clip.size));
    int on = 0;
    for (const ClipTrack &t : std::as_const(m_clip.tracks)) on += t.enabled;
    if (!m_clip.tracks.isEmpty())
        lines << QString("%1 of %2 audio sources kept").arg(on).arg(m_clip.tracks.size());
    m_info->setText(lines.join('\n'));
}

void EditorPage::refreshShare()
{
    m_linkRow->setVisible(!m_clip.shareUrl.isEmpty());
    m_link->setText(m_clip.shareUrl);
    m_share->setText(m_clip.shareUrl.isEmpty() ? "Share to Pixeldrain" : "Share again (new link)");
}

void EditorPage::togglePlay()
{
    if (m_clip.path.isEmpty()) return;
    if (m_player->isPaused()) {
        const double pos = m_player->position();
        if (pos < m_clip.effectiveStart() - 0.05 || pos > m_clip.effectiveEnd() - 0.1)
            m_player->seek(m_clip.effectiveStart());
        m_player->setPaused(false);
    } else {
        m_player->setPaused(true);
    }
}

void EditorPage::setTrimPoint(bool start)
{
    if (m_clip.duration <= 0) return;
    const double pos = qBound(0.0, m_player->position(), m_clip.duration);
    double a = m_clip.effectiveStart(), b = m_clip.effectiveEnd();
    if (start) a = qMin(pos, b - 0.5);
    else b = qMax(pos, a + 0.5);
    m_clip.trimStart = qMax(0.0, a);
    m_clip.trimEnd = b >= m_clip.duration - 0.01 ? -1 : b;
    m_timeline->setTrim(m_clip.effectiveStart(), m_clip.effectiveEnd());
    m_player->setLoopRange(m_clip.effectiveStart(), m_clip.effectiveEnd());
    refreshInfo();
    scheduleSave();
}

void EditorPage::loadFilmstrip()
{
    if (m_clip.duration <= 0) return;
    const QString path = m_clip.path;
    const QString strip = m_library->filmstripPath(m_clip);
    if (QFileInfo::exists(strip)) {
        m_timeline->setFilmstrip(QPixmap(strip));
        return;
    }
    MediaTools::filmstrip(path, strip, m_clip.duration, 16, [this, path, strip](bool ok) {
        if (ok && m_clip.path == path) m_timeline->setFilmstrip(QPixmap(strip));
    });
}

void EditorPage::setJobProgress(const QString &path, const QString &stage, double fraction)
{
    m_export->setEnabled(false);
    m_share->setEnabled(false);
    if (path != m_clip.path) return;
    m_progress->show();
    m_cancel->show();
    m_progress->setValue(int(fraction * 1000));
    m_status->setStyleSheet({});
    m_status->setText(QString("%1… %2%").arg(stage).arg(int(fraction * 100)));
}

void EditorPage::setJobIdle(const QString &message, bool error)
{
    m_export->setEnabled(true);
    m_share->setEnabled(true);
    m_progress->hide();
    m_cancel->hide();
    m_status->setStyleSheet(error ? "color: #ff5c6c;" : "");
    m_status->setText(message);
}
