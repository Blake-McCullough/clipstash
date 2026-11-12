#include "core/Settings.h"

#include <QDir>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>

namespace HotkeyIds {
QStringList all() { return {SaveClip, Save30, Save60, ToggleRecording}; }
QString label(const QString &id)
{
    if (id == SaveClip) return "Save clip (whole buffer)";
    if (id == Save30) return "Save last 30 seconds";
    if (id == Save60) return "Save last 60 seconds";
    if (id == ToggleRecording) return "Start / stop recorder";
    return id;
}
}

QString Settings::defaultLibraryDir()
{
    QString videos = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    if (videos.isEmpty()) videos = QDir::homePath() + "/Videos";
    return videos + "/Clipstash";
}

Settings Settings::load()
{
    QSettings s;
    Settings c;
    auto str = [&](const char *k, QString &v) { v = s.value(k, v).toString(); };
    auto num = [&](const char *k, int &v) { v = s.value(k, v).toInt(); };
    auto flag = [&](const char *k, bool &v) { v = s.value(k, v).toBool(); };

    c.libraryDir = defaultLibraryDir();
    str("storage/libraryDir", c.libraryDir);

    str("recorder/binary", c.recorderBinary);
    str("recorder/captureSource", c.captureSource);
    num("recorder/fps", c.fps);
    num("recorder/replaySeconds", c.replaySeconds);
    str("recorder/codec", c.codec);
    str("recorder/audioCodec", c.audioCodec);
    str("recorder/bitrateMode", c.bitrateMode);
    str("recorder/quality", c.quality);
    num("recorder/bitrateKbps", c.bitrateKbps);
    str("recorder/resolution", c.resolution);
    str("recorder/frameMode", c.frameMode);
    str("recorder/replayStorage", c.replayStorage);
    flag("recorder/recordCursor", c.recordCursor);
    flag("recorder/restorePortalSession", c.restorePortalSession);
    str("recorder/extraArgs", c.extraArgs);

    if (s.contains("recorder/audioTracks/size")) {
        int n = s.beginReadArray("recorder/audioTracks");
        for (int i = 0; i < n; ++i) {
            s.setArrayIndex(i);
            c.audioTracks.append({s.value("name").toString(), s.value("sources").toString()});
        }
        s.endArray();
    } else {
        c.audioTracks = {{"Desktop", "default_output"}, {"Microphone", "default_input"}};
    }

    flag("app/recordOnLaunch", c.recordOnLaunch);
    flag("app/closeToTray", c.closeToTray);
    flag("app/notifyOnSave", c.notifyOnSave);
    flag("app/soundOnSave", c.soundOnSave);
    num("app/soundVolume", c.soundVolume);
    flag("app/autostart", c.autostart);

    str("hotkeys/backend", c.hotkeyBackend);
    if (s.childGroups().contains("hotkeys")) {
        for (const QString &id : HotkeyIds::all()) {
            HotkeyBinding b;
            b.text = s.value("hotkeys/" + id + "/text").toString();
            b.code = s.value("hotkeys/" + id + "/code", 0).toInt();
            b.mods = s.value("hotkeys/" + id + "/mods", 0).toUInt();
            c.hotkeys[id] = b;
        }
    }
    if (!s.contains(QString("hotkeys/") + HotkeyIds::SaveClip + "/text")) {
        // Alt+F10, same as ShadowPlay / Medal defaults. KEY_F10 = 68.
        c.hotkeys[HotkeyIds::SaveClip] = {"Alt+F10", 68, ModAlt};
    }

    str("share/pixeldrainKey", c.pixeldrainKey);
    flag("share/copyLink", c.copyLinkAfterUpload);

    str("export/videoMode", c.exportVideoMode);
    str("export/encoder", c.exportEncoder);
    num("export/quality", c.exportQuality);
    num("export/maxHeight", c.exportMaxHeight);
    num("export/audioKbps", c.exportAudioKbps);
    return c;
}

void Settings::save() const
{
    QSettings s;
    s.setValue("storage/libraryDir", libraryDir);

    s.setValue("recorder/binary", recorderBinary);
    s.setValue("recorder/captureSource", captureSource);
    s.setValue("recorder/fps", fps);
    s.setValue("recorder/replaySeconds", replaySeconds);
    s.setValue("recorder/codec", codec);
    s.setValue("recorder/audioCodec", audioCodec);
    s.setValue("recorder/bitrateMode", bitrateMode);
    s.setValue("recorder/quality", quality);
    s.setValue("recorder/bitrateKbps", bitrateKbps);
    s.setValue("recorder/resolution", resolution);
    s.setValue("recorder/frameMode", frameMode);
    s.setValue("recorder/replayStorage", replayStorage);
    s.setValue("recorder/recordCursor", recordCursor);
    s.setValue("recorder/restorePortalSession", restorePortalSession);
    s.setValue("recorder/extraArgs", extraArgs);

    s.remove("recorder/audioTracks");
    s.beginWriteArray("recorder/audioTracks", audioTracks.size());
    for (int i = 0; i < audioTracks.size(); ++i) {
        s.setArrayIndex(i);
        s.setValue("name", audioTracks[i].name);
        s.setValue("sources", audioTracks[i].sources);
    }
    s.endArray();

    s.setValue("app/recordOnLaunch", recordOnLaunch);
    s.setValue("app/closeToTray", closeToTray);
    s.setValue("app/notifyOnSave", notifyOnSave);
    s.setValue("app/soundOnSave", soundOnSave);
    s.setValue("app/soundVolume", soundVolume);
    s.setValue("app/autostart", autostart);

    s.setValue("hotkeys/backend", hotkeyBackend);
    for (const QString &id : HotkeyIds::all()) {
        const HotkeyBinding b = hotkeys.value(id);
        s.setValue("hotkeys/" + id + "/text", b.text);
        s.setValue("hotkeys/" + id + "/code", b.code);
        s.setValue("hotkeys/" + id + "/mods", b.mods);
    }

    s.setValue("share/pixeldrainKey", pixeldrainKey);
    s.setValue("share/copyLink", copyLinkAfterUpload);

    s.setValue("export/videoMode", exportVideoMode);
    s.setValue("export/encoder", exportEncoder);
    s.setValue("export/quality", exportQuality);
    s.setValue("export/maxHeight", exportMaxHeight);
    s.setValue("export/audioKbps", exportAudioKbps);
    s.sync();
}

bool Settings::recorderConfigDiffers(const Settings &o) const
{
    return libraryDir != o.libraryDir || recorderBinary != o.recorderBinary
        || captureSource != o.captureSource || fps != o.fps
        || replaySeconds != o.replaySeconds || codec != o.codec
        || audioCodec != o.audioCodec || bitrateMode != o.bitrateMode
        || quality != o.quality || bitrateKbps != o.bitrateKbps
        || resolution != o.resolution || frameMode != o.frameMode
        || replayStorage != o.replayStorage || recordCursor != o.recordCursor
        || restorePortalSession != o.restorePortalSession || extraArgs != o.extraArgs
        || audioTracks != o.audioTracks;
}
