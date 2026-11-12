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
#include <QPushButton>
#include <QScrollBar>
#include <QShortcut>
#include <QUrl>
#include <QVBoxLayout>

void ClipFilter::setQuery(const QString &q)
{
    m_query = q.trimmed();
    invalidateFilter();
}

void ClipFilter::setMode(int mode)
{
    m_mode = mode;
    invalidateFilter();
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
    m_delegate = new ClipDelegate(this);

    m_view = new QListView;
    m_view->setModel(m_filter);
    m_view->setItemDelegate(m_delegate);
    m_view->setViewMode(QListView::IconMode);
    m_view->setResizeMode(QListView::Adjust);
    m_view->setMovement(QListView::Static);
    m_view->setUniformItemSizes(true);
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
    m_summary->setText(n == 0 ? QString()
        : QString("%1 clip%2 · %3").arg(n).arg(n == 1 ? "" : "s").arg(formatSize(m_library->totalSize())));
    m_empty->setVisible(m_filter->rowCount() == 0);
    if (n > 0) m_empty->setText("Nothing matches.");
    else m_empty->setText("No clips yet.\nPress your clip hotkey while playing, or hit “Clip now”.");
    m_empty->setGeometry(0, 60, qMax(200, m_view->width()), 60);
}

QStringList LibraryPage::selectedPaths() const
{
    QStringList paths;
    for (const QModelIndex &i : m_view->selectionModel()->selectedIndexes())
        paths << i.data(ClipLibrary::PathRole).toString();
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
        const QString t = QInputDialog::getText(this, "Rename clip", "Title", QLineEdit::Normal, clip.title, &ok);
        if (ok && !t.trimmed().isEmpty()) m_library->update(path, [&](Clip &c) { c.title = t.trimmed(); });
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
    menu.addAction("Show in file manager", this, [=] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
    });
    auto *del = menu.addAction(selection.size() > 1 ? QString("Delete %1 clips").arg(selection.size()) : "Delete",
                               this, [=, this] { emit deleteRequested(selection); });
    Q_UNUSED(del);
    menu.exec(m_view->viewport()->mapToGlobal(pos));
}
