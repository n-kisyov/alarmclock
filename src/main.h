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

/* Base window sizes, authored at 96 dpi like every other layout constant and
   scaled at the point of use. */
#define WIN_W_DIGITAL  720
#define WIN_H_DIGITAL  520
#define WIN_W_ANALOG   500
#define WIN_H_ANALOG   710

typedef struct {
    int   hour;
    int   minute;
    BOOL  enabled;
    WCHAR label[32];
    BYTE  repeat_days;

    /* Per-alarm overrides. -1 and an empty path mean "follow the global
       setting", so an alarm only differs where it was asked to. */
    WCHAR sound[MAX_PATH];
    int   volume;            /* -1 = global */
    int   snooze_minutes;    /* -1 = global */
    BOOL  skip_next;         /* consumed and cleared at the next occurrence */
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
    int      ringing_alarm;      /* slot that is ringing; -1 when it is the timer */
    /* A whole timestamp in minutes, not a minute of the day. This used to hold
       0..1439, so once a 07:00 alarm had fired the value stayed 420 and every
       later 07:00 - tomorrow's, and every day after that - compared equal and
       was suppressed: a repeating alarm rang exactly once per run of the app. */
    ULONGLONG last_fire_stamp;
    /* The last minute the app was awake and watching. The gap between this and
       now is what the catch-up walk covers. */
    ULONGLONG last_seen_stamp;
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

    /* Sleep timer: the inverse of the crescendo. Plays from the songs folder
       and rides the same gain down to silence. */
    BOOL      sleep_running;
    ULONGLONG sleep_end_ms;
    int       sleep_minutes;      /* persisted preference */

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

    /* The sound threads are gone: audio.c owns the one render thread, and a
       stop signals its event rather than waiting for a sleep to expire. */
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
BOOL   alarms_check(AppState *s, const SYSTEMTIME *st, int *out_index);
BOOL   alarms_next_delta_minutes(const SYSTEMTIME *st, const Alarm *a, int *delta_minutes);
ULONGLONG alarms_minute_stamp(const SYSTEMTIME *st);
BOOL   alarms_due_at(const Alarm *a, const SYSTEMTIME *st);
BOOL   alarms_stamp_to_systemtime(ULONGLONG stamp, SYSTEMTIME *st);
BOOL   alarms_catch_up(AppState *s, ULONGLONG nowStamp, int maxGapMinutes,
                       int *out_index, SYSTEMTIME *out_when);

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
UINT   tray_taskbar_created_msg(void);

void   sound_play_alarm(AppState *s);
void   sound_stop_alarm(AppState *s);
void   sound_on_track_done(AppState *s);
BOOL   sound_start_sleep_timer(AppState *s);
void   sound_stop_sleep_timer(AppState *s);

void   autostart_update(AppState *s);

BOOL   power_arm_wake_timer(AppState *s);
BOOL   power_wake_timers_allowed(void);
LONGLONG power_seconds_until_wake(const AppState *s, const SYSTEMTIME *now);
void   power_keep_awake(BOOL awake);
void   power_cleanup(void);

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

/* Per-alarm overrides resolve here so the "-1 means global" rule lives in one
   place rather than at every call site. idx is the ringing slot, or -1 for the
   countdown timer, which has no slot of its own. */
static inline int alarm_volume_for(const AppState *s, int idx) {
    if (idx >= 0 && idx < MAX_ALARMS && s->alarms[idx].volume >= 0)
        return s->alarms[idx].volume;
    return s->alarm_volume;
}

static inline int alarm_snooze_for(const AppState *s, int idx) {
    if (idx >= 0 && idx < MAX_ALARMS && s->alarms[idx].snooze_minutes > 0)
        return s->alarms[idx].snooze_minutes;
    return s->snooze_minutes;
}

/* NULL when this alarm has no sound of its own. */
static inline const WCHAR *alarm_sound_for(const AppState *s, int idx) {
    if (idx >= 0 && idx < MAX_ALARMS && s->alarms[idx].sound[0])
        return s->alarms[idx].sound;
    return NULL;
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
