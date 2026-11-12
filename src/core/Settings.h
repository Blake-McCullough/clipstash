#pragma once
#include <QList>
#include <QMap>
#include <QString>

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

    // behaviour
    bool recordOnLaunch = true;
    bool closeToTray = true;
    bool notifyOnSave = true;
    bool autostart = false;

    // hotkeys
    QString hotkeyBackend = "auto";  // auto | portal | evdev | none
    QMap<QString, HotkeyBinding> hotkeys;

    // sharing
    QString pixeldrainKey;
    bool copyLinkAfterUpload = true;

    // export
    QString exportVideoMode = "copy";  // copy | reencode
    QString exportEncoder = "libx264";
    int exportQuality = 21;
    int exportMaxHeight = 0;
    int exportAudioKbps = 192;

    static Settings load();
    void save() const;
    static QString defaultLibraryDir();

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
