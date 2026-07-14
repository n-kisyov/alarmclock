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
