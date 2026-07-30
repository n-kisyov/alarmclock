#include "tray.h"
#include "main.h"
#include <limits.h>

static BOOL compute_next_alarm_delta_minutes(const SYSTEMTIME *st,
                                             const Alarm *alarm, int *delta_minutes) {
    if (!alarm->enabled || alarm->hour == ALARM_UNSET || alarm->minute == ALARM_UNSET) {
        return FALSE;
    }

    int now_min = (int)st->wHour * 60 + (int)st->wMinute;
    int alarm_min = alarm->hour * 60 + alarm->minute;

    if (alarm->repeat_days == 0) {
        if (alarm_min <= now_min) {
            return FALSE;
        }
        *delta_minutes = alarm_min - now_min;
        return TRUE;
    }

    for (int day_offset = 0; day_offset < 7; day_offset++) {
        int day = (st->wDayOfWeek + day_offset) % 7;
        if (!(alarm->repeat_days & (1 << day))) {
            continue;
        }
        if (day_offset == 0 && alarm_min <= now_min) {
            continue;
        }

        *delta_minutes = day_offset * 24 * 60 + (alarm_min - now_min);
        return TRUE;
    }

    return FALSE;
}

void tray_create(HWND hwnd, AppState *s) {

    ZeroMemory(&s->nid, sizeof(s->nid));
    s->nid.cbSize = sizeof(NOTIFYICONDATAW);
    s->nid.hWnd = hwnd;
    s->nid.uID = 1;
    s->nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    s->nid.uCallbackMessage = WM_TRAYICON;
    s->nid.hIcon = LoadIconW(GetModuleHandle(NULL), MAKEINTRESOURCEW(IDI_APPICON));
    lstrcpyW(s->nid.szTip, L"AlarmClock");

    Shell_NotifyIconW(NIM_ADD, &s->nid);
    s->tray_added = TRUE;
}

void tray_remove(AppState *s) {
    if (s->tray_added) {
        Shell_NotifyIconW(NIM_DELETE, &s->nid);
        s->tray_added = FALSE;
    }
}

void tray_show_menu(HWND hwnd, AppState *s) {
    (void)s;
    SetForegroundWindow(hwnd);

    HMENU hMenu = CreatePopupMenu();
    AppendMenuW(hMenu, MF_STRING, IDM_TRAY_SHOW, L"Show");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hMenu, MF_STRING, IDM_TRAY_EXIT, L"Exit");

    POINT pt;
    GetCursorPos(&pt);
    TrackPopupMenu(hMenu, TPM_RIGHTALIGN | TPM_BOTTOMALIGN, pt.x, pt.y, 0, hwnd, NULL);
    DestroyMenu(hMenu);
}

void tray_update_tooltip(AppState *s) {
    if (!s->tray_added) return;

    SYSTEMTIME st;
    GetLocalTime(&st);

    int best_delta = INT_MAX;
    int best_hour = ALARM_UNSET;
    int best_min = ALARM_UNSET;

    for (int i = 0; i < s->alarm_count; i++) {
        int delta_minutes;
        if (!compute_next_alarm_delta_minutes(&st, &s->alarms[i], &delta_minutes)) {
            continue;
        }

        if (delta_minutes < best_delta) {
            best_delta = delta_minutes;
            best_hour = s->alarms[i].hour;
            best_min = s->alarms[i].minute;
        }
    }

    WCHAR tip[128];
    if (best_hour >= 0) {
        wsprintfW(tip, L"Next alarm: %02d:%02d", best_hour, best_min);
    } else {
        lstrcpyW(tip, L"AlarmClock");
    }

    if (lstrcmpW(s->nid.szTip, tip) != 0) {
        lstrcpyW(s->nid.szTip, tip);
        Shell_NotifyIconW(NIM_MODIFY, &s->nid);
    }
}
