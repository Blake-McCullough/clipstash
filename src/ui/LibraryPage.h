#pragma once
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
    QString m_query;
    int m_mode = 0;
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

private:
    void showContextMenu(const QPoint &pos);
    void updateSummary();

    ClipLibrary *m_library;
    ClipFilter *m_filter;
    ClipDelegate *m_delegate;
    QListView *m_view;
    QLabel *m_summary;
    QLabel *m_empty;
};
