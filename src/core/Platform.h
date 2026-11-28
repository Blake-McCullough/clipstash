#pragma once
#include <QString>

class QProcess;

namespace Platform {
// Moves `from` over `to` in one step, replacing it (atomic on the same drive).
bool replaceFile(const QString &from, const QString &to);
// Makes a helper process die with Clipstash and, optionally, run at lower
// priority so games keep the CPU. Call right after the process has started.
void tieToApp(QProcess *process, bool lowPriority);
// Puts Clipstash in (or takes it out of) the list of apps started at login.
bool setAutostart(bool on, const QString &exe, const QStringList &args);
}
