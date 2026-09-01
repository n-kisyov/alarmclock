#include "tray.h"
#include "main.h"
#include <limits.h>

/* Explorer restarting destroys every tray icon. The shell broadcasts this
   registered message once it is back so applications can re-add theirs. */
UINT tray_taskbar_created_msg(void) {
    static UINT msg = 0;
    if (!msg) msg = RegisterWindowMessageW(L"TaskbarCreated");
    return msg;
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
    AppendMenuW(hMenu, MF_STRING, IDM_SETTINGS, L"Settings");
    AppendMenuW(hMenu, MF_STRING, IDM_ABOUT, L"About");
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

    for (int i = 0; i < MAX_ALARMS; i++) {
        int delta_minutes;
        if (!alarms_next_delta_minutes(&st, &s->alarms[i], &delta_minutes)) {
            continue;
        }

        if (delta_minutes < best_delta) {
            best_delta = delta_minutes;
            best_hour = s->alarms[i].hour;
            best_min = s->alarms[i].minute;
        }
    }

    WCHAR tip[128];
    if (!s->alarms_enabled) {
        /* The tooltip used to promise the next alarm even with the master
           switch off, which is exactly backwards for the one place you check. */
        lstrcpyW(tip, L"AlarmClock - alarms off");
    } else if (best_hour >= 0) {
        wsprintfW(tip, L"Next alarm: %02d:%02d", best_hour, best_min);
    } else {
        lstrcpyW(tip, L"AlarmClock");
    }

    if (lstrcmpW(s->nid.szTip, tip) != 0) {
        lstrcpyW(s->nid.szTip, tip);
        Shell_NotifyIconW(NIM_MODIFY, &s->nid);
    }
}
