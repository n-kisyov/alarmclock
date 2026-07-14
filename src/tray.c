#include "tray.h"
#include "main.h"

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

    int bestHour = ALARM_UNSET;
    int bestMin  = ALARM_UNSET;

    SYSTEMTIME st;
    GetLocalTime(&st);
    int nowMin = st.wHour * 60 + st.wMinute;
    int today = st.wDayOfWeek;

    for (int i = 0; i < s->alarm_count; i++) {
        if (!s->alarms[i].enabled || s->alarms[i].hour == ALARM_UNSET) continue;

        int alarmMin = s->alarms[i].hour * 60 + s->alarms[i].minute;

        if (s->alarms[i].repeat_mode == REPEAT_ONCE) {
            if (alarmMin <= nowMin) continue;
        }
        if (s->alarms[i].repeat_mode == REPEAT_WEEKDAYS &&
            (today == 0 || today == 6)) continue;
        if (s->alarms[i].repeat_mode == REPEAT_WEEKENDS &&
            (today >= 1 && today <= 5)) continue;

        if (bestHour == ALARM_UNSET || alarmMin < bestHour * 60 + bestMin) {
            bestHour = s->alarms[i].hour;
            bestMin  = s->alarms[i].minute;
        }
    }

    WCHAR tip[128];
    if (bestHour >= 0) {
        wsprintfW(tip, L"Next alarm: %02d:%02d", bestHour, bestMin);
    } else {
        lstrcpyW(tip, L"AlarmClock");
    }

    if (lstrcmpW(s->nid.szTip, tip) != 0) {
        lstrcpyW(s->nid.szTip, tip);
        Shell_NotifyIconW(NIM_MODIFY, &s->nid);
    }
}
