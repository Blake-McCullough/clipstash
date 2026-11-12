#pragma once
#include <QObject>
#include <QQueue>
#include <QStringList>
#include <functional>

// Runs short helper processes (ffprobe, thumbnailers, device listings) off the
// UI thread's critical path with a small concurrency cap.
class ProcQueue : public QObject {
    Q_OBJECT
public:
    using Callback = std::function<void(int exitCode, const QByteArray &out, const QByteArray &err)>;

    explicit ProcQueue(int maxParallel = 2, QObject *parent = nullptr);
    void run(const QString &program, const QStringList &args, Callback cb);
    static ProcQueue *shared();

private:
    struct Job {
        QString program;
        QStringList args;
        Callback cb;
    };
    void pump();
    QQueue<Job> m_pending;
    int m_running = 0;
    int m_max;
};
