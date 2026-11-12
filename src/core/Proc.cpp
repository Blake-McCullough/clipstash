#include "core/Proc.h"

#include <QCoreApplication>
#include <QProcess>

ProcQueue::ProcQueue(int maxParallel, QObject *parent) : QObject(parent), m_max(maxParallel) {}

ProcQueue *ProcQueue::shared()
{
    static ProcQueue *q = new ProcQueue(2, QCoreApplication::instance());
    return q;
}

void ProcQueue::run(const QString &program, const QStringList &args, Callback cb)
{
    m_pending.enqueue({program, args, std::move(cb)});
    pump();
}

void ProcQueue::pump()
{
    while (m_running < m_max && !m_pending.isEmpty()) {
        Job job = m_pending.dequeue();
        ++m_running;
        auto *p = new QProcess(this);
        auto done = [this, p, cb = std::move(job.cb)](int code) {
            const QByteArray out = p->readAllStandardOutput();
            const QByteArray err = p->readAllStandardError();
            p->deleteLater();
            --m_running;
            if (cb) cb(code, out, err);
            pump();
        };
        connect(p, &QProcess::finished, this, [done](int code, QProcess::ExitStatus st) {
            done(st == QProcess::NormalExit ? code : -1);
        });
        connect(p, &QProcess::errorOccurred, this, [done](QProcess::ProcessError e) {
            if (e == QProcess::FailedToStart) done(-2);
        });
        p->start(job.program, job.args, QIODevice::ReadOnly);
    }
}
