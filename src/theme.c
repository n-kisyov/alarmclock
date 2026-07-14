#include "theme.h"
#include "main.h"
#include <dwmapi.h>
#include <uxtheme.h>

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_SYSTEMBACKDROP_TYPE
#define DWMWA_SYSTEMBACKDROP_TYPE 38
#endif
#ifndef DWMSBT_ACRYLIC
#define DWMSBT_ACRYLIC 3
#endif

static void theme_delete_gdi(AppState *s) {
    if (s->hBgBrush)    { DeleteObject(s->hBgBrush);    s->hBgBrush   = NULL; }
    if (s->hPanelBrush) { DeleteObject(s->hPanelBrush); s->hPanelBrush = NULL; }
}

void theme_update_colors(AppState *s) {
    theme_delete_gdi(s);

    if (s->dark_mode) {
        s->bgColor      = RGB(0x1E, 0x1E, 0x1E);
        s->panelBgColor = RGB(0x2D, 0x2D, 0x2D);
        s->textColor    = RGB(0xE0, 0xE0, 0xE0);
        s->accentColor  = RGB(0x00, 0x78, 0xD7);
        s->clockColor   = RGB(0x00, 0xFF, 0x33);
    } else {
        s->bgColor      = RGB(0xFF, 0xFF, 0xFF);
        s->panelBgColor = RGB(0xF3, 0xF3, 0xF3);
        s->textColor    = RGB(0x1E, 0x1E, 0x1E);
        s->accentColor  = RGB(0x00, 0x78, 0xD7);
        s->clockColor   = RGB(0x22, 0x22, 0x22);
    }

    s->hBgBrush    = CreateSolidBrush(s->bgColor);
    s->hPanelBrush = CreateSolidBrush(s->panelBgColor);
}

void theme_apply(HWND hwnd, BOOL dark) {
    BOOL val = dark ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &val, sizeof(val));

    int backdrop = g_state.acrylic ? DWMSBT_ACRYLIC : 0;
    DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop));

    InvalidateRect(hwnd, NULL, TRUE);
    UpdateWindow(hwnd);

    HWND child = GetWindow(hwnd, GW_CHILD);
    while (child) {
        InvalidateRect(child, NULL, TRUE);
        child = GetWindow(child, GW_HWNDNEXT);
    }
}

void theme_dialog_colors(HWND hDlg, AppState *s, HWND ctrl, HDC hdc) {
    (void)hDlg;

    if (!s->dark_mode) return;

    TCHAR cls[32];
    GetClassName(ctrl, cls, 32);

    if (lstrcmp(cls, L"Static") == 0 || lstrcmp(cls, L"Button") == 0) {
        SetTextColor(hdc, s->textColor);
        SetBkColor(hdc, s->bgColor);
        SetBkMode(hdc, TRANSPARENT);
    }

    if (lstrcmp(cls, L"Static") == 0) {
        DWORD style = GetWindowLong(ctrl, GWL_STYLE);
        if ((style & SS_TYPEMASK) != SS_ICON && (style & SS_TYPEMASK) != SS_BITMAP) {
            return;
        }
    }

    if (lstrcmp(cls, L"Edit") == 0) {
        SetTextColor(hdc, s->textColor);
        SetBkColor(hdc, s->panelBgColor);
    }
}

void theme_dialog_init(HWND hDlg, AppState *s) {
    theme_apply(hDlg, s->dark_mode);

    if (!s->dark_mode) return;

    HWND ctrl = GetWindow(hDlg, GW_CHILD);
    while (ctrl) {
        TCHAR cls[32];
        GetClassName(ctrl, cls, 32);

        if (lstrcmp(cls, L"Button") == 0) {
            DWORD style = GetWindowLongW(ctrl, GWL_STYLE);
            DWORD type  = style & BS_TYPEMASK;

            if (type == BS_AUTOCHECKBOX || type == BS_AUTORADIOBUTTON ||
                type == BS_GROUPBOX || type == BS_3STATE || type == BS_AUTO3STATE) {
                SetWindowTheme(ctrl, L"", L"");
            } else {
                SetWindowTheme(ctrl, L"DarkMode_Explorer", NULL);
            }
        } else if (lstrcmp(cls, L"ComboBox") == 0) {
            SetWindowTheme(ctrl, L"", L"");
        }

        ctrl = GetWindow(ctrl, GW_HWNDNEXT);
    }

    InvalidateRect(hDlg, NULL, TRUE);
}
