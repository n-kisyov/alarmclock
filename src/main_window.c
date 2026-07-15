#include "main.h"
#include <windowsx.h>
#include "theme.h"
#include "tray.h"
#include "settings_dialog.h"
#include "alarm_dialog.h"
#include "alarms.h"
#include "clock_renderer.h"
#include "sound.h"
#include "settings_data.h"

#define ALARM_PAD_X      10
#define ALARM_PAD_Y      8
#define ALARM_ROW_H      30
#define ALARM_HEADER_H   22
#define ALARM_CHK_SIZE   18
#define ALARM_BTN_W      52
#define ALARM_BTN_H      22
#define ALARM_BTN_GAP    5
#define SEP_MARGIN       8

/* ---------- layout helpers ---------- */

static void calc_clock_rect(HWND hwnd, RECT *rc) {
    GetClientRect(hwnd, rc);
    int h = g_state.clockAreaH;
    if (h < 80) h = 260;
    rc->bottom = rc->top + h;
}

static HFONT create_fitted_clock_font(HWND hwnd, const WCHAR *faceName) {
    RECT cr;
    GetClientRect(hwnd, &cr);
    int availW = cr.right - cr.left - SEP_MARGIN * 2 - 8;

    HDC hdc = GetDC(hwnd);
    int lo = 20, hi = 600, best = 60;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        HFONT hTest = CreateFontW(mid, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                   DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                   CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, faceName);
        HFONT hOld = (HFONT)SelectObject(hdc, hTest);
        SIZE sz;
        GetTextExtentPoint32W(hdc, L"00:00:00", 8, &sz);
        SelectObject(hdc, hOld);
        DeleteObject(hTest);
        if (sz.cx > availW * 92 / 100) hi = mid - 1;
        else { best = mid; lo = mid + 1; }
    }
    ReleaseDC(hwnd, hdc);
    return CreateFontW(best, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                        CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, faceName);
}

static void calc_alarm_rects(HWND hwnd, RECT *panel, RECT *header, RECT *srcClock) {
    RECT cr;
    GetClientRect(hwnd, &cr);
    int cw = cr.right - cr.left;
    int sepY = srcClock->bottom;
    int panW = cw - ALARM_PAD_X * 2;
    int panX = ALARM_PAD_X;
    int panY = sepY + 4;
    int availH = cr.bottom - panY - ALARM_PAD_Y;
    int minH = ALARM_HEADER_H + 6 + ALARM_ROW_H + 6;
    int maxH = ALARM_HEADER_H + 6 + g_state.alarm_count * ALARM_ROW_H + 6;
    int panH = availH;
    if (panH < minH) panH = minH;
    if (panH > maxH) panH = maxH;
    panel->left = panX; panel->top = panY;
    panel->right = panX + panW; panel->bottom = panY + panH;
    header->left = panX + 12; header->top = panY + 6;
    header->right = panX + panW - 12; header->bottom = header->top + ALARM_HEADER_H;
}

static RECT get_alarm_row_rect(const RECT *panel, const RECT *header, int idx) {
    RECT r;
    int baseY = header->bottom + 2;
    r.left = panel->left + 8; r.top = baseY + idx * ALARM_ROW_H;
    r.right = panel->right - 8; r.bottom = r.top + ALARM_ROW_H;
    return r;
}
static RECT get_check_rect(const RECT *alarmRow) {
    RECT r;
    int cy = (alarmRow->top + alarmRow->bottom) / 2;
    r.left = alarmRow->left + ALARM_PAD_X; r.top = cy - ALARM_CHK_SIZE / 2;
    r.right = r.left + ALARM_CHK_SIZE; r.bottom = r.top + ALARM_CHK_SIZE;
    return r;
}
static RECT get_edit_rect(const RECT *alarmRow) {
    RECT r;
    r.right = alarmRow->right - 8; r.left = r.right - ALARM_BTN_W;
    r.top = alarmRow->top + (ALARM_ROW_H - ALARM_BTN_H) / 2;
    r.bottom = r.top + ALARM_BTN_H;
    return r;
}
static RECT get_clear_rect(const RECT *alarmRow) {
    RECT edit = get_edit_rect(alarmRow);
    RECT r;
    r.right = edit.left - ALARM_BTN_GAP; r.left = r.right - ALARM_BTN_W + 4;
    r.top = edit.top; r.bottom = edit.bottom;
    return r;
}

/* ---------- drawing helpers ---------- */

static void draw_button(HDC hdc, const RECT *r, const TCHAR *text,
                         COLORREF bg, COLORREF fg) {
    HBRUSH hBr = CreateSolidBrush(bg);
    HPEN   hPn = CreatePen(PS_SOLID, 1, fg);
    HBRUSH hOldBr = (HBRUSH)SelectObject(hdc, hBr);
    HPEN   hOldPn = (HPEN)SelectObject(hdc, hPn);
    RoundRect(hdc, r->left, r->top, r->right, r->bottom, 4, 4);
    SelectObject(hdc, hOldBr); SelectObject(hdc, hOldPn);
    DeleteObject(hBr); DeleteObject(hPn);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, fg);
    HFONT hOldFont = (HFONT)SelectObject(hdc, g_state.hGuiFont);
    DrawText(hdc, text, -1, (RECT *)r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, hOldFont);
}

static void draw_highlighted_button(HDC hdc, const RECT *r, const TCHAR *text,
                                     COLORREF bg, COLORREF fg) {
    COLORREF hlBg = RGB(
        (GetRValue(bg) + 40 > 255 ? 255 : GetRValue(bg) + 40),
        (GetGValue(bg) + 40 > 255 ? 255 : GetGValue(bg) + 40),
        (GetBValue(bg) + 40 > 255 ? 255 : GetBValue(bg) + 40));
    draw_button(hdc, r, text, hlBg, fg);
}

static void draw_alarm_panel(HDC hdc, HWND hwnd, const RECT *clockRect) {
    (void)hwnd;
    AppState *s = &g_state;
    RECT panel, header;
    calc_alarm_rects(hwnd, &panel, &header, (RECT *)clockRect);

    HBRUSH hOldBr = (HBRUSH)SelectObject(hdc, s->hPanelBrush);
    HPEN   hOldPn = (HPEN)SelectObject(hdc, GetStockObject(NULL_PEN));
    Rectangle(hdc, panel.left, panel.top, panel.right, panel.bottom);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, s->textColor);
    HFONT hOldFont = (HFONT)SelectObject(hdc, s->hGuiFont);
    DrawText(hdc, L"Alarms", -1, &header, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, hOldFont);

    for (int i = 0; i < s->alarm_count; i++) {
        RECT rowR  = get_alarm_row_rect(&panel, &header, i);
        RECT chkR  = get_check_rect(&rowR);
        RECT editR = get_edit_rect(&rowR);
        RECT clrR  = get_clear_rect(&rowR);

        HPEN hChkPen = CreatePen(PS_SOLID, 2, s->textColor);
        HBRUSH hChkBr = CreateSolidBrush(
            (s->alarms[i].enabled && s->alarms[i].hour != ALARM_UNSET) ? s->accentColor : s->bgColor);
        SelectObject(hdc, hChkBr); SelectObject(hdc, hChkPen);
        Rectangle(hdc, chkR.left, chkR.top, chkR.right, chkR.bottom);

        if (s->alarms[i].enabled && s->alarms[i].hour != ALARM_UNSET) {
            SetTextColor(hdc, RGB(0xFF, 0xFF, 0xFF));
            SetBkMode(hdc, TRANSPARENT);
            HFONT hTmp = CreateFont(14, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
                                     OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                     DEFAULT_PITCH, L"Segoe UI");
            HFONT hOld = (HFONT)SelectObject(hdc, hTmp);
            DrawText(hdc, L"\x2713", 1, (RECT *)&chkR, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(hdc, hOld); DeleteObject(hTmp);
        }
        DeleteObject(hChkPen); DeleteObject(hChkBr);

        TCHAR timeStr[64];
        if (s->alarms[i].hour >= 0 && s->alarms[i].minute >= 0) {
            int h = s->alarms[i].hour;
            WCHAR ap[4] = L"";
            if (!s->hour24) {
                ap[0] = (h >= 12) ? L'P' : L'A'; ap[1] = L'M';
                if (h == 0) h = 12; else if (h > 12) h -= 12;
            }
            if (s->alarms[i].label[0])
                wsprintf(timeStr, L"%02d:%02d %s %s", h, s->alarms[i].minute, ap, s->alarms[i].label);
            else
                wsprintf(timeStr, L"%02d:%02d %s", h, s->alarms[i].minute, ap);
        } else {
            lstrcpy(timeStr, L"--:--");
        }

        RECT timeR;
        timeR.left = chkR.right + 8; timeR.top = rowR.top + 4;
        timeR.right = clrR.left - 8; timeR.bottom = rowR.bottom - 4;

        SetBkMode(hdc, TRANSPARENT); SetTextColor(hdc, s->textColor);
        HFONT hRowFont = (HFONT)SelectObject(hdc, s->hGuiFont);
        DrawText(hdc, timeStr, -1, &timeR, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        SelectObject(hdc, hRowFont);

        COLORREF btnBg = s->dark_mode ? RGB(0x45, 0x45, 0x45) : RGB(0xE0, 0xE0, 0xE0);
        draw_button(hdc, &editR, L"Edit", btnBg, s->textColor);
        draw_button(hdc, &clrR, L"Clear", btnBg, s->textColor);
    }
    SelectObject(hdc, hOldBr); SelectObject(hdc, hOldPn);
}

/* ---------- mode action bar ---------- */

static void draw_mode_bar(HDC hdc, const RECT *clockRect) {
    AppState *s = &g_state;
    int cx = clockRect->left + (clockRect->right - clockRect->left) / 2;
    int by = clockRect->bottom - 30;
    int bh = 26;

    if (s->alarm_active) {
        RECT r;
        r.top = by; r.bottom = by + bh;
        r.left = cx - 108; r.right = cx - 6;
        draw_button(hdc, &r, L"SNOOZE", RGB(0xDE, 0x87, 0x00), RGB(255,255,255));
        r.left = cx + 6; r.right = cx + 108;
        draw_button(hdc, &r, L"DISMISS", RGB(0xE8, 0x11, 0x23), RGB(255,255,255));
        return;
    }

    if (s->snooze_pending) {
        DWORD remainMs = (s->snooze_end_ms > GetTickCount()) ? (s->snooze_end_ms - GetTickCount()) : 0;
        int rs = (int)(remainMs / 1000);
        WCHAR buf[32];
        wsprintfW(buf, L"Snoozed  %d:%02d", rs / 60, rs % 60);
        SetBkMode(hdc, TRANSPARENT); SetTextColor(hdc, s->textColor);
        HFONT hOld = (HFONT)SelectObject(hdc, s->hGuiFont);
        RECT tr; tr.left = cx - 100; tr.top = by; tr.right = cx + 100; tr.bottom = by + 14;
        DrawTextW(hdc, buf, -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(hdc, hOld);
        RECT btn; btn.left = cx - 52; btn.top = by; btn.right = cx + 52; btn.bottom = by + bh;
        draw_button(hdc, &btn, L"CANCEL", RGB(0xE8, 0x11, 0x23), RGB(255,255,255));
        return;
    }

    if (s->app_mode == APP_MODE_COUNTDOWN) {
        RECT cb;
        cb.top = by; cb.bottom = by + bh;
        cb.left = cx + 90; cb.right = cx + 135;
        COLORREF cbBg = s->dark_mode ? RGB(0x45,0x45,0x45) : RGB(0xE0,0xE0,0xE0);
        draw_button(hdc, &cb, L"Clock", cbBg, s->textColor);

        if (s->cd_running) {
            RECT r; r.top = by; r.bottom = by + bh;
            r.left = cx - 80; r.right = cx - 5;
            draw_button(hdc, &r, L"Pause", s->accentColor, RGB(255,255,255));
            r.left = cx + 5; r.right = cx + 80;
            draw_button(hdc, &r, L"Reset", s->dark_mode ? RGB(0x50,0x50,0x50) : RGB(0xD0,0xD0,0xD0), s->textColor);
        } else {
            RECT r; r.top = by; r.bottom = by + bh;
            r.left = cx - 80; r.right = cx - 5;
            if (s->cd_remaining_ms > 0)
                draw_button(hdc, &r, L"Start", RGB(0x00,0x88,0x00), RGB(255,255,255));
            else
                draw_button(hdc, &r, L"Set", s->accentColor, RGB(255,255,255));
            r.left = cx + 5; r.right = cx + 80;
            draw_button(hdc, &r, L"Reset", s->dark_mode ? RGB(0x50,0x50,0x50) : RGB(0xD0,0xD0,0xD0), s->textColor);
        }
        return;
    }

    if (s->app_mode == APP_MODE_STOPWATCH) {
        RECT cb;
        cb.top = by; cb.bottom = by + bh;
        cb.left = cx + 90; cb.right = cx + 135;
        COLORREF cbBg = s->dark_mode ? RGB(0x45,0x45,0x45) : RGB(0xE0,0xE0,0xE0);
        draw_button(hdc, &cb, L"Clock", cbBg, s->textColor);

        if (s->sw_running) {
            RECT r; r.top = by; r.bottom = by + bh;
            r.left = cx - 80; r.right = cx - 5;
            draw_button(hdc, &r, L"Stop", RGB(0xCC,0x33,0x00), RGB(255,255,255));
            r.left = cx + 5; r.right = cx + 80;
            draw_button(hdc, &r, L"Reset", s->dark_mode ? RGB(0x50,0x50,0x50) : RGB(0xD0,0xD0,0xD0), s->textColor);
        } else {
            RECT r; r.top = by; r.bottom = by + bh;
            r.left = cx - 80; r.right = cx - 5;
            draw_button(hdc, &r, L"Start", RGB(0x00,0x88,0x00), RGB(255,255,255));
            r.left = cx + 5; r.right = cx + 80;
            draw_button(hdc, &r, L"Reset", s->dark_mode ? RGB(0x50,0x50,0x50) : RGB(0xD0,0xD0,0xD0), s->textColor);
        }
        return;
    }

    /* Clock mode — mode toggle buttons */
    RECT r; r.top = by; r.bottom = by + bh;
    r.left = cx - 135; r.right = cx - 65;
    if (s->app_mode == APP_MODE_CLOCK)
        draw_highlighted_button(hdc, &r, L"Clock", s->accentColor, RGB(255,255,255));
    else
        draw_button(hdc, &r, L"Clock", s->dark_mode ? RGB(0x45,0x45,0x45) : RGB(0xE0,0xE0,0xE0), s->textColor);

    r.left = cx - 58; r.right = cx + 10;
    draw_button(hdc, &r, L"Timer", s->dark_mode ? RGB(0x45,0x45,0x45) : RGB(0xE0,0xE0,0xE0), s->textColor);

    r.left = cx + 18; r.right = cx + 85;
    draw_button(hdc, &r, L"Stopw.", s->dark_mode ? RGB(0x45,0x45,0x45) : RGB(0xE0,0xE0,0xE0), s->textColor);
}

/* ---------- snooze / dismiss (forward) ---------- */
static void snooze_alarm(void);
static void dismiss_alarm(void);

/* ---------- mode bar hit testing ---------- */

static void on_mode_click(HWND hwnd, int mx, int my, const RECT *clockRect) {
    AppState *s = &g_state;
    int cx = clockRect->left + (clockRect->right - clockRect->left) / 2;
    int by = clockRect->bottom - 30;
    int bh = 26;

    if (s->alarm_active) {
        RECT sr = {cx - 108, by, cx - 6, by + bh};
        RECT dr = {cx + 6, by, cx + 108, by + bh};
        if (PtInRect(&sr, (POINT){mx, my})) { snooze_alarm(); return; }
        if (PtInRect(&dr, (POINT){mx, my})) { dismiss_alarm(); return; }
        return;
    }
    if (s->snooze_pending) {
        RECT cr = {cx - 52, by, cx + 52, by + bh};
        if (PtInRect(&cr, (POINT){mx, my})) { dismiss_alarm(); return; }
        return;
    }

    if (s->app_mode == APP_MODE_COUNTDOWN) {
        RECT cb = {cx + 90, by, cx + 135, by + bh};
        if (PtInRect(&cb, (POINT){mx, my})) {
            s->app_mode = APP_MODE_CLOCK;
            s->cd_running = FALSE;
            InvalidateRect(hwnd, NULL, FALSE);
            return;
        }
        if (s->cd_running) {
            RECT r = {cx - 80, by, cx - 5, by + bh};
            if (PtInRect(&r, (POINT){mx, my})) { s->cd_running = FALSE; InvalidateRect(hwnd,NULL,FALSE); return; }
            r = (RECT){cx + 5, by, cx + 80, by + bh};
            if (PtInRect(&r, (POINT){mx, my})) {
                s->cd_running = FALSE;
                s->cd_remaining_ms = (s->cd_hours*3600 + s->cd_mins*60 + s->cd_secs)*1000;
                InvalidateRect(hwnd,NULL,FALSE); return;
            }
        } else {
            RECT r = {cx - 80, by, cx - 5, by + bh};
            if (PtInRect(&r, (POINT){mx, my})) {
                if (s->cd_remaining_ms > 0) {
                    s->cd_running = TRUE; s->cd_last_tick = GetTickCount();
                } else {
                    DialogBoxParamW(GetModuleHandle(NULL), MAKEINTRESOURCEW(IDD_COUNTDOWN_SET),
                                    hwnd, cd_set_dlg_proc, (LPARAM)s);
                    InvalidateRect(hwnd,NULL,FALSE);
                }
                return;
            }
        }
        return;
    }

    if (s->app_mode == APP_MODE_STOPWATCH) {
        RECT cb = {cx + 90, by, cx + 135, by + bh};
        if (PtInRect(&cb, (POINT){mx, my})) {
            s->app_mode = APP_MODE_CLOCK;
            s->sw_running = FALSE;
            InvalidateRect(hwnd, NULL, FALSE);
            return;
        }
        if (s->sw_running) {
            RECT r = {cx - 80, by, cx - 5, by + bh};
            if (PtInRect(&r, (POINT){mx, my})) {
                s->sw_accumulated_ms += GetTickCount() - s->sw_start_tick;
                s->sw_running = FALSE; InvalidateRect(hwnd,NULL,FALSE); return;
            }
            r = (RECT){cx + 5, by, cx + 80, by + bh};
            if (PtInRect(&r, (POINT){mx, my})) {
                s->sw_running = FALSE; s->sw_accumulated_ms = 0; InvalidateRect(hwnd,NULL,FALSE); return;
            }
        } else {
            RECT r = {cx - 80, by, cx - 5, by + bh};
            if (PtInRect(&r, (POINT){mx, my})) {
                s->sw_running = TRUE; s->sw_start_tick = GetTickCount(); InvalidateRect(hwnd,NULL,FALSE); return;
            }
            r = (RECT){cx + 5, by, cx + 80, by + bh};
            if (PtInRect(&r, (POINT){mx, my})) {
                s->sw_accumulated_ms = 0; InvalidateRect(hwnd,NULL,FALSE); return;
            }
        }
        return;
    }

    /* Clock mode — mode toggle buttons */
    RECT cr = {cx - 135, by, cx - 65, by + bh};
    if (PtInRect(&cr, (POINT){mx, my})) return; /* already clock */

    RECT tr = {cx - 58, by, cx + 10, by + bh};
    if (PtInRect(&tr, (POINT){mx, my})) {
        s->app_mode = APP_MODE_COUNTDOWN;
        if (s->cd_remaining_ms == 0)
            s->cd_remaining_ms = (s->cd_hours*3600 + s->cd_mins*60 + s->cd_secs)*1000;
        s->sw_running = FALSE;
        InvalidateRect(hwnd,NULL,FALSE); return;
    }
    RECT wr = {cx + 18, by, cx + 85, by + bh};
    if (PtInRect(&wr, (POINT){mx, my})) {
        s->app_mode = APP_MODE_STOPWATCH;
        s->cd_running = FALSE;
        InvalidateRect(hwnd,NULL,FALSE); return;
    }
}

/* ---------- snooze / dismiss ---------- */

static void snooze_alarm(void) {
    AppState *s = &g_state;
    s->snooze_total_sec = s->snooze_minutes * 60;
    s->snooze_end_ms = GetTickCount() + (DWORD)s->snooze_total_sec * 1000;
    s->snooze_pending = TRUE;
    s->alarm_active = FALSE;
    sound_stop_alarm(s);
    InvalidateRect(s->hMainWnd, NULL, FALSE);
}

static void dismiss_alarm(void) {
    AppState *s = &g_state;
    s->alarm_active = FALSE;
    s->snooze_pending = FALSE;
    sound_stop_alarm(s);
    InvalidateRect(s->hMainWnd, NULL, FALSE);
}

/* ---------- countdown set dialog ---------- */

INT_PTR CALLBACK cd_set_dlg_proc(HWND hDlg, UINT msg, WPARAM wp, LPARAM lp) {
    static AppState *s;
    switch (msg) {
    case WM_INITDIALOG:
        s = (AppState *)lp;
        theme_apply(hDlg, s->dark_mode);
        { TCHAR b[8]; wsprintf(b,L"%d",s->cd_hours);   SetDlgItemText(hDlg,IDC_CD_HOURS,b); }
        { TCHAR b[8]; wsprintf(b,L"%d",s->cd_mins);    SetDlgItemText(hDlg,IDC_CD_MINS,b); }
        { TCHAR b[8]; wsprintf(b,L"%d",s->cd_secs);    SetDlgItemText(hDlg,IDC_CD_SECS,b); }
        return TRUE;
    case WM_CTLCOLORSTATIC:
        if (s->dark_mode) { SetTextColor((HDC)wp,s->textColor); SetBkColor((HDC)wp,s->bgColor); SetBkMode((HDC)wp,TRANSPARENT); }
        return (INT_PTR)s->hBgBrush;
    case WM_CTLCOLOREDIT:
        if (s->dark_mode) { SetTextColor((HDC)wp,s->textColor); SetBkColor((HDC)wp,s->panelBgColor); }
        return (INT_PTR)s->hPanelBrush;
    case WM_CTLCOLORBTN: return (INT_PTR)s->hBgBrush;
    case WM_CTLCOLORDLG: return (INT_PTR)s->hBgBrush;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDOK: {
            TCHAR b[16];
            GetDlgItemText(hDlg,IDC_CD_HOURS,b,16); s->cd_hours = _wtoi(b);
            GetDlgItemText(hDlg,IDC_CD_MINS,b,16);  s->cd_mins  = _wtoi(b);
            GetDlgItemText(hDlg,IDC_CD_SECS,b,16);  s->cd_secs  = _wtoi(b);
            if (s->cd_hours<0) s->cd_hours=0;
            if (s->cd_mins<0)  s->cd_mins=0;
            if (s->cd_secs<0)  s->cd_secs=0;
            if (s->cd_hours==0 && s->cd_mins==0 && s->cd_secs==0) s->cd_mins=1;
            s->cd_remaining_ms = (s->cd_hours*3600 + s->cd_mins*60 + s->cd_secs)*1000;
            s->cd_running = FALSE;
            EndDialog(hDlg, IDOK);
            return TRUE;
        }
        case IDCANCEL: EndDialog(hDlg, IDCANCEL); return TRUE;
        }
        break;
    }
    return FALSE;
}

/* ---------- paint ---------- */

static void on_paint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdcScreen = BeginPaint(hwnd, &ps);
    RECT cr; GetClientRect(hwnd, &cr);
    int cw = cr.right - cr.left, ch = cr.bottom - cr.top;

    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP hBmp = CreateCompatibleBitmap(hdcScreen, cw, ch);
    HBITMAP hOldBmp = (HBITMAP)SelectObject(hdcMem, hBmp);

    HBRUSH hBgBr = CreateSolidBrush(g_state.bgColor);
    FillRect(hdcMem, &cr, hBgBr);
    DeleteObject(hBgBr);

    RECT clockRect;
    calc_clock_rect(hwnd, &clockRect);

    HPEN hSep = CreatePen(PS_SOLID, 1,
        g_state.dark_mode ? RGB(0x50,0x50,0x50) : RGB(0xC0,0xC0,0xC0));
    SelectObject(hdcMem, hSep);
    MoveToEx(hdcMem, cr.left + SEP_MARGIN, clockRect.bottom, NULL);
    LineTo(hdcMem, cr.right - SEP_MARGIN, clockRect.bottom);
    DeleteObject(hSep);

    SYSTEMTIME st;
    GetLocalTime(&st);

    RECT clkInner = clockRect;
    clkInner.top += 3; clkInner.bottom -= 36;

    if (g_state.app_mode == APP_MODE_COUNTDOWN) {
        DWORD now = GetTickCount();
        if (g_state.cd_running) {
            int elapsed = (int)(now - g_state.cd_last_tick);
            g_state.cd_last_tick = now;
            g_state.cd_remaining_ms -= elapsed;
            if (g_state.cd_remaining_ms <= 0) {
                g_state.cd_remaining_ms = 0;
                g_state.cd_running = FALSE;
                g_state.alarm_active = TRUE;
                sound_play_alarm(&g_state);
            }
        }
        COLORREF tc = g_state.cd_remaining_ms < 10000 ? RGB(0xFF,0x40,0x40) : g_state.clockColor;
        clock_draw_countdown(hdcMem, &clkInner, g_state.cd_remaining_ms, tc, &g_state);
    } else if (g_state.app_mode == APP_MODE_STOPWATCH) {
        DWORD elapsed = g_state.sw_accumulated_ms;
        if (g_state.sw_running) elapsed += GetTickCount() - g_state.sw_start_tick;
        clock_draw_stopwatch(hdcMem, &clkInner, elapsed, &g_state);
    } else if (g_state.clock_style == CLOCK_ANALOG) {
        clock_draw_analog(hdcMem, &clkInner, &st, &g_state);
    } else {
        clock_draw_digital(hdcMem, &clkInner, &st, &g_state);
    }

    draw_mode_bar(hdcMem, &clockRect);
    draw_alarm_panel(hdcMem, hwnd, &clockRect);

    if (g_state.acrylic) {
        BLENDFUNCTION bf = { AC_SRC_OVER, 0, 220, 0 };
        AlphaBlend(hdcScreen, cr.left, cr.top, cw, ch, hdcMem, 0, 0, cw, ch, bf);
    } else {
        BitBlt(hdcScreen, cr.left, cr.top, cw, ch, hdcMem, 0, 0, SRCCOPY);
    }

    SelectObject(hdcMem, hOldBmp);
    DeleteObject(hBmp); DeleteDC(hdcMem);
    EndPaint(hwnd, &ps);
}

/* ---------- message handlers ---------- */

static void on_lbuttondown(HWND hwnd, LPARAM lp) {
    int mx = GET_X_LPARAM(lp), my = GET_Y_LPARAM(lp);
    RECT clockRect; calc_clock_rect(hwnd, &clockRect);

    on_mode_click(hwnd, mx, my, &clockRect);

    RECT panel, header;
    calc_alarm_rects(hwnd, &panel, &header, &clockRect);
    for (int i = 0; i < g_state.alarm_count; i++) {
        RECT rowR = get_alarm_row_rect(&panel, &header, i);
        RECT chkR = get_check_rect(&rowR);
        RECT editR = get_edit_rect(&rowR);
        RECT clrR = get_clear_rect(&rowR);

        if (PtInRect(&chkR, (POINT){mx, my})) {
            if (g_state.alarms[i].hour != ALARM_UNSET) {
                g_state.alarms[i].enabled = !g_state.alarms[i].enabled;
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return;
        }
        if (PtInRect(&editR, (POINT){mx, my})) {
            AlarmEditData data;
            data.hour = g_state.alarms[i].hour;
            data.minute = g_state.alarms[i].minute;
            data.enabled = g_state.alarms[i].enabled;
            data.repeat_days = g_state.alarms[i].repeat_days;
            lstrcpyW(data.label, g_state.alarms[i].label);
            if (DialogBoxParamW(GetModuleHandle(NULL), MAKEINTRESOURCEW(IDD_ALARM),
                                hwnd, alarm_dlg_proc, (LPARAM)&data) == IDOK) {
                g_state.alarms[i].hour = data.hour;
                g_state.alarms[i].minute = data.minute;
                g_state.alarms[i].enabled = data.enabled;
                g_state.alarms[i].repeat_days = data.repeat_days;
                lstrcpyW(g_state.alarms[i].label, data.label);
                settings_save(&g_state);
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return;
        }
        if (PtInRect(&clrR, (POINT){mx, my})) {
            g_state.alarms[i].hour = ALARM_UNSET;
            g_state.alarms[i].minute = ALARM_UNSET;
            g_state.alarms[i].enabled = FALSE;
            g_state.alarms[i].repeat_days = 0;
            g_state.alarms[i].label[0] = 0;
            settings_save(&g_state);
            InvalidateRect(hwnd, NULL, FALSE);
            return;
        }
    }
}

static LRESULT on_create(HWND hwnd) {
    AppState *s = &g_state;
    s->hMainWnd = hwnd;
    theme_update_colors(s);

    lstrcpyW(s->clockFaceName, L"Consolas");
    HINSTANCE hInst = (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE);
    HRSRC hFontRes = FindResourceW(hInst, MAKEINTRESOURCEW(IDR_DIGITALFONT), RT_RCDATA);
    if (hFontRes) {
        HGLOBAL hMem = LoadResource(hInst, hFontRes);
        if (hMem) {
            void *fontData = LockResource(hMem);
            if (fontData && SizeofResource(hInst, hFontRes) > 0) {
                DWORD nf = 0;
                if (AddFontMemResourceEx(fontData, SizeofResource(hInst, hFontRes), NULL, &nf) && nf > 0)
                    lstrcpyW(s->clockFaceName, L"Digital-7 Mono");
            }
        }
    }

    s->hClockFont = create_fitted_clock_font(hwnd, s->clockFaceName);

    HDC hdc = GetDC(hwnd);
    TEXTMETRICW tmClock;
    HFONT hOld = (HFONT)SelectObject(hdc, s->hClockFont);
    GetTextMetricsW(hdc, &tmClock);
    int dateH = tmClock.tmHeight / 6;
    if (dateH < 18) dateH = 18;
    SelectObject(hdc, hOld);
    ReleaseDC(hwnd, hdc);

    s->hDateFont = CreateFontW(dateH,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");

    hdc = GetDC(hwnd);
    SelectObject(hdc, s->hClockFont); GetTextMetricsW(hdc, &tmClock);
    TEXTMETRICW tmDate;
    SelectObject(hdc, s->hDateFont); GetTextMetricsW(hdc, &tmDate);
    SelectObject(hdc, hOld); ReleaseDC(hwnd, hdc);

    if (s->clock_style == CLOCK_ANALOG) {
        RECT cr; GetClientRect(hwnd, &cr);
        int box = (cr.right - cr.left) - SEP_MARGIN*2;
        s->clockAreaH = box + 10;
    } else {
        int gap = tmClock.tmHeight / 10;
        s->clockAreaH = tmClock.tmHeight + gap + tmDate.tmHeight + 6 + 34;
    }

    s->hGuiFont = CreateFontW(16,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");

    theme_apply(hwnd, s->dark_mode);
    tray_create(hwnd, s);
    SetTimer(hwnd, TIMER_CLOCK, 50, NULL);
    return 0;
}

static void on_command(HWND hwnd, WPARAM wp) {
    AppState *s = &g_state;
    switch (LOWORD(wp)) {
    case IDM_SETTINGS:
        if (DialogBoxParamW(GetModuleHandle(NULL), MAKEINTRESOURCEW(IDD_SETTINGS), hwnd, settings_dlg_proc, (LPARAM)s) == IDOK) {
            settings_save(s);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        break;
    case IDM_ABOUT:
        MessageBoxW(hwnd, L"AlarmClock v1.2\nA simple alarm clock for Windows.", L"About AlarmClock", MB_OK|MB_ICONINFORMATION);
        break;
    case IDM_EXIT:
        sound_stop_alarm(s); DestroyWindow(hwnd); break;
    case IDM_TRAY_SHOW:
        AnimateWindow(hwnd, 200, AW_BLEND);
        ShowWindow(hwnd, SW_SHOW); SetForegroundWindow(hwnd); break;
    case IDM_TRAY_EXIT:
        sound_stop_alarm(s); DestroyWindow(hwnd); break;
    }
}

static void on_timer(HWND hwnd) {
    AppState *s = &g_state;
    SYSTEMTIME st;
    GetLocalTime(&st);
    static int lastAlarmSec = -1;

    if ((int)st.wSecond != lastAlarmSec) {
        lastAlarmSec = (int)st.wSecond;

        if (alarms_check(s, &st)) {
            sound_play_alarm(s);
            if (IsIconic(hwnd)) {
                AnimateWindow(hwnd, 200, AW_BLEND);
            }
            ShowWindow(hwnd, SW_SHOW);
            SetForegroundWindow(hwnd);

            /* Tray balloon */
            if (IsIconic(hwnd) || !IsWindowVisible(hwnd)) {
                s->nid.uFlags |= NIF_INFO;
                s->nid.dwInfoFlags = NIIF_USER;
                lstrcpyW(s->nid.szInfoTitle, L"AlarmClock");
                s->nid.szInfo[0] = 0;
                if (g_state.alarms_enabled) {
                    for (int i = 0; i < s->alarm_count; i++) {
                        if (s->alarms[i].hour == (int)st.wHour && s->alarms[i].minute == (int)st.wMinute) {
                            wsprintfW(s->nid.szInfo, L"%02d:%02d  %s",
                                      s->alarms[i].hour, s->alarms[i].minute,
                                      s->alarms[i].label[0] ? s->alarms[i].label : L"Alarm");
                            break;
                        }
                    }
                }
                Shell_NotifyIconW(NIM_MODIFY, &s->nid);
            }
        }

        if (s->snooze_pending && GetTickCount() >= s->snooze_end_ms) {
            s->snooze_pending = FALSE;
            s->alarm_active = TRUE;
            s->last_fire_min = (int)st.wHour * 60 + (int)st.wMinute;
            sound_play_alarm(s);
            ShowWindow(hwnd, SW_SHOW);
            SetForegroundWindow(hwnd);
        }

        tray_update_tooltip(s);
    }

    InvalidateRect(hwnd, NULL, FALSE);
}

static void on_size(HWND hwnd, WPARAM wp) {
    AppState *s = &g_state;
    if (wp == SIZE_MINIMIZED) return;
    if (s->hMainWnd != hwnd) return;

    RECT cr; GetClientRect(hwnd, &cr);
    int availW = cr.right - cr.left - SEP_MARGIN*2 - 8;
    if (availW < 40) return;

    if (s->hClockFont) { DeleteObject(s->hClockFont); s->hClockFont = NULL; }
    if (s->hDateFont)  { DeleteObject(s->hDateFont);  s->hDateFont  = NULL; }

    s->hClockFont = create_fitted_clock_font(hwnd, s->clockFaceName);

    TEXTMETRICW tm;
    HDC hdc = GetDC(hwnd);
    HFONT hOld = (HFONT)SelectObject(hdc, s->hClockFont);
    GetTextMetricsW(hdc, &tm);
    int dateH = tm.tmHeight / 6;
    if (dateH < 18) dateH = 18;
    SelectObject(hdc, hOld); ReleaseDC(hwnd, hdc);

    s->hDateFont = CreateFontW(dateH,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");

    hdc = GetDC(hwnd);
    SelectObject(hdc, s->hClockFont); GetTextMetricsW(hdc, &tm);
    TEXTMETRICW tmDate;
    SelectObject(hdc, s->hDateFont); GetTextMetricsW(hdc, &tmDate);
    SelectObject(hdc, hOld); ReleaseDC(hwnd, hdc);

    if (s->clock_style == CLOCK_ANALOG) {
        int box = (cr.right - cr.left) - SEP_MARGIN*2;
        s->clockAreaH = box + 10;
    } else {
        int gap = tm.tmHeight / 10;
        s->clockAreaH = tm.tmHeight + gap + tmDate.tmHeight + 6 + 34;
    }

    InvalidateRect(hwnd, NULL, TRUE);
}

static void on_destroy(HWND hwnd) {
    AppState *s = &g_state;
    KillTimer(hwnd, TIMER_CLOCK);
    sound_stop_alarm(s);

    WINDOWPLACEMENT wp; wp.length = sizeof(wp);
    if (GetWindowPlacement(hwnd, &wp)) {
        s->winX = wp.rcNormalPosition.left;
        s->winY = wp.rcNormalPosition.top;
        s->winW = wp.rcNormalPosition.right - wp.rcNormalPosition.left;
        s->winH = wp.rcNormalPosition.bottom - wp.rcNormalPosition.top;
    }

    settings_save(s);
    tray_remove(s);

    if (s->hClockFont) DeleteObject(s->hClockFont);
    if (s->hDateFont)  DeleteObject(s->hDateFont);
    if (s->hGuiFont)   DeleteObject(s->hGuiFont);
    if (s->hBgBrush)   DeleteObject(s->hBgBrush);
    if (s->hPanelBrush)DeleteObject(s->hPanelBrush);

    PostQuitMessage(0);
}

LRESULT CALLBACK main_wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: return on_create(hwnd);
    case WM_PAINT: on_paint(hwnd); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_TIMER: if (wp == TIMER_CLOCK) on_timer(hwnd); return 0;
    case WM_GETMINMAXINFO: { MINMAXINFO *mmi = (MINMAXINFO *)lp; mmi->ptMinTrackSize.x=400; mmi->ptMinTrackSize.y=340; return 0; }
    case WM_SIZE: on_size(hwnd, wp); return 0;
    case WM_COMMAND: on_command(hwnd, wp); return 0;
    case WM_LBUTTONDOWN: on_lbuttondown(hwnd, lp); return 0;
    case WM_CLOSE:
        AnimateWindow(hwnd, 200, AW_HIDE | AW_BLEND);
        ShowWindow(hwnd, SW_HIDE); return 0;
    case WM_DESTROY: on_destroy(hwnd); return 0;
    case WM_TRAYICON:
        if (LOWORD(lp) == WM_RBUTTONUP) tray_show_menu(hwnd, &g_state);
        else if (LOWORD(lp) == WM_LBUTTONDBLCLK) {
            AnimateWindow(hwnd, 200, AW_BLEND);
            ShowWindow(hwnd, SW_SHOW); SetForegroundWindow(hwnd);
        }
        return 0;
    case MM_MCINOTIFY: sound_on_mci_notify(&g_state); return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
