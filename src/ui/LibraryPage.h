#pragma once
#include <QAbstractListModel>
#include <QDate>
#include <QSortFilterProxyModel>
#include <QWidget>

class ClipDelegate;
class ClipLibrary;
class QLabel;
class QLineEdit;
class QListView;
class QPushButton;

class ClipFilter : public QSortFilterProxyModel {
    Q_OBJECT
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;
    void setQuery(const QString &q);
    void setMode(int mode);  // 0 all, 1 favourites, 2 shared

protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override;

private:
    void beginChange();
    void endChange();
    QString m_query;
    int m_mode = 0;
};

// Puts a heading row ("Today", "Yesterday", "Monday 24 November"...) in front
// of each day's clips. Clips are already newest first, so each day is one run.
class ClipGroups : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles { HeaderRole = Qt::UserRole + 50, HeaderCountRole };

    explicit ClipGroups(QAbstractItemModel *clips, QObject *parent = nullptr);
    void setGrouped(bool on);
    bool grouped() const { return m_grouped; }

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

    static QString dayLabel(const QDate &day, const QDate &today);

private:
    void rebuild();
    struct Row {
        int source = -1;     // row in the clip model, -1 for a heading
        QDate day;           // headings only
        int count = 0;       // headings only
    };
    QAbstractItemModel *m_clips;
    QList<Row> m_rows;
    QList<int> m_rowOfSource;
    bool m_grouped = true;
};

class LibraryPage : public QWidget {
    Q_OBJECT
public:
    explicit LibraryPage(ClipLibrary *library, QWidget *parent = nullptr);

    void setJobProgress(const QString &path, const QString &stage, double fraction);
    void clearJobProgress();
    QStringList selectedPaths() const;

signals:
    void openRequested(const QString &path);
    void shareRequested(const QString &path);
    void deleteRequested(const QStringList &paths);
    void exportRequested(const QStringList &paths);
    void convertRequested(const QStringList &paths);
    void discordRequested(const QStringList &paths);

private:
    void showContextMenu(const QPoint &pos);
    void updateSummary();

    ClipLibrary *m_library;
    ClipFilter *m_filter;
    ClipGroups *m_groups;
    ClipDelegate *m_delegate;
    QListView *m_view;
    QLabel *m_summary;
    QLabel *m_empty;
    QStringList m_keepSelection;
    int m_keepScroll = 0;
    QDate m_today = QDate::currentDate();
};
