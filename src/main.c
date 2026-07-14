#include "main.h"
#include "theme.h"
#include "settings_data.h"
#include <dwmapi.h>

AppState g_state;

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                    LPSTR lpCmdLine, int nCmdShow) {
    (void)hPrevInstance;
    (void)lpCmdLine;

    INITCOMMONCONTROLSEX icc;
    icc.dwSize = sizeof(icc);
    icc.dwICC  = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    ZeroMemory(&g_state, sizeof(g_state));
    g_state.clock_style    = CLOCK_DIGITAL;
    g_state.sound_mode     = SOUND_SIMPLE;
    g_state.alarms_enabled = TRUE;
    g_state.alarm_count    = 5;

    GetModuleFileNameW(NULL, g_state.exe_dir, MAX_PATH);
    TCHAR *slash = wcsrchr(g_state.exe_dir, L'\\');
    if (slash) *slash = 0;

    settings_load(&g_state);

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

    int width  = g_state.clock_style == CLOCK_ANALOG ? 500 : 720;
    int height = g_state.clock_style == CLOCK_ANALOG ? 710 : 520;

    HWND hwnd = CreateWindowExW(
        0, APP_CLASS, APP_NAME,
        WS_OVERLAPPEDWINDOW,
        (GetSystemMetrics(SM_CXSCREEN) - width) / 2,
        (GetSystemMetrics(SM_CYSCREEN) - height) / 2,
        width, height,
        NULL, NULL, hInstance, NULL);

    if (!hwnd) {
        MessageBoxW(NULL, L"Failed to create window.", L"Error", MB_OK | MB_ICONERROR);
        return 1;
    }

    g_state.hMainWnd = hwnd;

    HMENU hMenu = CreateMenu();
    HMENU hFile = CreatePopupMenu();
    AppendMenuW(hFile, MF_STRING, IDM_SETTINGS, L"Settings");
    AppendMenuW(hFile, MF_SEPARATOR, 0, NULL);
    AppendMenuW(hFile, MF_STRING, IDM_EXIT, L"Exit");
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hFile, L"File");

    HMENU hHelp = CreatePopupMenu();
    AppendMenuW(hHelp, MF_STRING, IDM_ABOUT, L"About");
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hHelp, L"Help");

    SetMenu(hwnd, hMenu);

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    return (int)msg.wParam;
}
