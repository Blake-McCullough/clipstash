#pragma once
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

struct AudioTrackCfg {
    QString name;
    // gpu-screen-recorder source expression, e.g. "default_output",
    // "app:spotify" or "default_input|app:discord" (sources joined by | are merged).
    QString sources;
    bool operator==(const AudioTrackCfg &) const = default;
};

enum HotkeyMod : quint32 { ModCtrl = 1, ModShift = 2, ModAlt = 4, ModMeta = 8 };

struct HotkeyBinding {
    QString text;      // portable key sequence text, e.g. "Alt+F10"
    int code = 0;      // evdev key code
    quint32 mods = 0;  // HotkeyMod mask
    bool isSet() const { return code != 0 || !text.isEmpty(); }
    bool operator==(const HotkeyBinding &) const = default;
};

struct Settings {
    // storage
    QString libraryDir;

    // recorder
    QString recorderBinary = "gpu-screen-recorder";
    QString captureSource = "screen";
    int fps = 60;
    int replaySeconds = 60;
    QString codec = "h264";
    QString audioCodec = "aac";
    QString bitrateMode = "qp";      // qp | vbr | cbr
    QString quality = "very_high";   // medium | high | very_high | ultra
    int bitrateKbps = 20000;         // used with cbr
    QString resolution;              // "" = native, otherwise WxH
    QString frameMode = "vfr";
    QString replayStorage = "ram";
    bool recordCursor = true;
    bool restorePortalSession = true;
    QString extraArgs;
    QList<AudioTrackCfg> audioTracks;
    // Auto split: every app that has played audio gets its own track.
    bool autoSplitAudio = false;
    QStringList knownApps;
    // The tracks actually recorded: the manual list, or the generated one.
    QList<AudioTrackCfg> effectiveTracks() const;
    static QList<AudioTrackCfg> autoSplitTracks(const QStringList &apps);

    // behaviour
    bool recordOnLaunch = true;
    bool closeToTray = true;
    bool notifyOnSave = true;
    bool soundOnSave = true;
    int soundVolume = 70;  // percent
    bool autostart = false;
    bool checkUpdates = true;

    // hotkeys
    QString hotkeyBackend = "auto";  // auto | portal | evdev | none
    QMap<QString, HotkeyBinding> hotkeys;

    // sharing
    QString pixeldrainKey;
    bool copyLinkAfterUpload = true;
    int discordLimitMb = 10;

    // export
    QString exportVideoMode = "copy";  // copy | reencode
    QString exportEncoder = "auto";    // auto | libx264 | h264_nvenc | h264_vaapi
    bool exportCompat = true;          // convert HEVC/AV1 to H.264 on export and share
    int exportQuality = 21;
    int exportMaxHeight = 0;
    int exportAudioKbps = 192;

    static Settings load();
    void save() const;
    static QString defaultLibraryDir();
    // Cleans up a folder typed or pasted by hand: quotes, file:// and ~ are
    // resolved and the result is absolute.
    static QString normalizeDir(const QString &dir);

    // True if a change between the two requires restarting the recorder.
    bool recorderConfigDiffers(const Settings &o) const;
};

namespace HotkeyIds {
inline const char *SaveClip = "save-clip";
inline const char *Save30 = "save-30";
inline const char *Save60 = "save-60";
inline const char *ToggleRecording = "toggle-recording";
QStringList all();
QString label(const QString &id);
}
