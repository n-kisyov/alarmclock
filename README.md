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

```powershell
# Clean rebuild
powershell -ExecutionPolicy Bypass -File build.ps1 -Clean
```

## Features

### Clock

- **Digital display** — auto-fitted clock font (embedded Digital-7 Mono), dynamically sized to the window width
- **Analog display** — GDI+ anti-aliased rendering with smooth-sweep second hand (sub-millisecond precision via `GetSystemTimePreciseAsFileTime`), 60 tick marks, hour numbers, rounded hand caps
- **12/24-hour mode** with AM/PM indicator
- **Acrylic backdrop** — Windows 11 blur/glass effect behind the window (toggleable)
- **Light/dark theme** — DWM immersive dark mode, custom color palettes for all controls and dialogs
- **Always on top** — optional flag to keep the clock above other windows
- **Double-buffered rendering** — flicker-free display

### Timer / Stopwatch

- **Countdown timer** — set hours/minutes/seconds, start/pause/reset, alarm fires at zero, digits turn red
- **Stopwatch** — start/stop/reset, `HH:MM:SS.cc` centisecond display
- **Background running** — timers keep ticking when switching to clock mode; green-highlighted mode buttons indicate active background timers
- Toggle between Clock / Timer / Stopwatch via the mode bar below the clock

### Alarms

- **Up to 10 configurable alarms** — each with custom hour, minute, label, and per-day scheduling (Sun-Sat checkboxes or All/None shortcuts)
- **Inline alarm panel** in the main window — toggle checkbox to enable/disable each alarm, Edit/Clear buttons, time + label display
- **Collapsible panel** — click the ▼/▶ arrow to expand or collapse the alarm area; window auto-resizes
- **Snooze** — configurable delay (1-30 min) with on-screen countdown and cancel button
- **Repeat modes** — once, daily, or per-day bitmask

### Sound

- **Simple tones** — PC speaker beeps with configurable crescendo (15-second volume/frequency ramp)
- **MP3 playback** — place `.mp3` files in the `songs\` folder; shuffled random playback via Windows MCI
- **Configurable volume** — 10%-100% slider for MP3
- **Sound preview** — "Test Sound" button in Settings

### System tray

- **Minimize to tray** on close (fade animation)
- **Tray icon** with right-click menu (Show / Exit)
- **Tooltip** shows next upcoming alarm
- **Balloon notification** on alarm trigger when window is hidden
- **Start minimized** option — launch directly to the tray
- **Start with Windows** — optional registry autorun entry

### Settings

All options are persisted to `alarmclock_settings.json` (UTF-8) next to the executable:

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
| Alarm volume | 10%-100% (MP3 only) |
| Crescendo Alarm | Ramp volume over 15 seconds |
| Start with Windows | Registry Run key |
| Always on Top | Keep window above others |
| Start minimized | Launch hidden in tray |
| Window position/size | Restored on next launch |

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
  sound.c / .h        Beep tones, MCI MP3 playback, crescendo
  tray.c / .h         System tray icon, tooltip, balloon
  settings_data.c/.h  JSON load/save bridge
  json_utils.c / .h   UTF-8 JSON reader/writer
resources/
  app.rc              Resource script (manifest, icon, font, dialogs)
  app.manifest        Common Controls v6 + PerMonitorV2 DPI
  digital-7.ttf       Embedded digital display font
songs/                Place .mp3 files here for alarm sounds
generate_icon.ps1     Clock-face app icon generator
build.ps1             MSYS2 UCRT64 build script
```
