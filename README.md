# Clipstash

Instant-replay clipping for Linux and Windows, in the spirit of Medal / ShadowPlay.
[gpu-screen-recorder](https://git.dec05eba.com/gpu-screen-recorder/about/) runs in the
background keeping the last minute or so in memory; press a hotkey and the clip lands in
your clip folder, ready to trim, re-mix and share.

- **Background replay buffer** – gpu-screen-recorder does the capture and encoding on the GPU.
- **Clip library** – thumbnails, search, favourites, rename, delete. Clips are plain `.mp4`
  files in a folder (any drive) you choose.
- **Global hotkeys** – save the whole buffer, the last 30 s or the last 60 s.
- **Separate audio sources** – record desktop, mic and individual apps (Spotify, Discord…)
  on their own tracks, then mute or rebalance each one per clip.
- **Editor** – trim with a filmstrip timeline, preview the mix live, save as a new clip.
- **Pixeldrain sharing** – one click uploads the clip (with your trim and audio mix applied)
  and copies the link.
- **Small and quick** – native Qt 6 / C++ with libmpv for playback. No Electron, no web view.

## Install (CachyOS / Arch)

```sh
sudo pacman -S --needed gpu-screen-recorder qt6-base mpv ffmpeg cmake ninja git
git clone https://github.com/Blake-McCullough/clipstash
cd clipstash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build
sudo cmake --install build
```

Or build a package: `cd packaging && makepkg -si`.

If `gpu-screen-recorder` is not in your repos, get it from the AUR (`paru -S gpu-screen-recorder`).

Then start **Clipstash** from the app launcher, or run `clipstash`.

## Install (Windows 10 / 11)

Download **Clipstash-Setup-x64.exe** from the
[latest release](https://github.com/Blake-McCullough/clipstash/releases/latest) and run it.
It installs for your user only (no admin prompt) and includes everything it needs (ffmpeg
and the video player), so there is nothing else to install.

On Windows the recording is done by ffmpeg using Desktop Duplication and your graphics
card's encoder, keeping the replay as short pieces on disk that roll over:

- **Settings → Capture → Encoder** – *Automatic* picks NVIDIA NVENC, then AMD AMF, then
  Intel Quick Sync, whichever works on your PC; you can also pick one, or the CPU (x264),
  which takes processor time from games.
- **Low resource mode** – 30 fps, at most 1080p, a fixed 8 Mbps bitrate, the encoder's
  fastest setting and a low-priority recorder. Use it if recording makes a game stutter.
  It works on Linux too.
- If Desktop Duplication isn't available (some laptops, remote desktop), Clipstash switches
  to a slower "compatibility capture" by itself.
- Audio tracks can use the default speakers (everything you hear) and the default
  microphone. Per-app tracks and auto split are Linux-only for now.
- Hotkeys work while games have focus. Start-with-the-computer uses the normal Windows
  startup list.

## First run

1. **Settings → Storage**: pick the folder (and drive) clips are saved to.
2. **Settings → Capture**: `screen` records the first monitor; pick a specific monitor from
   the list if you have several. On Wayland `portal` also works (the desktop asks which
   screen to share).
3. **Settings → Audio tracks**: see below.
4. **Settings → Sharing**: paste a Pixeldrain API key
   (<https://pixeldrain.com/user/api_keys>). Pixeldrain does not accept anonymous uploads.
5. Press **Save settings**. The recorder restarts with the new setup.

### Splitting audio sources

Every row under *Audio tracks* becomes its own track in the clip. A typical setup:

| Track      | Source                 |
|------------|------------------------|
| Game       | `app-inverse:spotify`  |
| Music      | `app:spotify`          |
| Microphone | `default_input`        |

*Split an app into its own track…* sets this up for you from the list of apps that are
currently playing audio. In the editor, untick a track (or drag its slider) and that is how
the clip is exported and shared. The original file keeps every track, so you can change your
mind later. Per-app capture needs PipeWire, which CachyOS uses by default.

**Auto split** (tick box at the top of *Audio tracks*) does this for you: Clipstash watches
which apps play sound and gives each one its own track, plus "Other audio" for everything
else and "Microphone". Apps are remembered, so the next session starts with their tracks
already in place. The first time a new app is noticed the replay buffer restarts once.

### Sharing on Discord

**Copy for Discord** (editor button, or right-click in the library) makes a small H.264 copy
that fits under Discord's upload limit and puts it on the clipboard, so Ctrl+V in Discord
attaches it. The copy is kept out of your clip folder and cleaned up after two days. Resolution and frame rate are chosen from the clip length and the limit
set in **Settings → Sharing** (10 MB by default; raise it if your account allows more).

### Start with the computer

**Settings → App → Start with the computer** launches Clipstash in the tray at login with the
recorder running. If the clip folder is on a drive that mounts a little later, it waits for it.

### Hotkeys

Set the keys under **Settings → Hotkeys**. The default is **Alt+F10**. Three methods:

- **Automatic** (default): uses the desktop's Global Shortcuts portal (KDE Plasma, Hyprland,
  GNOME 48+). The desktop may ask you to confirm the keys the first time and lets you change
  them later in its own shortcut settings.
- **Direct keyboard access**: reads key presses straight from the keyboard, works on any
  compositor and uses exactly the keys set in Clipstash. Needs
  `sudo usermod -aG input $USER` and a re-login.
- **Bind a command yourself**: any compositor can run `clipstash --save-clip`.

```sh
clipstash --save-clip               # save the whole buffer
clipstash --save-clip --seconds 30  # save the last 30 seconds
clipstash --toggle-recording        # start / stop the recorder
clipstash --minimized               # start in the tray
```

## Updates

Clipstash checks this repository's GitHub releases when it starts and shows an
**Update** button in the sidebar when a newer version is out. **Settings → Updates** downloads
the new build, verifies it, and replaces the installed program. If Clipstash lives in a system
folder such as `/usr/bin`, your desktop asks for your password to do that. Then restart.
On Windows it downloads the new installer, runs it silently and Clipstash starts again by itself.

Releases are built automatically: change the version in `CMakeLists.txt`, push to `main`,
and GitHub Actions builds, tests and publishes the release for that version.

## Saving edits

- **Save** applies the trim and audio changes to the clip itself (it asks first, since the
  cut-off parts are gone for good).
- **Save as new clip…** asks for a name and writes a new file, leaving the original alone.

Both keep each audio source you left ticked as its own track, so you can still mute or
rebalance them later. Only sharing (Pixeldrain, Discord) merges them into one track, because
browsers and Discord only play the first.

## Full recordings

**Start recording** (sidebar, tray menu, or a hotkey under Settings → Hotkeys) records
everything until you press **Stop recording**, like a normal screen recording. The replay buffer
keeps running underneath, so clipping still works while a recording is going. Recordings land in
the clip folder as `Recording_<date>.mp4` and open in the editor like clips.
`clipstash --toggle-full-recording` does the same from a script or compositor binding.

## Renaming

Rename a clip from the editor (**Rename** next to the title) or by right-clicking it in the
library. The file on disk gets the new name too, so it shows up that way when you drag it into
Discord. Characters Windows drives refuse (`: ? * " < > | \ /`) become `-` in the file name; the
title keeps exactly what you typed.

## Clipping several times in a row

Press the hotkey as often as you like: every press becomes its own full-length clip ending at
the moment you pressed it, even when the presses come seconds apart. The recorder can only
write one clip at a time, so Clipstash queues the presses. It keeps 20 seconds more in the
buffer than the clip length, and trims each saved clip back to the moment of its press.

## Troubleshooting

- **Saving is slow, clips go missing or end early:** set **Settings → Capture → Buffer kept in**
  to **RAM**. A buffer on disk is rewritten constantly and copied in full on every save, which
  is slow on NTFS/exFAT drives shared with Windows.
- Every start, save and recorder message is logged; `clipstash --diagnose` prints the last
  lines along with the rest of the report.

`clipstash --diagnose` prints the clip folder, how many video files are in it, the recorder
command line and which tools were found. Handy when something does not show up.

## Editor keys

| Key            | Action                |
|----------------|-----------------------|
| Space          | Play / pause          |
| I / O          | Set trim start / end  |
| ← / →          | Seek 2 seconds        |
| , / .          | Step one frame        |

## Notes

- Sharing mixes the ticked audio tracks down to one, because browsers only play the first
  audio track of a video. The video stream is copied as-is, so this takes a second or two.
- "Save as new clip" copies the video by default, so cuts land on the nearest keyframe
  before the start point (at most about 2 seconds early). Switch **Settings → Export → Video** to
  re-encode for frame-exact cuts.
- H.264 is the default recording codec because Discord and browsers play it. If you record
  in HEVC or AV1 (smaller files, nicer at 4K), "Save as new clip" and sharing convert the
  clip to H.264 for you, using the GPU encoder when there is one. The original recording in
  the clip folder stays HEVC/AV1, so send the converted clip, not the original.
- To fix up clips you already have, select them in the library (Ctrl+A for all) and
  right-click: **Export for sharing** makes H.264 copies with your trim and audio mix applied,
  **Convert originals to H.264** re-encodes the files themselves in place, keeping names,
  dates, audio tracks and edits.
- Titles, edits and links are stored in a hidden `.clipstash` folder inside the clip folder,
  so the library survives moving the drive or reinstalling.
- If the preview misbehaves on an unusual GPU setup, mpv options can be overridden:
  `CLIPSTASH_MPV_OPTS="hwdec=no" clipstash`.

## Development

```sh
cmake -B build -G Ninja -DCLIPSTASH_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```
