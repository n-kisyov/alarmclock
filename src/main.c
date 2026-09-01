#include "main.h"
#include "theme.h"
#include "settings_data.h"
#include "clock_renderer.h"
#include "audio.h"
#include <dwmapi.h>
#include <strsafe.h>
#include <stdlib.h>

AppState g_state;

void autostart_update(AppState *s) {
    TCHAR exePath[MAX_PATH];
    TCHAR quotedExePath[MAX_PATH * 2];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    if (FAILED(StringCchPrintfW(quotedExePath, MAX_PATH * 2, L"\"%s\"", exePath))) {
        return;
    }

    HKEY hKey;
    if (s->autostart) {
        if (RegCreateKeyExW(HKEY_CURRENT_USER,
                L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                0, NULL, 0, KEY_SET_VALUE, NULL, &hKey, NULL) == ERROR_SUCCESS) {
            RegSetValueExW(hKey, L"AlarmClock", 0, REG_SZ,
                           (const BYTE *)quotedExePath,
                           (DWORD)((lstrlenW(quotedExePath) + 1) * sizeof(WCHAR)));
            RegCloseKey(hKey);
        }
    } else {
        if (RegOpenKeyExW(HKEY_CURRENT_USER,
                L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                0, KEY_SET_VALUE, &hKey) == ERROR_SUCCESS) {
            RegDeleteValueW(hKey, L"AlarmClock");
            RegCloseKey(hKey);
        }
    }
}

/* A saved position on a monitor that is no longer attached would put the window
   off-screen - and since closing hides to the tray rather than exiting, there is
   no easy way to drag it back. */
/* No window exists yet when the first size is chosen, so this reads the desktop
   rather than a window. */
static int primary_dpi(void) {
    HDC hdc = GetDC(NULL);
    int d = hdc ? GetDeviceCaps(hdc, LOGPIXELSY) : 96;
    if (hdc) ReleaseDC(NULL, hdc);
    return d ? d : 96;
}

static void ensure_on_screen(int *x, int *y, int w, int h) {
    RECT r = { *x, *y, *x + w, *y + h };
    if (MonitorFromRect(&r, MONITOR_DEFAULTTONULL) != NULL) return;

    RECT work = {0};
    if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0)) {
        work.left = work.top = 0;
        work.right  = GetSystemMetrics(SM_CXSCREEN);
        work.bottom = GetSystemMetrics(SM_CYSCREEN);
    }
    *x = work.left + ((work.right - work.left) - w) / 2;
    *y = work.top  + ((work.bottom - work.top) - h) / 2;
    if (*x < work.left) *x = work.left;
    if (*y < work.top)  *y = work.top;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                    LPSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance;
    (void)lpCmdLine;

    /* Two instances means two tray icons, two alarms ringing over each other,
       and both writing the settings file on exit - last one wins, and the
       other's alarm edits are gone. */
    HANDLE hInstanceMutex = CreateMutexW(NULL, TRUE, L"Local\\AlarmClockSingleInstance");
    if (hInstanceMutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existing = FindWindowW(APP_CLASS, NULL);
        if (existing) {
            if (IsIconic(existing)) ShowWindow(existing, SW_RESTORE);
            else ShowWindow(existing, SW_SHOW);
            SetForegroundWindow(existing);
        }
        CloseHandle(hInstanceMutex);
        return 0;
    }

    INITCOMMONCONTROLSEX icc;
    icc.dwSize = sizeof(icc);
    icc.dwICC  = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    clock_init();
    audio_init();

    ZeroMemory(&g_state, sizeof(g_state));
    /* Zeroing leaves every slot at hour 0, minute 0 - a real midnight alarm as
       far as the panel and the checkboxes are concerned. alarms_init is the only
       thing that writes ALARM_UNSET, and json_load_settings only reaches it once
       a settings file has been read, so a first run never got one. */
    alarms_init(&g_state);

    /* Unseeded, rand() deals the same permutation on every run - the shuffled
       alarm played the same track every morning. */
    srand((unsigned)GetTickCount64());

    g_state.clock_style     = CLOCK_DIGITAL;
    g_state.sound_mode      = SOUND_SIMPLE;
    g_state.alarms_enabled  = TRUE;
    g_state.alarm_count     = 5;
    g_state.hour24          = TRUE;
    g_state.snooze_minutes  = 3;
    g_state.acrylic         = TRUE;
    g_state.app_mode        = APP_MODE_CLOCK;
    g_state.alarm_volume    = 80;
    g_state.sleep_minutes   = 30;
    /* Zero is "nothing has fired yet"; the stamp is a real point in time, so it
       can never legitimately be zero. */
    g_state.last_fire_stamp = 0;
    g_state.ringing_alarm   = -1;

    GetModuleFileNameW(NULL, g_state.exe_dir, MAX_PATH);
    TCHAR *slash = wcsrchr(g_state.exe_dir, L'\\');
    if (slash) *slash = 0;

    settings_load(&g_state);

    /* The countdown fields come straight out of the settings file, so clamp them
       before anything multiplies them up. */
    cd_clamp(&g_state);
    if (g_state.cd_remaining_ms == 0)
        g_state.cd_remaining_ms = cd_total_ms(&g_state);

    WNDCLASSEXW wc = {0};
    wc.cbSize        = sizeof(WNDCLASSEXW);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = main_wnd_proc;
    wc.hInstance     = hInstance;
    wc.hIcon         = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APPICON));
    wc.hCursor       = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = APP_CLASS;
    wc.hIconSm       = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APPICON));

    if (!RegisterClassExW(&wc)) {
        MessageBoxW(NULL, L"Failed to register window class.", L"Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    if (g_state.winW == 0) {
        /* These were raw pixels at any scaling, so on a high-dpi display the
           window came up too small for its own fitted clock font and panel. */
        int dpi = primary_dpi();
        BOOL analog = (g_state.clock_style == CLOCK_ANALOG);
        g_state.winW = MulDiv(analog ? WIN_W_ANALOG : WIN_W_DIGITAL, dpi, 96);
        g_state.winH = MulDiv(analog ? WIN_H_ANALOG : WIN_H_DIGITAL, dpi, 96);
        g_state.winX = (GetSystemMetrics(SM_CXSCREEN) - g_state.winW) / 2;
        g_state.winY = (GetSystemMetrics(SM_CYSCREEN) - g_state.winH) / 2;
    }

    ensure_on_screen(&g_state.winX, &g_state.winY, g_state.winW, g_state.winH);

    HWND hwnd = CreateWindowExW(
        g_state.always_on_top ? WS_EX_TOPMOST : 0,
        APP_CLASS, APP_NAME,
        WS_OVERLAPPEDWINDOW,
        g_state.winX, g_state.winY, g_state.winW, g_state.winH,
        NULL, NULL, hInstance, NULL);

    if (!hwnd) {
        MessageBoxW(NULL, L"Failed to create window.", L"Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    g_state.hMainWnd = hwnd;

    /* No menu bar: Windows draws it in the system light colours regardless of
       the dark theme applied to the rest of the window, and darkening a menu
       BAR (as opposed to its popups) needs undocumented messages. Settings is
       a themed button in the alarm panel header instead, and the tray menu
       carries Settings, About and Exit.  */

    if (!g_state.start_minimized) {
        ShowWindow(hwnd, nCmdShow);
    }
    UpdateWindow(hwnd);

    MSG msg;
    BOOL got;
    while ((got = GetMessageW(&msg, NULL, 0, 0)) != 0) {
        if (got == -1) break;   /* -1 is an error, not another message */
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    audio_shutdown();

    if (hInstanceMutex) {
        ReleaseMutex(hInstanceMutex);
        CloseHandle(hInstanceMutex);
    }

    return (int)msg.wParam;
}
