#ifndef MAIN_H
#define MAIN_H

#include <windows.h>
#include <commctrl.h>
#include <limits.h>
#include "resource.h"

#define APP_NAME       L"AlarmClock"
#define APP_CLASS      L"AlarmClockMainWnd"

#define MAX_ALARMS     10
#define ALARM_UNSET    -1

#define CLOCK_DIGITAL  0
#define CLOCK_ANALOG   1

#define SOUND_SIMPLE   0
#define SOUND_MP3      1

typedef struct {
    int   hour;
    int   minute;
    BOOL  enabled;
    WCHAR label[32];
    BYTE  repeat_days;
} Alarm;

typedef struct {
    BOOL     dark_mode;
    int      clock_style;
    BOOL     alarms_enabled;
    int      alarm_count;
    int      sound_mode;
    BOOL     hour24;
    BOOL     crescendo;
    BOOL     autostart;
    BOOL     start_minimized;
    BOOL     acrylic;
    BOOL     always_on_top;
    int      alarm_volume;
    int      snooze_minutes;
    Alarm    alarms[MAX_ALARMS];

    int      app_mode;
    int      winX, winY, winW, winH;

    BOOL     alarm_active;
    int      last_fire_min;
    ULONGLONG alarm_started_ms;  /* for the maximum ring duration */
    int      auto_snooze_count;

    BOOL     snooze_pending;
    ULONGLONG snooze_end_ms;
    int      snooze_total_sec;

    /* Countdown state */
    int      cd_hours, cd_mins, cd_secs;
    int      cd_remaining_ms;
    BOOL     cd_running;
    ULONGLONG cd_last_tick;
    BOOL     alarms_collapsed;

    /* Stopwatch state */
    BOOL     sw_running;
    DWORD    sw_start_tick;
    DWORD    sw_accumulated_ms;

    HWND     hMainWnd;
    HFONT    hClockFont;
    HFONT    hDateFont;
    HFONT    hGuiFont;
    WCHAR    clockFaceName[64];
    int      clockAreaH;
    int      dpi;            /* pixels per inch for this window; 96 == 100% */

    HBRUSH   hBgBrush;
    HBRUSH   hPanelBrush;
    COLORREF textColor;
    COLORREF bgColor;
    COLORREF panelBgColor;
    COLORREF accentColor;
    COLORREF clockColor;

    NOTIFYICONDATAW nid;
    BOOL     tray_added;

    HANDLE   hSoundThread;
    HANDLE   hCrescendoThread;
    HANDLE   hPreviewThread;
    /* Written by the UI thread, polled by the sound threads. volatile keeps
       -O2 from caching it in a register inside their loops. */
    volatile LONG stop_sound;
    BOOL     sound_preview;

    TCHAR    exe_dir[MAX_PATH];
} AppState;

extern AppState g_state;

void   theme_apply(HWND hwnd, BOOL dark);
void   theme_update_colors(AppState *s);
void   theme_dialog_colors(HWND hDlg, AppState *s, HWND ctrl, HDC hdc);
void   theme_dialog_init(HWND hDlg, AppState *s);

/* Settings load/save live in json_utils.h, which owns the result type. */

void   alarms_init(AppState *s);
BOOL   alarms_check(AppState *s, const SYSTEMTIME *st);

void   clock_init(void);
void   clock_cleanup(void);
void   clock_draw_digital(HDC hdc, const RECT *rc, const SYSTEMTIME *st, const AppState *s);
void   clock_draw_analog(HDC hdc, const RECT *rc, const SYSTEMTIME *st, const AppState *s);
void   clock_draw_countdown(HDC hdc, const RECT *rc, int remaining_ms, COLORREF tc, const AppState *s);
void   clock_draw_stopwatch(HDC hdc, const RECT *rc, DWORD elapsed_ms, const AppState *s);

void   tray_create(HWND hwnd, AppState *s);
void   tray_remove(AppState *s);
void   tray_show_menu(HWND hwnd, AppState *s);
void   tray_update_tooltip(AppState *s);

void   sound_play_alarm(AppState *s);
void   sound_stop_alarm(AppState *s);
void   sound_on_mci_notify(AppState *s);

void   autostart_update(AppState *s);

INT_PTR CALLBACK settings_dlg_proc(HWND hDlg, UINT msg, WPARAM wp, LPARAM lp);
INT_PTR CALLBACK alarm_dlg_proc(HWND hDlg, UINT msg, WPARAM wp, LPARAM lp);
INT_PTR CALLBACK cd_set_dlg_proc(HWND hDlg, UINT msg, WPARAM wp, LPARAM lp);

/* Countdown length, in one place. The raw (h*3600 + m*60 + s) * 1000 form was
   spelled out in five separate spots and overflowed int past about 596 hours,
   landing negative - which the timer then read as "already finished". Inline in
   the header so the settings reader can reach it too; it is linked into the test
   harness, which has no main_window.c. */
#define CD_MAX_HOURS 99

static inline void cd_clamp(AppState *s) {
    if (s->cd_hours < 0) s->cd_hours = 0;
    if (s->cd_hours > CD_MAX_HOURS) s->cd_hours = CD_MAX_HOURS;
    if (s->cd_mins  < 0)  s->cd_mins = 0;
    if (s->cd_mins  > 59) s->cd_mins = 59;
    if (s->cd_secs  < 0)  s->cd_secs = 0;
    if (s->cd_secs  > 59) s->cd_secs = 59;
}

static inline int cd_total_ms(const AppState *s) {
    long long total = ((long long)s->cd_hours * 3600 +
                       (long long)s->cd_mins  * 60 +
                       (long long)s->cd_secs) * 1000;
    if (total < 0) total = 0;
    if (total > (long long)INT_MAX) total = INT_MAX;
    return (int)total;
}

LRESULT CALLBACK main_wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

#endif
