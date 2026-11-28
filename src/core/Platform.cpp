#include "core/Platform.h"

#include <QList>

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <cstdio>
#endif

namespace Platform {

bool replaceFile(const QString &from, const QString &to)
{
#ifdef Q_OS_WIN
    return MoveFileExW(reinterpret_cast<const wchar_t *>(QDir::toNativeSeparators(from).utf16()),
                       reinterpret_cast<const wchar_t *>(QDir::toNativeSeparators(to).utf16()),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED | MOVEFILE_WRITE_THROUGH) != 0;
#else
    return ::rename(from.toLocal8Bit().constData(), to.toLocal8Bit().constData()) == 0;
#endif
}

void tieToApp(QProcess *process, bool lowPriority)
{
#ifdef Q_OS_WIN
    if (!process || process->processId() == 0) return;
    // A job object that kills its processes when Clipstash goes away, so a
    // crash never leaves ffmpeg recording in the background.
    static HANDLE job = [] {
        HANDLE j = CreateJobObjectW(nullptr, nullptr);
        if (j) {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
            info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
            SetInformationJobObject(j, JobObjectExtendedLimitInformation, &info, sizeof(info));
        }
        return j;
    }();
    HANDLE h = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE | PROCESS_SET_INFORMATION, FALSE,
                           DWORD(process->processId()));
    if (!h) return;
    if (job) AssignProcessToJobObject(job, h);
    if (lowPriority) SetPriorityClass(h, BELOW_NORMAL_PRIORITY_CLASS);
    CloseHandle(h);
#else
    Q_UNUSED(process);
    Q_UNUSED(lowPriority);
#endif
}

bool setAutostart(bool on, const QString &exe, const QStringList &args)
{
#ifdef Q_OS_WIN
    QSettings run("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run", QSettings::NativeFormat);
    if (on) {
        QString cmd = "\"" + QDir::toNativeSeparators(exe) + "\"";
        for (const QString &a : args) cmd += " " + a;
        run.setValue("Clipstash", cmd);
    } else {
        run.remove("Clipstash");
    }
    run.sync();
    return run.status() == QSettings::NoError;
#else
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/autostart";
    const QString path = dir + "/io.github.blake_mccullough.clipstash.desktop";
    if (!on) return QFile::remove(path) || !QFile::exists(path);
    QDir().mkpath(dir);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    QString cmd = "\"" + exe + "\"";
    for (const QString &a : args) cmd += " " + a;
    f.write(QString("[Desktop Entry]\nType=Application\nName=Clipstash\nExec=%1\n"
                    "Icon=io.github.blake_mccullough.clipstash\nX-GNOME-Autostart-enabled=true\n")
                .arg(cmd).toUtf8());
    return true;
#endif
}

}

QRect Platform::monitorRect([[maybe_unused]] int index)
{
#ifdef Q_OS_WIN
    struct Ctx {
        QList<QRect> rects;
    } ctx;
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR mon, HDC, LPRECT, LPARAM data) -> BOOL {
        MONITORINFO info{};
        info.cbSize = sizeof info;
        if (GetMonitorInfoW(mon, &info)) {
            const RECT &r = info.rcMonitor;
            const QRect rect(r.left, r.top, r.right - r.left, r.bottom - r.top);
            auto *c = reinterpret_cast<Ctx *>(data);
            // Primary first, like Desktop Duplication usually numbers them.
            if (info.dwFlags & MONITORINFOF_PRIMARY) c->rects.prepend(rect);
            else c->rects.append(rect);
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&ctx));
    return ctx.rects.value(index);
#else
    return {};
#endif
}
