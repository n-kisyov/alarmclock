# AlarmClock

A native Windows desktop alarm clock application written in C using the Win32 API.

## Requirements

- Windows 11
- MSYS2 with the UCRT64 toolchain installed at `C:\msys64\`

### MSYS2 packages

```sh
pacman -S mingw-w64-ucrt-x86_64-gcc
pacman -S mingw-w64-ucrt-x86_64-binutils
```

## Build

```powershell
powershell -ExecutionPolicy Bypass -File build.ps1
```

The build script compiles all source files with `gcc`, links resources with `windres`, and produces `alarmclock.exe`. Use the `-Clean` switch to remove all build artifacts:

```powershell
powershell -ExecutionPolicy Bypass -File build.ps1 -Clean
```

### Font

The app bundles **Digital-7 Mono** (a freeware font by Sizenko Alexander) as an embedded binary resource. It is loaded at runtime from the executable and used for the digital clock display. No external font files are needed.

### Icon

The app icon (a clock face) is generated programmatically by `generate_icon.ps1` and compiled into the executable. The build script runs the generator automatically if the `.ico` file is missing.

## Configuration

All settings are stored in `alarmclock_settings.json` next to the executable. The file uses UTF-8 encoding and is created automatically on first run.

| Key | Type | Default | Description |
|---|---|---|---|
| `dark_mode` | bool | `false` | Enable dark theme across all windows |
| `hour24` | bool | `true` | Use 24-hour time format (off for 12-hour with AM/PM) |
| `crescendo` | bool | `false` | Ramp alarm volume from quiet to full over 15 seconds |
| `autostart` | bool | `false` | Launch automatically when Windows starts |
| `start_minimized` | bool | `false` | Start hidden in the system tray |
| `acrylic` | bool | `true` | Enable acrylic blur backdrop (Windows 11) |
| `clock_style` | string | `"digital"` | `"digital"` or `"analog"` |
| `alarms_enabled` | bool | `true` | Master alarm toggle |
| `alarm_count` | int | `5` | Number of alarm slots visible (1-10) |
| `snooze_minutes` | int | `3` | Snooze delay in minutes |
| `sound_mode` | string | `"simple"` | `"simple"` (PC speaker beeps) or `"mp3"` (files from `songs\`) |
| `alarms` | array | 10 empty slots | Each alarm: `hour`, `minute`, `enabled`, `label`, `repeat` |

### Alarm repeat modes

| Value | Mode |
|---|---|
| 0 | Once - disables after firing |
| 1 | Daily - fires every day |
| 2 | Weekdays - Mon through Fri |
| 3 | Weekends - Sat and Sun |

## MP3 Alarm Sounds

Place `.mp3` files in the `songs\` directory next to the executable. When the sound mode is set to "MP3", files are played in shuffled random order. Falls back to simple tones if no MP3s are found or MCI playback fails.

## Features

- **Digital clock** — large digits using an embedded digital-display font, dynamically sized to fill the window width
- **Analog clock** — smooth-sweep second hand, tick marks, and hour numbers
- **Up to 10 alarms** — each with a custom label, repeat schedule, and enable/disable toggle
- **Light/dark theme** — toggle in Settings, applies to all windows and dialogs, uses Windows 11 DWM immersive dark mode
- **Acrylic background** — Windows 11 glass/blur backdrop (toggleable)
- **System tray** — closes to tray instead of exiting; right-click tray icon for Show/Exit
- **Snooze** — configurable delay (1-30 min) with on-screen countdown timer
- **Crescendo alarm** — volume gradually increases over 15 seconds
- **12/24-hour time** — with AM/PM indicator in 12-hour mode
- **Window position memory** — size and position are restored on next launch
- **Start with Windows** — optional registry autorun entry
- **Start minimized** — option to launch directly to the tray
- **Double-buffered rendering** — flicker-free display
- **Resizable window** — all UI elements auto-arrange when resizing

## Source Structure

```
src/
├── main.c              Entry point, window creation, message loop
├── main.h              AppState struct, constants, function declarations
├── main_window.c       Main window procedure, double-buffered painting, alarm panel
├── resource.h          Resource IDs
├── theme.c / .h        Light/dark color palettes, DWM theming, dialog init
├── clock_renderer.c / .h   Digital and analog clock drawing
├── alarms.c / .h       Alarm data structures and firing logic
├── alarm_dialog.c / .h     Per-alarm edit dialog (owner-draw combo boxes)
├── settings_dialog.c / .h  Settings dialog (owner-draw combo boxes)
├── sound.c / .h        Alarm sound playback (Beep tones, MCI MP3)
├── tray.c / .h         System tray icon, tooltip, context menu
├── settings_data.c / .h    Load/save JSON settings
├── json_utils.c / .h   Minimal UTF-8 JSON reader/writer
resources/
├── app.rc              Resource script (manifest, icon, font, dialogs)
├── app.manifest        Common Controls v6 + PerMonitorV2 DPI awareness
└── digital-7.ttf       Embedded digital display font
songs/                  Place .mp3 files here for alarm sounds
generate_icon.ps1       Generates the clock-face app icon
build.ps1               MSYS2 UCRT64 build script
```

## License

This project is provided as-is for personal use. The bundled Digital-7 Mono font is freeware by Sizenko Alexander.
