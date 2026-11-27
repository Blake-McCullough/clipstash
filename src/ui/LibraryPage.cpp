#include "ui/LibraryPage.h"

#include "core/ClipLibrary.h"
#include "ui/ClipDelegate.h"

#include <QApplication>
#include <QButtonGroup>
#include <QClipboard>
#include <QDesktopServices>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollBar>
#include <QShortcut>
#include <QUrl>
#include <QItemSelection>
#include <QLocale>
#include <QTimer>
#include <QSettings>
#include <QVBoxLayout>

// Qt 6.10 replaced invalidateFilter() with a begin/end pair.
void ClipFilter::beginChange()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    beginFilterChange();
#endif
}

void ClipFilter::endChange()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    endFilterChange();
#else
    invalidateFilter();
#endif
}

void ClipFilter::setQuery(const QString &q)
{
    beginChange();
    m_query = q.trimmed();
    endChange();
}

void ClipFilter::setMode(int mode)
{
    beginChange();
    m_mode = mode;
    endChange();
}

bool ClipFilter::filterAcceptsRow(int row, const QModelIndex &) const
{
    const auto *lib = static_cast<ClipLibrary *>(sourceModel());
    const Clip &c = lib->clipAt(row);
    if (m_mode == 1 && !c.favorite) return false;
    if (m_mode == 2 && c.shareUrl.isEmpty()) return false;
    return m_query.isEmpty() || c.title.contains(m_query, Qt::CaseInsensitive)
        || c.fileName().contains(m_query, Qt::CaseInsensitive);
}

ClipGroups::ClipGroups(QAbstractItemModel *clips, QObject *parent) : QAbstractListModel(parent), m_clips(clips)
{
    for (auto sig : {&QAbstractItemModel::rowsInserted, &QAbstractItemModel::rowsRemoved})
        connect(clips, sig, this, &ClipGroups::rebuild);
    connect(clips, &QAbstractItemModel::modelReset, this, &ClipGroups::rebuild);
    connect(clips, &QAbstractItemModel::layoutChanged, this, &ClipGroups::rebuild);
    connect(clips, &QAbstractItemModel::dataChanged, this,
            [this](const QModelIndex &a, const QModelIndex &b, const QList<int> &roles) {
        for (int r = a.row(); r <= b.row() && r < m_rowOfSource.size(); ++r) {
            const QModelIndex i = index(m_rowOfSource[r]);
            emit dataChanged(i, i, roles);
        }
    });
    rebuild();
}

void ClipGroups::setGrouped(bool on)
{
    if (m_grouped == on) return;
    m_grouped = on;
    rebuild();
}

QString ClipGroups::dayLabel(const QDate &day, const QDate &today)
{
    const qint64 ago = day.daysTo(today);
    if (ago == 0) return "Today";
    if (ago == 1) return "Yesterday";
    if (ago > 1 && ago < 7) return QLocale().toString(day, "dddd");
    if (day.year() == today.year()) return QLocale().toString(day, "dddd d MMMM");
    return QLocale().toString(day, "d MMMM yyyy");
}

void ClipGroups::rebuild()
{
    beginResetModel();
    m_rows.clear();
    const int n = m_clips->rowCount();
    m_rowOfSource.resize(n);
    int header = -1;
    for (int r = 0; r < n; ++r) {
        if (m_grouped) {
            const QDate day = m_clips->index(r, 0).data(ClipLibrary::ClipRole).value<Clip>().created.date();
            if (header < 0 || m_rows[header].day != day) {
                header = int(m_rows.size());
                m_rows.append({-1, day, 0});
            }
            ++m_rows[header].count;
        }
        m_rowOfSource[r] = int(m_rows.size());
        m_rows.append({r, {}, 0});
    }
    endResetModel();
}

int ClipGroups::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant ClipGroups::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_rows.size()) return {};
    const Row &row = m_rows[index.row()];
    if (row.source >= 0) {
        if (role == HeaderRole) return false;
        return m_clips->index(row.source, 0).data(role);
    }
    switch (role) {
    case HeaderRole: return true;
    case HeaderCountRole: return row.count;
    case Qt::DisplayRole: return dayLabel(row.day, QDate::currentDate());
    }
    return {};
}

Qt::ItemFlags ClipGroups::flags(const QModelIndex &index) const
{
    if (!index.isValid() || index.row() >= m_rows.size()) return Qt::NoItemFlags;
    return m_rows[index.row()].source >= 0 ? (Qt::ItemIsEnabled | Qt::ItemIsSelectable) : Qt::NoItemFlags;
}

LibraryPage::LibraryPage(ClipLibrary *library, QWidget *parent) : QWidget(parent), m_library(library)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(22, 18, 12, 0);
    root->setSpacing(12);

    auto *top = new QHBoxLayout;
    auto *title = new QLabel("Clips");
    title->setObjectName("h1");
    m_summary = new QLabel;
    m_summary->setObjectName("muted");
    top->addWidget(title);
    top->addWidget(m_summary, 0, Qt::AlignBottom);
    top->addStretch();

    auto *group = new QButtonGroup(this);
    int id = 0;
    for (const char *name : {"All", "Favourites", "Shared"}) {
        auto *chip = new QPushButton(name);
        chip->setObjectName("chip");
        chip->setCheckable(true);
        chip->setChecked(id == 0);
        chip->setCursor(Qt::PointingHandCursor);
        group->addButton(chip, id++);
        top->addWidget(chip);
    }
    QSettings prefs;
    auto *byDay = new QPushButton("By day");
    byDay->setObjectName("chip");
    byDay->setCheckable(true);
    byDay->setChecked(prefs.value("library/groupByDay", true).toBool());
    byDay->setCursor(Qt::PointingHandCursor);
    byDay->setToolTip("Group clips under the day they were recorded");
    top->addSpacing(8);
    top->addWidget(byDay);
    auto *search = new QLineEdit;
    search->setPlaceholderText("Search clips");
    search->setClearButtonEnabled(true);
    search->setFixedWidth(220);
    top->addSpacing(8);
    top->addWidget(search);
    top->addSpacing(10);
    root->addLayout(top);

    m_filter = new ClipFilter(this);
    m_filter->setSourceModel(library);
    m_groups = new ClipGroups(m_filter, this);
    m_groups->setGrouped(byDay->isChecked());
    m_delegate = new ClipDelegate(this);

    m_view = new QListView;
    m_view->setModel(m_groups);
    m_view->setItemDelegate(m_delegate);
    m_view->setViewMode(QListView::IconMode);
    m_view->setResizeMode(QListView::Adjust);
    m_view->setMovement(QListView::Static);
    m_view->setUniformItemSizes(false);  // day headings span the full width
    m_view->setWrapping(true);
    m_view->setFlow(QListView::LeftToRight);
    m_view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_view->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_view->verticalScrollBar()->setSingleStep(28);
    m_view->setMouseTracking(true);
    m_view->setContextMenuPolicy(Qt::CustomContextMenu);
    m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    root->addWidget(m_view, 1);

    m_empty = new QLabel(m_view);
    m_empty->setObjectName("muted");
    m_empty->setAlignment(Qt::AlignCenter);
    m_empty->setText("No clips yet.\nPress your clip hotkey while playing, or hit “Clip now”.");
    m_empty->setAttribute(Qt::WA_TransparentForMouseEvents);

    connect(search, &QLineEdit::textChanged, m_filter, &ClipFilter::setQuery);
    connect(group, &QButtonGroup::idClicked, m_filter, &ClipFilter::setMode);
    connect(m_view, &QListView::doubleClicked, this, [this](const QModelIndex &i) {
        emit openRequested(i.data(ClipLibrary::PathRole).toString());
    });
    connect(m_view, &QListView::customContextMenuRequested, this, &LibraryPage::showContextMenu);

    // Regrouping resets the model; keep the selection and scroll position.
    connect(m_groups, &QAbstractItemModel::modelAboutToBeReset, this, [this] {
        m_keepSelection = selectedPaths();
        m_keepScroll = m_view->verticalScrollBar()->value();
    });
    connect(m_groups, &QAbstractItemModel::modelReset, this, [this] {
        if (!m_keepSelection.isEmpty()) {
            QItemSelection sel;
            for (int r = 0; r < m_groups->rowCount(); ++r) {
                const QModelIndex i = m_groups->index(r);
                if (m_keepSelection.contains(i.data(ClipLibrary::PathRole).toString())) sel.select(i, i);
            }
            m_view->selectionModel()->select(sel, QItemSelectionModel::ClearAndSelect);
        }
        const int scroll = m_keepScroll;
        QTimer::singleShot(0, m_view, [this, scroll] { m_view->verticalScrollBar()->setValue(scroll); });
    });
    connect(byDay, &QPushButton::toggled, this, [this](bool on) {
        QSettings().setValue("library/groupByDay", on);
        m_groups->setGrouped(on);
    });
    // "Today" becomes "Yesterday" at midnight.
    auto *midnight = new QTimer(this);
    midnight->setInterval(60 * 1000);
    connect(midnight, &QTimer::timeout, this, [this] {
        if (m_today == QDate::currentDate()) return;
        m_today = QDate::currentDate();
        m_view->viewport()->update();
    });
    midnight->start();

    auto *del = new QShortcut(QKeySequence::Delete, m_view);
    del->setContext(Qt::WidgetShortcut);
    connect(del, &QShortcut::activated, this, [this] {
        const QStringList paths = selectedPaths();
        if (!paths.isEmpty()) emit deleteRequested(paths);
    });
    auto *enter = new QShortcut(Qt::Key_Return, m_view);
    enter->setContext(Qt::WidgetShortcut);
    connect(enter, &QShortcut::activated, this, [this] {
        const QStringList paths = selectedPaths();
        if (paths.size() == 1) emit openRequested(paths.first());
    });

    for (auto sig : {&QAbstractItemModel::rowsInserted, &QAbstractItemModel::rowsRemoved})
        connect(m_filter, sig, this, &LibraryPage::updateSummary);
    connect(m_filter, &QAbstractItemModel::modelReset, this, &LibraryPage::updateSummary);
    connect(m_filter, &QAbstractItemModel::layoutChanged, this, &LibraryPage::updateSummary);
    connect(library, &ClipLibrary::clipChanged, this, &LibraryPage::updateSummary);
    updateSummary();
}

void LibraryPage::updateSummary()
{
    const int n = m_library->rowCount();
    // Always name the folder so it is obvious which one is being shown.
    const QString dir = m_library->directory();
    m_summary->setText(n == 0 ? QString("No clips in %1").arg(dir)
        : QString("%1 clip%2 · %3 · %4").arg(n).arg(n == 1 ? "" : "s").arg(formatSize(m_library->totalSize()), dir));
    m_empty->setVisible(m_filter->rowCount() == 0);
    if (n > 0) m_empty->setText("Nothing matches.");
    else m_empty->setText("No clips yet.\nPress your clip hotkey while playing, or hit “Clip now”.");
    m_empty->setGeometry(0, 60, qMax(200, m_view->width()), 60);
}

QStringList LibraryPage::selectedPaths() const
{
    QStringList paths;
    for (const QModelIndex &i : m_view->selectionModel()->selectedIndexes()) {
        const QString p = i.data(ClipLibrary::PathRole).toString();
        if (!p.isEmpty()) paths << p;
    }
    return paths;
}

void LibraryPage::setJobProgress(const QString &path, const QString &stage, double fraction)
{
    m_delegate->busyPath = path;
    m_delegate->busyStage = stage;
    m_delegate->busyFraction = fraction;
    m_view->viewport()->update();
}

void LibraryPage::clearJobProgress()
{
    m_delegate->busyPath.clear();
    m_view->viewport()->update();
}

void LibraryPage::showContextMenu(const QPoint &pos)
{
    const QModelIndex idx = m_view->indexAt(pos);
    if (!idx.isValid()) return;
    const QString path = idx.data(ClipLibrary::PathRole).toString();
    const Clip *c = m_library->clip(path);
    if (!c) return;
    const Clip clip = *c;
    QStringList selection = selectedPaths();
    if (!selection.contains(path)) selection = {path};

    QMenu menu(this);
    menu.addAction("Open in editor", this, [=, this] { emit openRequested(path); });
    menu.addAction("Rename…", this, [=, this] {
        bool ok = false;
        const QString t = QInputDialog::getText(this, "Rename clip", "Name (also used for the file name)",
                                                QLineEdit::Normal, clip.title, &ok);
        if (!ok || t.trimmed().isEmpty()) return;
        QString error;
        if (m_library->rename(path, t, &error).isEmpty()) QMessageBox::warning(this, "Rename clip", error);
    });
    menu.addAction(clip.favorite ? "Remove from favourites" : "Add to favourites", this, [=, this] {
        m_library->update(path, [](Clip &c) { c.favorite = !c.favorite; });
    });
    menu.addSeparator();
    menu.addAction(clip.shareUrl.isEmpty() ? "Share to Pixeldrain" : "Share again (new link)", this,
                   [=, this] { emit shareRequested(path); });
    if (!clip.shareUrl.isEmpty()) {
        menu.addAction("Copy link", this, [=] { QApplication::clipboard()->setText(clip.shareUrl); });
        menu.addAction("Open link", this, [=] { QDesktopServices::openUrl(QUrl(clip.shareUrl)); });
    }
    menu.addSeparator();
    const int many = selection.size();
    menu.addAction(many > 1 ? QString("Export %1 clips for sharing (H.264 copies)").arg(many)
                            : QString("Export for sharing (H.264 copy)"),
                   this, [=, this] { emit exportRequested(selection); });
    menu.addAction(many > 1 ? QString("Copy %1 clips for Discord").arg(many)
                            : QString("Copy for Discord"),
                   this, [=, this] { emit discordRequested(selection); });
    // Only offered when something selected is not H.264 already.
    QStringList convertible;
    for (const QString &p : selection) {
        const Clip *sel = m_library->clip(p);
        if (sel && !sel->playsEverywhere() && sel->videoCodec != "unknown") convertible << p;
    }
    if (!convertible.isEmpty())
        menu.addAction(convertible.size() > 1 ? QString("Convert %1 originals to H.264…").arg(convertible.size())
                                              : QString("Convert original to H.264…"),
                       this, [=, this] { emit convertRequested(convertible); });
    menu.addSeparator();
    menu.addAction("Show in file manager", this, [=] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
    });
    auto *del = menu.addAction(selection.size() > 1 ? QString("Delete %1 clips").arg(selection.size()) : "Delete",
                               this, [=, this] { emit deleteRequested(selection); });
    Q_UNUSED(del);
    menu.exec(m_view->viewport()->mapToGlobal(pos));
}
