# AlarmClock

A native Windows 11 desktop alarm clock written in pure C using the Win32 API and GDI+ flat C API.

## Build

Requires **MSYS2** with the **UCRT64** toolchain installed at `C:\msys64\`.

### Install the toolchain

```sh
pacman -S mingw-w64-ucrt-x86_64-gcc
pacman -S mingw-w64-ucrt-x86_64-binutils
```

### Compile

```powershell
powershell -ExecutionPolicy Bypass -File build.ps1
```

The build script uses `gcc` from `C:\msys64\ucrt64\bin`, compiles all `.c` sources, links resources with `windres`, and produces `alarmclock.exe`.

Builds are incremental: a source is recompiled only when it or a header is
newer than its object file.

```powershell
# Remove obj/ and the executable
powershell -ExecutionPolicy Bypass -File build.ps1 -Clean

# Force every file to recompile
powershell -ExecutionPolicy Bypass -File build.ps1 -Rebuild

# Build, then run the settings round-trip tests
powershell -ExecutionPolicy Bypass -File build.ps1 -Test
```

## Features

### Clock

- **Digital display** — auto-fitted clock font (embedded Digital-7 Mono), dynamically sized to the window width
- **Analog display** — GDI+ anti-aliased rendering with smooth-sweep second hand (sub-millisecond precision via `GetSystemTimePreciseAsFileTime`, converted to local time), 60 tick marks, hour numbers, AM/PM marker, rounded hand caps
- **12/24-hour mode** with AM/PM indicator
- **Acrylic backdrop** — sets the Windows 11 DWM backdrop type on the window frame (toggleable). The client area is painted opaque, so this affects the frame rather than showing through the clock.
- **Light/dark theme** — DWM immersive dark mode, custom color palettes for all controls and dialogs
- **Always on top** — optional flag to keep the clock above other windows
- **Double-buffered rendering** — flicker-free display, with the back buffer kept between frames
- **Paced repainting** — the redraw rate follows what is on screen: once a second for the digital clock, 20fps only for the analog sweep hand and the stopwatch

### Timer / Stopwatch

- **Countdown timer** — set hours/minutes/seconds with 5/10/25 minute presets, start/pause/reset, alarm fires at zero, digits turn red
- **Stopwatch** — start/stop/reset, `HH:MM:SS.cc` centisecond display
- **Background running** — timers keep ticking when switching to clock mode; green-highlighted mode buttons indicate active background timers
- Toggle between Clock / Timer / Stopwatch via the mode bar below the clock

### Alarms

- **Wakes the PC from sleep** — a waitable timer with `fResume` is armed for the next alarm, so a machine asleep at 07:00 comes back for it. Windows must have "Allow wake timers" on; Settings says so plainly when it does not, rather than letting the alarm fail silently
- **Catches up alarms it missed** — anything that came due while the machine was asleep or the app was not running rings on the next start or resume, labelled with the time it should have gone off. Capped at 12 hours, so coming back from a week away does not ring for a Tuesday that is long past
- **Holds off sleep while ringing** — `ES_SYSTEM_REQUIRED`/`ES_DISPLAY_REQUIRED` for as long as the alarm sounds
- **Up to 10 configurable alarms** — each with custom hour, minute, label, and per-day scheduling (Sun-Sat checkboxes or All/None shortcuts)
- **Per-alarm overrides** — its own sound file, volume and snooze length, each defaulting to the global setting. A row shows a note glyph when it has a sound of its own
- **Skip next occurrence** — spends itself on the next time the alarm would ring and then clears, missed occurrences included
- **Inline alarm panel** in the main window — toggle checkbox to enable/disable each alarm, Edit/Clear buttons, time + label display
- **Collapsible panel** — click the ▼/▶ arrow to expand or collapse the alarm area; window auto-resizes
- **Snooze** — configurable delay (1-30 min) with on-screen countdown and cancel button
- **Ring limit** — an unattended alarm auto-snoozes after 5 minutes, up to 3 times, then stops rather than sounding indefinitely
- **Repeat modes** — once, daily, or per-day bitmask

### Sound

Media Foundation decodes, WASAPI renders, and gain is applied to the float
buffer on the way out. That one gain serves the volume setting, the crescendo,
the sleep-timer fade and per-alarm volume, rather than four unrelated
mechanisms.

- **Music playback** — put audio files in `songs\`; `.mp3`, `.wav`, `.flac`, `.m4a`, `.wma`, `.aac` and `.mp4` all decode. Shuffled, and the shuffle carries on across alarms instead of restarting
- **Generated tone** — a two-tone alarm synthesised into the same path, so it obeys the volume setting. `Beep()` could not be given one
- **Configurable volume** — 10%-100%, and it now applies in both modes
- **Crescendo** — a real 15-second gain ramp, resumed across a track change rather than restarted
- **Nothing falls silent** — a track that will not decode is skipped, and an empty or unplayable `songs\` folder falls back to the tone
- **Sound preview** — "Test Sound" button in Settings

### Sleep timer

The inverse of the crescendo: plays from `songs\` and fades to silence over the
configured length, then closes the device. It lives on the clock mode bar and
shows the time remaining while it runs; an alarm firing takes the device over
from it. There is deliberately no tone fallback — with nothing to play, there
is no sleep timer.

### System tray

- **Minimize to tray** on close (fade animation)
- **Tray icon** with right-click menu (Show / Settings / About / Exit)
- **Tooltip** shows next upcoming alarm
- **Balloon notification** on alarm trigger when window is hidden
- **Start minimized** option — launch directly to the tray
- **Start with Windows** — optional registry autorun entry

### Keyboard

| Key | Action |
|---|---|
| `Esc` | Dismiss a ringing alarm, or cancel a pending snooze |
| `Space` / `Enter` | Snooze a ringing alarm |
| `S` | Open Settings |

### Settings

Reached from the **Settings** button in the alarm panel header, or from the tray
menu. All options are persisted to `alarmclock_settings.json` (UTF-8) next to the executable:

| Setting | Description |
|---|---|
| Dark Mode | Light/dark theme toggle |
| Clock Style | Digital or analog |
| 24-Hour Clock | Toggle 12h/24h time format |
| Acrylic Background | Windows 11 blur backdrop |
| Enable Alarms | Master alarm on/off |
| Alarm slots | Number of visible alarm rows (1-10) |
| Snooze (min) | Snooze delay (1, 2, 3, 5, 10, 15, 20, 30) |
| Alarm Sound | Simple tone or MP3 |
| Alarm volume | 10%-100%, in both sound modes |
| Crescendo Alarm | Ramp volume over 15 seconds |
| Sleep timer (min) | Length of the fade-to-silence timer |
| Start with Windows | Registry Run key |
| Always on Top | Keep window above others |
| Start minimized | Launch hidden in tray |
| Window position/size | Restored on next launch, and re-centred if the saved monitor is gone |

Settings are written to a temporary file and swapped into place, leaving the
previous copy as `alarmclock_settings.json.bak`. If the main file cannot be
parsed, the backup is loaded instead.

A key the running build does not recognise, or one carrying an unexpected type,
is skipped rather than treated as corruption — so a newer build's settings file
does not cost an older build every alarm in it.

## Source structure

```
src/
  main.c              WinMain entry point, GDI+ init, window creation
  main.h              AppState struct, constants, function declarations
  main_window.c       Main window procedure, double-buffered painting,
                      alarm panel, mode bar, countdown/stopwatch logic
  resource.h          Resource IDs and constants
  theme.c / .h        Light/dark color palettes, DWM, acrylic backdrop
  clock_renderer.c/.h  Digital clock, GDI+ analog clock, countdown/stopwatch
  alarms.c / .h       Alarm data structures and firing logic
  alarm_dialog.c/.h   Per-alarm edit dialog (per-day scheduling)
  settings_dialog.c/.h Owner-draw settings dialog
  sound.c / .h        Which track, how loud, when to stop; sleep timer
  audio.c / .h        Media Foundation decode, WASAPI render, software gain
  power.c / .h        Wake timers, keep-awake, wake-timer policy
  tray.c / .h         System tray icon, tooltip, balloon
  settings_data.c/.h  JSON load/save bridge
  json_utils.c / .h   UTF-8 JSON reader/writer
tests/
  test_settings.c     Settings round trip, alarm schedule, catch-up, wake timing
  test_audio.c        Drives the real audio device at zero gain
  test_dialog.c       Shows a dialog and checks its layout; not part of -Test
resources/
  app.rc              Resource script (manifest, icon, font, dialogs)
  app.manifest        Common Controls v6 + PerMonitorV2 DPI
  digital-7.ttf       Embedded digital display font
songs/                Place .mp3 files here for alarm sounds
generate_icon.ps1     Clock-face app icon generator
build.ps1             MSYS2 UCRT64 build script
```
