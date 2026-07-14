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

        if (sz.cx > availW * 92 / 100) {
            hi = mid - 1;
        } else {
            best = mid;
            lo = mid + 1;
        }
    }

    ReleaseDC(hwnd, hdc);

    return CreateFontW(best, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                        CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, faceName);
}

static void calc_alarm_rects(HWND hwnd, RECT *panel, RECT *header,
                              RECT *srcClock) {
    RECT cr;
    GetClientRect(hwnd, &cr);
    int cw = cr.right - cr.left;
    int sepY = srcClock->bottom;

    int panW = cw - ALARM_PAD_X * 2;
    int panX = ALARM_PAD_X;
    int panY = sepY + 4;
    int availH = cr.bottom - panY - ALARM_PAD_Y;

    int minH = ALARM_HEADER_H + 6 + ALARM_ROW_H + 6;  /* 1 row visible */
    int maxH = ALARM_HEADER_H + 6 + g_state.alarm_count * ALARM_ROW_H + 6;  /* all rows */

    int panH = availH;
    if (panH < minH) panH = minH;
    if (panH > maxH) panH = maxH;

    panel->left   = panX;
    panel->top    = panY;
    panel->right  = panX + panW;
    panel->bottom = panY + panH;

    header->left   = panX + 12;
    header->top    = panY + 6;
    header->right  = panX + panW - 12;
    header->bottom = header->top + ALARM_HEADER_H;
}

static RECT get_alarm_row_rect(const RECT *panel, const RECT *header, int idx) {
    RECT r;
    int baseY = header->bottom + 2;
    r.left   = panel->left + 8;
    r.top    = baseY + idx * ALARM_ROW_H;
    r.right  = panel->right - 8;
    r.bottom = r.top + ALARM_ROW_H;
    return r;
}

static RECT get_check_rect(const RECT *alarmRow) {
    RECT r;
    int cy = (alarmRow->top + alarmRow->bottom) / 2;
    r.left   = alarmRow->left + ALARM_PAD_X;
    r.top    = cy - ALARM_CHK_SIZE / 2;
    r.right  = r.left + ALARM_CHK_SIZE;
    r.bottom = r.top + ALARM_CHK_SIZE;
    return r;
}

static RECT get_edit_rect(const RECT *alarmRow) {
    RECT r;
    r.right  = alarmRow->right - 8;
    r.left   = r.right - ALARM_BTN_W;
    r.top    = alarmRow->top + (ALARM_ROW_H - ALARM_BTN_H) / 2;
    r.bottom = r.top + ALARM_BTN_H;
    return r;
}

static RECT get_clear_rect(const RECT *alarmRow) {
    RECT edit = get_edit_rect(alarmRow);
    RECT r;
    r.right  = edit.left - ALARM_BTN_GAP;
    r.left   = r.right - ALARM_BTN_W + 4;
    r.top    = edit.top;
    r.bottom = edit.bottom;
    return r;
}

static void draw_button(HDC hdc, const RECT *r, const TCHAR *text,
                         COLORREF bg, COLORREF fg) {
    HBRUSH hBr = CreateSolidBrush(bg);
    HPEN   hPn = CreatePen(PS_SOLID, 1, fg);

    HBRUSH hOldBr = (HBRUSH)SelectObject(hdc, hBr);
    HPEN   hOldPn = (HPEN)SelectObject(hdc, hPn);

    RoundRect(hdc, r->left, r->top, r->right, r->bottom, 4, 4);

    SelectObject(hdc, hOldBr);
    SelectObject(hdc, hOldPn);
    DeleteObject(hBr);
    DeleteObject(hPn);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, fg);
    HFONT hOldFont = (HFONT)SelectObject(hdc, g_state.hGuiFont);
    DrawText(hdc, text, -1, (RECT *)r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, hOldFont);
}

static void draw_alarm_panel(HDC hdc, HWND hwnd, const RECT *clockRect) {
    AppState *s = &g_state;

    RECT panel, header;
    calc_alarm_rects(hwnd, &panel, &header, (RECT *)clockRect);

    /* Panel background */
    HBRUSH hOldBr = (HBRUSH)SelectObject(hdc, s->hPanelBrush);
    HPEN   hOldPn = (HPEN)SelectObject(hdc, GetStockObject(NULL_PEN));
    Rectangle(hdc, panel.left, panel.top, panel.right, panel.bottom);

    /* Header */
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

        /* Checkbox */
        HPEN hChkPen = CreatePen(PS_SOLID, 2, s->textColor);
        HBRUSH hChkBr = CreateSolidBrush(
            (s->alarms[i].enabled && s->alarms[i].hour != ALARM_UNSET)
                ? s->accentColor : s->bgColor);
        SelectObject(hdc, hChkBr);
        SelectObject(hdc, hChkPen);
        Rectangle(hdc, chkR.left, chkR.top, chkR.right, chkR.bottom);

        /* Checkmark */
        if (s->alarms[i].enabled && s->alarms[i].hour != ALARM_UNSET) {
            SetTextColor(hdc, RGB(0xFF, 0xFF, 0xFF));
            SetBkMode(hdc, TRANSPARENT);
            HFONT hTmp = CreateFont(14, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
                                     0, 0, 0, 0, L"Segoe UI");
            HFONT hOld = (HFONT)SelectObject(hdc, hTmp);
            DrawText(hdc, L"\x2713", 1, (RECT *)&chkR, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(hdc, hOld);
            DeleteObject(hTmp);
        }
        DeleteObject(hChkPen);
        DeleteObject(hChkBr);

        /* Time + label text */
        TCHAR timeStr[64];
        if (s->alarms[i].hour >= 0 && s->alarms[i].minute >= 0) {
            int h = s->alarms[i].hour;
            WCHAR ap[4] = L"";
            if (!s->hour24) {
                ap[0] = (h >= 12) ? L'P' : L'A';
                ap[1] = L'M';
                if (h == 0) h = 12;
                else if (h > 12) h -= 12;
            }
            if (s->alarms[i].label[0]) {
                wsprintf(timeStr, L"%02d:%02d %s %s",
                         h, s->alarms[i].minute, ap, s->alarms[i].label);
            } else {
                wsprintf(timeStr, L"%02d:%02d %s",
                         h, s->alarms[i].minute, ap);
            }
        } else {
            lstrcpy(timeStr, L"--:--");
        }

        RECT timeR;
        timeR.left   = chkR.right + 8;
        timeR.top    = rowR.top + 4;
        timeR.right  = clrR.left - 8;
        timeR.bottom = rowR.bottom - 4;

        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, s->textColor);
        HFONT hRowFont = (HFONT)SelectObject(hdc, s->hGuiFont);
        DrawText(hdc, timeStr, -1, &timeR, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        SelectObject(hdc, hRowFont);

        /* Edit button */
        COLORREF btnBg = s->dark_mode ? RGB(0x45, 0x45, 0x45) : RGB(0xE0, 0xE0, 0xE0);
        draw_button(hdc, &editR, L"Edit", btnBg, s->textColor);

        /* Clear button */
        draw_button(hdc, &clrR, L"Clear", btnBg, s->textColor);
    }

    SelectObject(hdc, hOldBr);
    SelectObject(hdc, hOldPn);
}

static void draw_dismiss_button(HDC hdc, const RECT *clockRect) {
    AppState *s = &g_state;
    if (!s->alarm_active && !s->snooze_pending) return;

    int cx = clockRect->left + (clockRect->right - clockRect->left) / 2;

    if (s->snooze_pending) {
        /* Countdown display during snooze */
        DWORD remainMs = (s->snooze_end_ms > GetTickCount()) ? (s->snooze_end_ms - GetTickCount()) : 0;
        int remainSec = (int)(remainMs / 1000);
        int remainMin = remainSec / 60;
        remainSec = remainSec % 60;

        WCHAR buf[32];
        wsprintfW(buf, L"Snoozed  %d:%02d", remainMin, remainSec);

        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, s->textColor);
        HFONT hOld = (HFONT)SelectObject(hdc, s->hGuiFont);
        RECT r;
        r.left   = cx - 100;
        r.top    = clockRect->bottom - 30;
        r.right  = cx + 100;
        r.bottom = r.top + 14;
        DrawTextW(hdc, buf, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(hdc, hOld);

        /* Cancel snooze button */
        RECT btn;
        btn.left   = cx - 52;
        btn.top    = clockRect->bottom - 30;
        btn.right  = cx + 52;
        btn.bottom = btn.top + 26;
        draw_button(hdc, &btn, L"CANCEL", RGB(0xE8, 0x11, 0x23), RGB(0xFF, 0xFF, 0xFF));
        return;
    }

    RECT r;
    r.top    = clockRect->bottom - 30;
    r.bottom = r.top + 26;

    r.left   = cx - 108;
    r.right  = cx - 6;
    draw_button(hdc, &r, L"SNOOZE", RGB(0xDE, 0x87, 0x00), RGB(0xFF, 0xFF, 0xFF));

    r.left   = cx + 6;
    r.right  = cx + 108;
    draw_button(hdc, &r, L"DISMISS", RGB(0xE8, 0x11, 0x23), RGB(0xFF, 0xFF, 0xFF));
}

static RECT get_dismiss_rect(const RECT *clockRect) {
    int cx = clockRect->left + (clockRect->right - clockRect->left) / 2;
    RECT r;
    if (g_state.snooze_pending) {
        r.left   = cx - 52;
        r.top    = clockRect->bottom - 30;
        r.right  = cx + 52;
        r.bottom = r.top + 26;
    } else {
        r.left   = cx + 6;
        r.top    = clockRect->bottom - 30;
        r.right  = cx + 108;
        r.bottom = r.top + 26;
    }
    return r;
}

static void on_paint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdcScreen = BeginPaint(hwnd, &ps);

    RECT cr;
    GetClientRect(hwnd, &cr);
    int cw = cr.right - cr.left;
    int ch = cr.bottom - cr.top;

    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP hBmp = CreateCompatibleBitmap(hdcScreen, cw, ch);
    HBITMAP hOldBmp = (HBITMAP)SelectObject(hdcMem, hBmp);

    /* Background */
    HBRUSH hBgBr = CreateSolidBrush(g_state.bgColor);
    FillRect(hdcMem, &cr, hBgBr);
    DeleteObject(hBgBr);

    RECT clockRect;
    calc_clock_rect(hwnd, &clockRect);

    /* Separator */
    HPEN hSep = CreatePen(PS_SOLID, 1,
        g_state.dark_mode ? RGB(0x50, 0x50, 0x50) : RGB(0xC0, 0xC0, 0xC0));
    SelectObject(hdcMem, hSep);
    MoveToEx(hdcMem, cr.left + SEP_MARGIN, clockRect.bottom, NULL);
    LineTo(hdcMem, cr.right - SEP_MARGIN, clockRect.bottom);
    DeleteObject(hSep);

    /* Clock */
    SYSTEMTIME st;
    GetLocalTime(&st);

    RECT clkInner = clockRect;
    clkInner.top    += 3;
    clkInner.bottom -= 26;

    if (g_state.clock_style == CLOCK_ANALOG) {
        clock_draw_analog(hdcMem, &clkInner, &st, &g_state);
    } else {
        clock_draw_digital(hdcMem, &clkInner, &st, &g_state);
    }

    /* Dismiss button */
    draw_dismiss_button(hdcMem, &clockRect);

    /* Alarm panel */
    draw_alarm_panel(hdcMem, hwnd, &clockRect);

    /* Blit finished frame to screen */
    BitBlt(hdcScreen, cr.left, cr.top, cw, ch, hdcMem, 0, 0, SRCCOPY);

    SelectObject(hdcMem, hOldBmp);
    DeleteObject(hBmp);
    DeleteDC(hdcMem);

    EndPaint(hwnd, &ps);
}

static void snooze_alarm(void) {
    AppState *s = &g_state;

    s->snooze_total_sec = s->snooze_minutes * 60;
    s->snooze_end_ms    = GetTickCount() + (DWORD)s->snooze_total_sec * 1000;
    s->snooze_pending   = TRUE;

    s->alarm_active = FALSE;
    sound_stop_alarm(s);

    InvalidateRect(s->hMainWnd, NULL, FALSE);
}

static void dismiss_alarm(void) {
    AppState *s = &g_state;
    s->alarm_active   = FALSE;
    s->snooze_pending  = FALSE;
    sound_stop_alarm(s);
    InvalidateRect(s->hMainWnd, NULL, FALSE);
}

static void on_lbuttondown(HWND hwnd, LPARAM lp) {
    int mx = GET_X_LPARAM(lp);
    int my = GET_Y_LPARAM(lp);

    RECT clockRect;
    calc_clock_rect(hwnd, &clockRect);

    /* Snooze / Dismiss / Cancel button hit test */
    if (g_state.alarm_active || g_state.snooze_pending) {
        RECT dr = get_dismiss_rect(&clockRect);

        if (g_state.snooze_pending) {
            if (mx >= dr.left && mx <= dr.right && my >= dr.top && my <= dr.bottom) {
                dismiss_alarm();
                return;
            }
        } else {
            int cx = clockRect.left + (clockRect.right - clockRect.left) / 2;
            RECT sr;
            sr.left   = cx - 108;
            sr.top    = clockRect.bottom - 30;
            sr.right  = cx - 6;
            sr.bottom = sr.top + 26;

            if (mx >= sr.left && mx <= sr.right && my >= sr.top && my <= sr.bottom) {
                snooze_alarm();
                return;
            }
            if (mx >= dr.left && mx <= dr.right && my >= dr.top && my <= dr.bottom) {
                dismiss_alarm();
                return;
            }
        }
    }

    /* Alarm panel hit test */
    RECT panel, header;
    calc_alarm_rects(hwnd, &panel, &header, &clockRect);

    for (int i = 0; i < g_state.alarm_count; i++) {
        RECT rowR = get_alarm_row_rect(&panel, &header, i);
        RECT chkR = get_check_rect(&rowR);
        RECT editR = get_edit_rect(&rowR);
        RECT clrR = get_clear_rect(&rowR);

        if (mx >= chkR.left && mx <= chkR.right && my >= chkR.top && my <= chkR.bottom) {
            if (g_state.alarms[i].hour != ALARM_UNSET) {
                g_state.alarms[i].enabled = !g_state.alarms[i].enabled;
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return;
        }

        if (mx >= editR.left && mx <= editR.right && my >= editR.top && my <= editR.bottom) {
            AlarmEditData data;
            data.hour        = g_state.alarms[i].hour;
            data.minute      = g_state.alarms[i].minute;
            data.enabled     = g_state.alarms[i].enabled;
            data.repeat_mode = g_state.alarms[i].repeat_mode;
            lstrcpyW(data.label, g_state.alarms[i].label);

            if (DialogBoxParamW(GetModuleHandle(NULL), MAKEINTRESOURCEW(IDD_ALARM),
                                hwnd, alarm_dlg_proc, (LPARAM)&data) == IDOK) {
                g_state.alarms[i].hour        = data.hour;
                g_state.alarms[i].minute      = data.minute;
                g_state.alarms[i].enabled     = data.enabled;
                g_state.alarms[i].repeat_mode = data.repeat_mode;
                lstrcpyW(g_state.alarms[i].label, data.label);
                settings_save(&g_state);
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return;
        }

        if (mx >= clrR.left && mx <= clrR.right && my >= clrR.top && my <= clrR.bottom) {
            g_state.alarms[i].hour        = ALARM_UNSET;
            g_state.alarms[i].minute      = ALARM_UNSET;
            g_state.alarms[i].enabled     = FALSE;
            g_state.alarms[i].repeat_mode = REPEAT_ONCE;
            g_state.alarms[i].label[0]    = 0;
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
            DWORD fontSize = SizeofResource(hInst, hFontRes);
            void *fontData = LockResource(hMem);
            if (fontData && fontSize > 0) {
                DWORD numFonts = 0;
                if (AddFontMemResourceEx(fontData, fontSize, NULL, &numFonts) && numFonts > 0) {
                    lstrcpyW(s->clockFaceName, L"Digital-7 Mono");
                }
            }
        }
    }

    s->hClockFont = create_fitted_clock_font(hwnd, s->clockFaceName);

    /* Measure clock font for date font sizing */
    HDC hdc = GetDC(hwnd);
    TEXTMETRICW tmClock;
    HFONT hOld = (HFONT)SelectObject(hdc, s->hClockFont);
    GetTextMetricsW(hdc, &tmClock);
    int dateH = tmClock.tmHeight / 6;
    if (dateH < 18) dateH = 18;
    SelectObject(hdc, hOld);
    ReleaseDC(hwnd, hdc);

    s->hDateFont = CreateFontW(
        dateH, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    /* Compute exact clock area height from both font metrics */
    hdc = GetDC(hwnd);
    SelectObject(hdc, s->hClockFont);
    GetTextMetricsW(hdc, &tmClock);
    TEXTMETRICW tmDate;
    SelectObject(hdc, s->hDateFont);
    GetTextMetricsW(hdc, &tmDate);
    SelectObject(hdc, hOld);
    ReleaseDC(hwnd, hdc);

    if (s->clock_style == CLOCK_ANALOG) {
        RECT cr;
        GetClientRect(hwnd, &cr);
        int cw = cr.right - cr.left;
        int box = cw - SEP_MARGIN * 2;
        s->clockAreaH = box + 10;
    } else {
        int gap = tmClock.tmHeight / 10;
        int textH = tmClock.tmHeight + gap + tmDate.tmHeight;
        s->clockAreaH = textH + 6 + 34;
    }

    s->hGuiFont = CreateFontW(
        16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    theme_apply(hwnd, s->dark_mode);
    tray_create(hwnd, s);
    SetTimer(hwnd, TIMER_CLOCK, 1000, NULL);

    return 0;
}

static void on_command(HWND hwnd, WPARAM wp) {
    AppState *s = &g_state;

    switch (LOWORD(wp)) {
    case IDM_SETTINGS:
        if (DialogBoxParamW(GetModuleHandle(NULL), MAKEINTRESOURCEW(IDD_SETTINGS),
                            hwnd, settings_dlg_proc, (LPARAM)s) == IDOK) {
            settings_save(s);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        break;

    case IDM_ABOUT:
        MessageBoxW(hwnd,
            L"AlarmClock v1.0\r\nA simple alarm clock for Windows.",
            L"About AlarmClock", MB_OK | MB_ICONINFORMATION);
        break;

    case IDM_EXIT:
        sound_stop_alarm(s);
        DestroyWindow(hwnd);
        break;

    case IDM_TRAY_SHOW:
        ShowWindow(hwnd, SW_SHOW);
        SetForegroundWindow(hwnd);
        break;

    case IDM_TRAY_EXIT:
        sound_stop_alarm(s);
        DestroyWindow(hwnd);
        break;
    }
}

static void on_timer(HWND hwnd) {
    AppState *s = &g_state;

    SYSTEMTIME st;
    GetLocalTime(&st);

    if (alarms_check(s, &st)) {
        sound_play_alarm(s);
        ShowWindow(hwnd, SW_SHOW);
        SetForegroundWindow(hwnd);
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

    InvalidateRect(hwnd, NULL, FALSE);
}

static void on_size(HWND hwnd, WPARAM wp) {
    AppState *s = &g_state;

    if (wp == SIZE_MINIMIZED) return;
    if (s->hMainWnd != hwnd) return;

    RECT cr;
    GetClientRect(hwnd, &cr);
    int availW = cr.right - cr.left - SEP_MARGIN * 2 - 8;
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
    SelectObject(hdc, hOld);
    ReleaseDC(hwnd, hdc);

    s->hDateFont = CreateFontW(
        dateH, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    /* Recompute clock area height from both font metrics */
    hdc = GetDC(hwnd);
    SelectObject(hdc, s->hClockFont);
    GetTextMetricsW(hdc, &tm);
    TEXTMETRICW tmDate;
    SelectObject(hdc, s->hDateFont);
    GetTextMetricsW(hdc, &tmDate);
    SelectObject(hdc, hOld);
    ReleaseDC(hwnd, hdc);

    if (s->clock_style == CLOCK_ANALOG) {
        int cw = cr.right - cr.left;
        int box = cw - SEP_MARGIN * 2;
        s->clockAreaH = box + 10;
    } else {
        int gap = tm.tmHeight / 10;
        int textH = tm.tmHeight + gap + tmDate.tmHeight;
        s->clockAreaH = textH + 6 + 34;
    }

    InvalidateRect(hwnd, NULL, FALSE);
}

static void on_destroy(HWND hwnd) {
    AppState *s = &g_state;

    KillTimer(hwnd, TIMER_CLOCK);
    sound_stop_alarm(s);

    WINDOWPLACEMENT wp;
    wp.length = sizeof(wp);
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
    if (s->hPanelBrush) DeleteObject(s->hPanelBrush);

    PostQuitMessage(0);
}

LRESULT CALLBACK main_wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        return on_create(hwnd);

    case WM_PAINT:
        on_paint(hwnd);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_TIMER:
        if (wp == TIMER_CLOCK) on_timer(hwnd);
        return 0;

    case WM_GETMINMAXINFO: {
        MINMAXINFO *mmi = (MINMAXINFO *)lp;
        mmi->ptMinTrackSize.x = 400;
        mmi->ptMinTrackSize.y = 340;
        return 0;
    }

    case WM_SIZE:
        on_size(hwnd, wp);
        return 0;

    case WM_COMMAND:
        on_command(hwnd, wp);
        return 0;

    case WM_LBUTTONDOWN:
        on_lbuttondown(hwnd, lp);
        return 0;

    case WM_CLOSE:
        ShowWindow(hwnd, SW_HIDE);
        return 0;

    case WM_DESTROY:
        on_destroy(hwnd);
        return 0;

    case WM_TRAYICON:
        if (LOWORD(lp) == WM_RBUTTONUP) {
            tray_show_menu(hwnd, &g_state);
        } else if (LOWORD(lp) == WM_LBUTTONDBLCLK) {
            ShowWindow(hwnd, SW_SHOW);
            SetForegroundWindow(hwnd);
        }
        return 0;

    case MM_MCINOTIFY:
        sound_on_mci_notify(&g_state);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
