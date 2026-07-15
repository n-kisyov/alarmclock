#ifndef MAIN_H
#define MAIN_H

#include <windows.h>
#include <commctrl.h>
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

    BOOL     snooze_pending;
    DWORD    snooze_end_ms;
    int      snooze_total_sec;

    /* Countdown state */
    int      cd_hours, cd_mins, cd_secs;
    int      cd_remaining_ms;
    BOOL     cd_running;
    DWORD    cd_last_tick;

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
    BOOL     stop_sound;
    BOOL     sound_preview;

    TCHAR    exe_dir[MAX_PATH];
} AppState;

extern AppState g_state;

void   theme_apply(HWND hwnd, BOOL dark);
void   theme_update_colors(AppState *s);
void   theme_dialog_colors(HWND hDlg, AppState *s, HWND ctrl, HDC hdc);
void   theme_dialog_init(HWND hDlg, AppState *s);

BOOL   json_load_settings(AppState *s, const TCHAR *path);
BOOL   json_save_settings(const AppState *s, const TCHAR *path);

void   alarms_init(AppState *s);
BOOL   alarms_check(AppState *s, const SYSTEMTIME *st);

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

LRESULT CALLBACK main_wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

#endif
