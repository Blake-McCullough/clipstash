#pragma once
#include "core/MediaTools.h"
#include "core/Settings.h"

#include <QMap>
#include <QWidget>

class HotkeyEdit;
class QCheckBox;
class QSlider;
class QComboBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class QVBoxLayout;

class SettingsPage : public QWidget {
    Q_OBJECT
public:
    explicit SettingsPage(QWidget *parent = nullptr);

    void setSettings(const Settings &s);
    Settings settings() const;
    void setHotkeyStatus(const QString &status);
    void refreshDevices();

signals:
    void saveRequested(const Settings &s);
    void testSoundRequested(int volumePercent);

protected:
    void showEvent(QShowEvent *e) override;

private:
    struct TrackRow {
        QWidget *widget;
        QLineEdit *name;
        QLineEdit *sources;
    };
    void addTrackRow(const AudioTrackCfg &t);
    void showSourceMenu(QLineEdit *target, QWidget *anchor);
    void showSplitMenu(QWidget *anchor);
    void splitApp(const QString &app);
    void updateQualityUi();

    Settings m_base;
    MediaTools::NamedList m_devices;
    MediaTools::NamedList m_apps;

    QLineEdit *m_libraryDir;
    QComboBox *m_source;
    QSpinBox *m_fps;
    QSpinBox *m_replay;
    QComboBox *m_codec;
    QComboBox *m_bitrateMode;
    QComboBox *m_quality;
    QSpinBox *m_bitrate;
    QComboBox *m_resolution;
    QComboBox *m_replayStorage;
    QCheckBox *m_cursor;
    QCheckBox *m_restorePortal;
    QLineEdit *m_extraArgs;
    QVBoxLayout *m_trackLayout;
    QList<TrackRow> m_tracks;

    QComboBox *m_hotkeyBackend;
    QMap<QString, HotkeyEdit *> m_hotkeys;
    QLabel *m_hotkeyStatus;

    QLineEdit *m_pixeldrainKey;
    QCheckBox *m_copyLink;

    QComboBox *m_exportMode;
    QComboBox *m_exportEncoder;
    QSpinBox *m_exportQuality;
    QComboBox *m_exportHeight;

    QCheckBox *m_recordOnLaunch;
    QCheckBox *m_closeToTray;
    QCheckBox *m_notify;
    QCheckBox *m_sound;
    QSlider *m_soundVolume;
    QCheckBox *m_autostart;
    QLabel *m_saved;
};
