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
#include <strsafe.h>

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

/* The analog clock area is derived from window width, which on a wide or short
   window used to exceed the client height and push the mode bar and the whole
   alarm panel off-screen, out of reach. Keep enough room for a collapsed panel. */
static int clamp_clock_area(HWND hwnd, int desired) {
    RECT cr;
    GetClientRect(hwnd, &cr);
    int panelMin = 4 + ALARM_HEADER_H + 17 + ALARM_PAD_Y;
    int maxH = (cr.bottom - cr.top) - panelMin;
    if (maxH < 80) maxH = 80;
    if (desired > maxH) desired = maxH;
    if (desired < 80) desired = 80;
    return desired;
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

    int rowCount = g_state.alarms_collapsed ? 0 : g_state.alarm_count;
    int minH = ALARM_HEADER_H + 17;
    int maxH = ALARM_HEADER_H + 6 + rowCount * ALARM_ROW_H + 21;

    int panH = availH;
    if (panH < minH) panH = minH;
    if (panH > maxH) panH = maxH;
    panel->left = panX; panel->top = panY;
    panel->right = panX + panW; panel->bottom = panY + panH;
    header->left = panX + 12; header->top = panY + 6;
    header->right = panX + panW - 12; header->bottom = header->top + ALARM_HEADER_H;
}

/* How many rows actually fit inside the panel. On a short window the panel
   shrinks to its header, and rows drawn past its bottom edge spilled onto the
   background. */
static int alarm_visible_rows(const RECT *panel, const RECT *header) {
    int avail = panel->bottom - (header->bottom + 2);
    int n = (avail > 0) ? avail / ALARM_ROW_H : 0;
    if (n > g_state.alarm_count) n = g_state.alarm_count;
    return n;
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
/* Clear is furthest from the alarm it destroys, and Edit - the one you
   actually want most of the time - reads first. */
static RECT get_clear_rect(const RECT *alarmRow) {
    RECT r;
    r.right = alarmRow->right - 8; r.left = r.right - ALARM_BTN_W;
    r.top = alarmRow->top + (ALARM_ROW_H - ALARM_BTN_H) / 2;
    r.bottom = r.top + ALARM_BTN_H;
    return r;
}
static RECT get_edit_rect(const RECT *alarmRow) {
    RECT clear = get_clear_rect(alarmRow);
    RECT r;
    r.right = clear.left - ALARM_BTN_GAP; r.left = r.right - ALARM_BTN_W;
    r.top = clear.top; r.bottom = clear.bottom;
    return r;
}

/* Settings moved off the menu bar and into the panel header. */
static RECT get_settings_rect(const RECT *header) {
    RECT r;
    r.right  = header->right - 26;
    r.left   = r.right - 62;
    r.top    = header->top;
    r.bottom = header->top + ALARM_HEADER_H;
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

    /* Header with collapse arrow */
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, s->textColor);
    HFONT hOldFont = (HFONT)SelectObject(hdc, s->hGuiFont);

    RECT settingsR = get_settings_rect(&header);

    RECT hdr = header;
    hdr.right = settingsR.left - 8;
    DrawText(hdc, L"Alarms", -1, &hdr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, hOldFont);

    draw_button(hdc, &settingsR, L"Settings",
                s->dark_mode ? RGB(0x45,0x45,0x45) : RGB(0xE0,0xE0,0xE0), s->textColor);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, s->textColor);
    SelectObject(hdc, s->hGuiFont);

    WCHAR *arrow = s->alarms_collapsed ? L"\x25B6" : L"\x25BC";
    hdr = header;
    hdr.left = hdr.right - 22;
    DrawText(hdc, arrow, 1, &hdr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, hOldFont);

    if (s->alarms_collapsed) {
        SelectObject(hdc, hOldBr);
        SelectObject(hdc, hOldPn);
        return;
    }

    int visible = alarm_visible_rows(&panel, &header);
    for (int i = 0; i < visible; i++) {
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
            const WCHAR *ap = L"";
            if (!s->hour24) {
                ap = (h >= 12) ? L" PM" : L" AM";
                if (h == 0) h = 12; else if (h > 12) h -= 12;
            }
            /* The suffix carries its own leading space, so 24-hour rows no
               longer render with a double space and a trailing one. */
            if (s->alarms[i].label[0])
                StringCchPrintfW(timeStr, ARRAYSIZE(timeStr), L"%02d:%02d%s  %s",
                                 h, s->alarms[i].minute, ap, s->alarms[i].label);
            else
                StringCchPrintfW(timeStr, ARRAYSIZE(timeStr), L"%02d:%02d%s",
                                 h, s->alarms[i].minute, ap);
        } else {
            lstrcpy(timeStr, L"--:--");
        }

        RECT timeR;
        timeR.left = chkR.right + 8; timeR.top = rowR.top + 4;
        timeR.right = editR.left - 8; timeR.bottom = rowR.bottom - 4;

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

/* ---------- snooze / dismiss (forward) ---------- */
static void snooze_alarm(void);
static void dismiss_alarm(void);
static void show_and_focus(HWND hwnd);

/* ---------- mode action bar ----------

   The painter and the hit-tester used to recompute these rectangles from the
   same constants independently, and had already drifted apart: the Reset button
   shown while the countdown was stopped got drawn but never hit-tested, so it
   did nothing. Both now build one table and read positions out of it. */

typedef enum {
    MB_NONE = 0,
    MB_SNOOZE, MB_DISMISS,
    MB_TO_CLOCK, MB_TO_TIMER, MB_TO_STOPWATCH,
    MB_CD_START, MB_CD_PAUSE, MB_CD_SET, MB_CD_RESET,
    MB_SW_START, MB_SW_STOP, MB_SW_RESET
} ModeButtonId;

#define MODE_BAR_H        26
#define MODE_BAR_GAP      8
#define MODE_BAR_BOTTOM   4
#define MODE_BAR_MAX      5

typedef struct {
    ModeButtonId id;
    int          width;
    RECT         rect;
    const WCHAR *text;
    COLORREF     bg, fg;
    BOOL         highlight;
    BOOL         isLabel;   /* plain text, never hit-tested */
} ModeItem;

typedef struct {
    ModeItem items[MODE_BAR_MAX];
    int      count;
    WCHAR    labelBuf[32];
} ModeBar;

static ModeItem *mb_next(ModeBar *bar) {
    if (bar->count >= MODE_BAR_MAX) return NULL;
    ModeItem *it = &bar->items[bar->count++];
    ZeroMemory(it, sizeof(*it));
    return it;
}

static void mb_add(ModeBar *bar, ModeButtonId id, const WCHAR *text, int width,
                   COLORREF bg, COLORREF fg, BOOL highlight) {
    ModeItem *it = mb_next(bar);
    if (!it) return;
    it->id = id; it->text = text; it->width = width;
    it->bg = bg; it->fg = fg; it->highlight = highlight;
}

static void mb_add_label(ModeBar *bar, const WCHAR *text, int width, COLORREF fg) {
    ModeItem *it = mb_next(bar);
    if (!it) return;
    lstrcpynW(bar->labelBuf, text, 32);
    it->id = MB_NONE; it->text = bar->labelBuf; it->width = width;
    it->fg = fg; it->isLabel = TRUE;
}

/* Centres the row on the clock area. Because the width is summed from the items
   actually present, dropping a button - Start, when the timer is already at
   zero - no longer leaves the rest sitting off-centre. */
static void mb_layout(ModeBar *bar, const RECT *clockRect) {
    int total = 0;
    for (int i = 0; i < bar->count; i++) {
        total += bar->items[i].width;
        if (i) total += MODE_BAR_GAP;
    }
    int cx  = clockRect->left + (clockRect->right - clockRect->left) / 2;
    int top = clockRect->bottom - MODE_BAR_H - MODE_BAR_BOTTOM;
    int x   = cx - total / 2;

    for (int i = 0; i < bar->count; i++) {
        ModeItem *it = &bar->items[i];
        it->rect.left   = x;
        it->rect.right  = x + it->width;
        it->rect.top    = top;
        it->rect.bottom = top + MODE_BAR_H;
        x += it->width + MODE_BAR_GAP;
    }
}

static void build_mode_bar(const AppState *s, const RECT *clockRect, ModeBar *bar) {
    const COLORREF white   = RGB(255, 255, 255);
    const COLORREF neutral = s->dark_mode ? RGB(0x45,0x45,0x45) : RGB(0xE0,0xE0,0xE0);
    const COLORREF resetBg = RGB(0xC0, 0x50, 0x50);

    bar->count = 0;

    if (s->alarm_active) {
        mb_add(bar, MB_SNOOZE,  L"SNOOZE",  102, RGB(0xDE,0x87,0x00), white, FALSE);
        mb_add(bar, MB_DISMISS, L"DISMISS", 102, RGB(0xE8,0x11,0x23), white, FALSE);
    } else if (s->snooze_pending) {
        ULONGLONG now    = GetTickCount64();
        ULONGLONG remain = (s->snooze_end_ms > now) ? (s->snooze_end_ms - now) : 0;
        int rs = (int)(remain / 1000);
        WCHAR buf[32];
        wsprintfW(buf, L"Snoozed  %d:%02d", rs / 60, rs % 60);
        /* Beside the button, not on top of it - the old label rect started at
           the same y as CANCEL and overlapped it for the text's full height. */
        mb_add_label(bar, buf, 96, s->textColor);
        mb_add(bar, MB_DISMISS, L"CANCEL", 104, RGB(0xE8,0x11,0x23), white, FALSE);
    } else if (s->app_mode == APP_MODE_COUNTDOWN) {
        mb_add(bar, MB_TO_CLOCK, L"Clock", 56, neutral, s->textColor, FALSE);
        if (s->cd_running) {
            mb_add(bar, MB_CD_PAUSE, L"Pause", 62, s->accentColor, white, FALSE);
        } else {
            if (s->cd_remaining_ms > 0)
                mb_add(bar, MB_CD_START, L"Start", 62, RGB(0x00,0x88,0x00), white, FALSE);
            mb_add(bar, MB_CD_SET, L"Set", 62, s->accentColor, white, FALSE);
        }
        mb_add(bar, MB_CD_RESET, L"Reset", 62, resetBg, white, FALSE);
    } else if (s->app_mode == APP_MODE_STOPWATCH) {
        mb_add(bar, MB_TO_CLOCK, L"Clock", 56, neutral, s->textColor, FALSE);
        if (s->sw_running)
            mb_add(bar, MB_SW_STOP,  L"Stop",  62, RGB(0xCC,0x33,0x00), white, FALSE);
        else
            mb_add(bar, MB_SW_START, L"Start", 62, RGB(0x00,0x88,0x00), white, FALSE);
        mb_add(bar, MB_SW_RESET, L"Reset", 62, resetBg, white, FALSE);
    } else {
        mb_add(bar, MB_TO_CLOCK, L"Clock", 70, s->accentColor, white, TRUE);

        BOOL cdFinished = (s->cd_remaining_ms <= 0) &&
                          (s->cd_hours + s->cd_mins + s->cd_secs > 0);
        if (s->cd_running)
            mb_add(bar, MB_TO_TIMER, L"Timer", 68, RGB(0x20,0x80,0x20), white, TRUE);
        else if (cdFinished)
            mb_add(bar, MB_TO_TIMER, L"Finished!", 68, RGB(0xC0,0x30,0x30), white, FALSE);
        else
            mb_add(bar, MB_TO_TIMER, L"Timer", 68, neutral, s->textColor, FALSE);

        if (s->sw_running)
            mb_add(bar, MB_TO_STOPWATCH, L"Stopw.", 67, RGB(0x20,0x80,0x20), white, TRUE);
        else
            mb_add(bar, MB_TO_STOPWATCH, L"Stopw.", 67, neutral, s->textColor, FALSE);
    }

    mb_layout(bar, clockRect);
}

static void draw_mode_bar(HDC hdc, const RECT *clockRect) {
    ModeBar bar;
    build_mode_bar(&g_state, clockRect, &bar);

    for (int i = 0; i < bar.count; i++) {
        const ModeItem *it = &bar.items[i];
        if (it->isLabel) {
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, it->fg);
            HFONT hOld = (HFONT)SelectObject(hdc, g_state.hGuiFont);
            DrawTextW(hdc, it->text, -1, (RECT *)&it->rect,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(hdc, hOld);
        } else if (it->highlight) {
            draw_highlighted_button(hdc, &it->rect, it->text, it->bg, it->fg);
        } else {
            draw_button(hdc, &it->rect, it->text, it->bg, it->fg);
        }
    }
}

/* ---------- mode bar hit testing ---------- */

/* TRUE if the click landed on a button, so the caller stops looking. */
static BOOL on_mode_click(HWND hwnd, int mx, int my, const RECT *clockRect) {
    AppState *s = &g_state;
    ModeBar bar;
    build_mode_bar(s, clockRect, &bar);

    POINT pt = { mx, my };
    ModeButtonId hit = MB_NONE;
    for (int i = 0; i < bar.count; i++) {
        if (!bar.items[i].isLabel && PtInRect(&bar.items[i].rect, pt)) {
            hit = bar.items[i].id;
            break;
        }
    }
    if (hit == MB_NONE) return FALSE;

    switch (hit) {
    case MB_SNOOZE:  snooze_alarm();  return TRUE;
    case MB_DISMISS: dismiss_alarm(); return TRUE;

    case MB_TO_CLOCK:
        s->app_mode = APP_MODE_CLOCK;
        break;
    case MB_TO_TIMER:
        s->app_mode = APP_MODE_COUNTDOWN;
        if (s->cd_remaining_ms == 0)
            s->cd_remaining_ms = (s->cd_hours*3600 + s->cd_mins*60 + s->cd_secs)*1000;
        break;
    case MB_TO_STOPWATCH:
        s->app_mode = APP_MODE_STOPWATCH;
        break;

    case MB_CD_START:
        s->cd_running   = TRUE;
        s->cd_last_tick = GetTickCount64();
        break;
    case MB_CD_PAUSE:
        s->cd_running = FALSE;
        break;
    case MB_CD_SET:
        DialogBoxParamW(GetModuleHandle(NULL), MAKEINTRESOURCEW(IDD_COUNTDOWN_SET),
                        hwnd, cd_set_dlg_proc, (LPARAM)s);
        break;
    case MB_CD_RESET:
        s->cd_running      = FALSE;
        s->cd_remaining_ms = (s->cd_hours*3600 + s->cd_mins*60 + s->cd_secs)*1000;
        break;

    case MB_SW_START:
        s->sw_running    = TRUE;
        s->sw_start_tick = GetTickCount();
        break;
    case MB_SW_STOP:
        s->sw_accumulated_ms += GetTickCount() - s->sw_start_tick;
        s->sw_running = FALSE;
        break;
    case MB_SW_RESET:
        s->sw_running        = FALSE;
        s->sw_accumulated_ms = 0;
        break;

    case MB_NONE:
    default:
        return FALSE;
    }

    InvalidateRect(hwnd, NULL, FALSE);
    return TRUE;
}

/* ---------- snooze / dismiss ---------- */

static void snooze_alarm(void) {
    AppState *s = &g_state;
    s->snooze_total_sec = s->snooze_minutes * 60;
    s->snooze_end_ms = GetTickCount64() + (ULONGLONG)s->snooze_total_sec * 1000ULL;
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
    /* On the window, not in a static: WM_CTLCOLOR* is not guaranteed to arrive
       after WM_INITDIALOG, and these handlers dereference the pointer. */
    AppState *s = (AppState *)GetWindowLongPtrW(hDlg, GWLP_USERDATA);
    switch (msg) {
    case WM_INITDIALOG:
        s = (AppState *)lp;
        SetWindowLongPtrW(hDlg, GWLP_USERDATA, (LONG_PTR)s);
        theme_apply(hDlg, s->dark_mode);
        { TCHAR b[8]; wsprintf(b,L"%d",s->cd_hours);   SetDlgItemText(hDlg,IDC_CD_HOURS,b); }
        { TCHAR b[8]; wsprintf(b,L"%d",s->cd_mins);    SetDlgItemText(hDlg,IDC_CD_MINS,b); }
        { TCHAR b[8]; wsprintf(b,L"%d",s->cd_secs);    SetDlgItemText(hDlg,IDC_CD_SECS,b); }
        return TRUE;
    case WM_CTLCOLORSTATIC:
        if (!s) break;
        if (s->dark_mode) { SetTextColor((HDC)wp,s->textColor); SetBkColor((HDC)wp,s->bgColor); SetBkMode((HDC)wp,TRANSPARENT); }
        return (INT_PTR)s->hBgBrush;
    case WM_CTLCOLOREDIT:
        if (!s) break;
        if (s->dark_mode) { SetTextColor((HDC)wp,s->textColor); SetBkColor((HDC)wp,s->panelBgColor); }
        return (INT_PTR)s->hPanelBrush;
    case WM_CTLCOLORBTN:
        if (!s) break;
        return (INT_PTR)s->hBgBrush;
    case WM_CTLCOLORDLG:
        if (!s) break;
        return (INT_PTR)s->hBgBrush;
    case WM_COMMAND:
        if (!s) break;
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

    DWORD swElapsed = g_state.sw_accumulated_ms;
    if (g_state.sw_running)
        swElapsed += GetTickCount() - g_state.sw_start_tick;

    if (g_state.app_mode == APP_MODE_COUNTDOWN) {
        COLORREF tc = g_state.clockColor;
        if (g_state.cd_remaining_ms > 0 && g_state.cd_remaining_ms < 10000)
            tc = RGB(0xFF, 0x40, 0x40);
        else if (g_state.cd_remaining_ms <= 0 &&
                 (g_state.cd_hours + g_state.cd_mins + g_state.cd_secs > 0))
            tc = RGB(0xFF, 0x40, 0x40);
        clock_draw_countdown(hdcMem, &clkInner, g_state.cd_remaining_ms, tc, &g_state);
    } else if (g_state.app_mode == APP_MODE_STOPWATCH) {
        clock_draw_stopwatch(hdcMem, &clkInner, swElapsed, &g_state);
    } else if (g_state.clock_style == CLOCK_ANALOG) {
        clock_draw_analog(hdcMem, &clkInner, &st, &g_state);
    } else {
        clock_draw_digital(hdcMem, &clkInner, &st, &g_state);
    }

    draw_mode_bar(hdcMem, &clockRect);
    draw_alarm_panel(hdcMem, hwnd, &clockRect);

    /* Always an opaque blit. The acrylic setting used to AlphaBlend this frame
       at 220/255 over whatever was already in the window DC - and since
       WM_ERASEBKGND returns 1, that was the previous frame, not the DWM
       backdrop. It composited against stale pixels and left ghosting. The
       setting now drives only DWMWA_SYSTEMBACKDROP_TYPE, in theme_apply. */
    BitBlt(hdcScreen, cr.left, cr.top, cw, ch, hdcMem, 0, 0, SRCCOPY);

    SelectObject(hdcMem, hOldBmp);
    DeleteObject(hBmp); DeleteDC(hdcMem);
    EndPaint(hwnd, &ps);
}

/* ---------- message handlers ---------- */

static void on_lbuttondown(HWND hwnd, LPARAM lp) {
    int mx = GET_X_LPARAM(lp), my = GET_Y_LPARAM(lp);
    RECT clockRect; calc_clock_rect(hwnd, &clockRect);

    if (on_mode_click(hwnd, mx, my, &clockRect)) return;

    RECT panel, header;
    calc_alarm_rects(hwnd, &panel, &header, &clockRect);

    RECT settingsR = get_settings_rect(&header);
    if (PtInRect(&settingsR, (POINT){mx, my})) {
        SendMessageW(hwnd, WM_COMMAND, IDM_SETTINGS, 0);
        return;
    }

    /* Collapse arrow hit test */
    RECT arrowR = header;
    arrowR.left = arrowR.right - 24;
    if (PtInRect(&arrowR, (POINT){mx, my})) {
        g_state.alarms_collapsed = !g_state.alarms_collapsed;

        /* Resize window to fit */
        int panelH = ALARM_HEADER_H + 17;
        if (!g_state.alarms_collapsed)
            panelH = ALARM_HEADER_H + g_state.alarm_count * ALARM_ROW_H + 27;

        int clientH = g_state.clockAreaH + 4 + panelH;

        RECT wr, cr;
        GetWindowRect(hwnd, &wr);
        GetClientRect(hwnd, &cr);
        int chromeH = (wr.bottom - wr.top) - (cr.bottom - cr.top);

        if (!IsZoomed(hwnd)) {
            SetWindowPos(hwnd, NULL, 0, 0, wr.right - wr.left,
                         clientH + chromeH, SWP_NOMOVE | SWP_NOZORDER);
        }

        InvalidateRect(hwnd, NULL, FALSE);
        return;
    }

    int visibleRows = alarm_visible_rows(&panel, &header);
    for (int i = 0; i < visibleRows; i++) {
        RECT rowR = get_alarm_row_rect(&panel, &header, i);
        RECT chkR = get_check_rect(&rowR);
        RECT editR = get_edit_rect(&rowR);
        RECT clrR = get_clear_rect(&rowR);

        if (PtInRect(&chkR, (POINT){mx, my})) {
            if (g_state.alarms[i].hour != ALARM_UNSET) {
                g_state.alarms[i].enabled = !g_state.alarms[i].enabled;
                settings_save(&g_state);
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
            if (g_state.alarms[i].hour != ALARM_UNSET) {
                if (MessageBoxW(hwnd,
                        L"Clear this alarm?\n\nIts time, label and repeat days "
                        L"will be discarded.",
                        L"AlarmClock", MB_OKCANCEL | MB_ICONWARNING) != IDOK)
                    return;
            }
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
    hOld = (HFONT)SelectObject(hdc, s->hClockFont);
    GetTextMetricsW(hdc, &tmClock);
    TEXTMETRICW tmDate;
    SelectObject(hdc, s->hDateFont);
    GetTextMetricsW(hdc, &tmDate);
    SelectObject(hdc, hOld);
    ReleaseDC(hwnd, hdc);

    if (s->clock_style == CLOCK_ANALOG) {
        RECT cr; GetClientRect(hwnd, &cr);
        int box = (cr.right - cr.left) - SEP_MARGIN*2;
        s->clockAreaH = clamp_clock_area(hwnd, box + 10);
    } else {
        int gap = tmClock.tmHeight / 10;
        s->clockAreaH = clamp_clock_area(hwnd,
            tmClock.tmHeight + gap + tmDate.tmHeight + 6 + 34);
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
        MessageBoxW(hwnd, L"AlarmClock v1.1\n\nNikolay Kisyov, 2026", L"About AlarmClock", MB_OK|MB_ICONINFORMATION);
        break;
    case IDM_EXIT:
        sound_stop_alarm(s); DestroyWindow(hwnd); break;
    case IDM_TRAY_SHOW:
        show_and_focus(hwnd); break;
    case IDM_TRAY_EXIT:
        sound_stop_alarm(s); DestroyWindow(hwnd); break;
    }
}

/* SW_SHOW displays a window at its current state, which for a minimized window
   means it stays minimized - so an alarm could ring with its own window still
   in the taskbar. The fade is kept for the hidden-to-tray case, where it works. */
static void show_and_focus(HWND hwnd) {
    if (IsIconic(hwnd)) {
        ShowWindow(hwnd, SW_RESTORE);
    } else if (!IsWindowVisible(hwnd)) {
        AnimateWindow(hwnd, 200, AW_BLEND);
        ShowWindow(hwnd, SW_SHOW);
    } else {
        ShowWindow(hwnd, SW_SHOW);
    }
    SetForegroundWindow(hwnd);
}

/* Runs off the timer, not the paint, so the countdown keeps running - and still
   fires - while the window is hidden in the tray or minimized. */
static void tick_countdown(void) {
    AppState *s = &g_state;
    if (!s->cd_running) return;

    ULONGLONG now = GetTickCount64();
    s->cd_remaining_ms -= (int)(now - s->cd_last_tick);
    s->cd_last_tick = now;

    if (s->cd_remaining_ms <= 0) {
        s->cd_remaining_ms = 0;
        s->cd_running = FALSE;
        if (!s->alarm_active) {
            s->alarm_active = TRUE;
            sound_play_alarm(s);
            /* Bring the window up like a scheduled alarm does, so there is
               something to press Dismiss on. */
            s->app_mode = APP_MODE_COUNTDOWN;
            if (s->hMainWnd) show_and_focus(s->hMainWnd);
        }
    }
}

static void show_alarm_balloon(AppState *s, const SYSTEMTIME *st) {
    s->nid.uFlags |= NIF_INFO;
    s->nid.dwInfoFlags = NIIF_USER;
    lstrcpyW(s->nid.szInfoTitle, L"AlarmClock");
    s->nid.szInfo[0] = 0;
    if (s->alarms_enabled) {
        for (int i = 0; i < MAX_ALARMS; i++) {
            if (s->alarms[i].hour == (int)st->wHour && s->alarms[i].minute == (int)st->wMinute) {
                wsprintfW(s->nid.szInfo, L"%02d:%02d  %s",
                          s->alarms[i].hour, s->alarms[i].minute,
                          s->alarms[i].label[0] ? s->alarms[i].label : L"Alarm");
                break;
            }
        }
    }
    Shell_NotifyIconW(NIM_MODIFY, &s->nid);

    /* Leaving NIF_INFO set would make every later tooltip update re-raise
       this same balloon. */
    s->nid.uFlags &= ~NIF_INFO;
    s->nid.szInfo[0] = 0;
}

static void on_timer(HWND hwnd) {
    AppState *s = &g_state;
    SYSTEMTIME st;
    GetLocalTime(&st);
    static int lastAlarmSec = -1;

    tick_countdown();

    if ((int)st.wSecond != lastAlarmSec) {
        lastAlarmSec = (int)st.wSecond;

        if (alarms_check(s, &st)) {
            /* Sampled before showing the window, or the test below would
               always see a visible window and never notify. */
            BOOL wasInBackground = IsIconic(hwnd) || !IsWindowVisible(hwnd);

            sound_play_alarm(s);
            show_and_focus(hwnd);

            if (wasInBackground) show_alarm_balloon(s, &st);
        }

        if (s->snooze_pending && GetTickCount64() >= s->snooze_end_ms) {
            s->snooze_pending = FALSE;
            s->alarm_active = TRUE;
            s->last_fire_min = (int)st.wHour * 60 + (int)st.wMinute;
            sound_play_alarm(s);
            show_and_focus(hwnd);
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

    static int lastW = 0, lastH = 0;
    if (cr.right - cr.left == lastW && cr.bottom - cr.top == lastH) return;
    lastW = cr.right - cr.left;
    lastH = cr.bottom - cr.top;

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
    hOld = (HFONT)SelectObject(hdc, s->hClockFont);
    GetTextMetricsW(hdc, &tm);
    TEXTMETRICW tmDate;
    SelectObject(hdc, s->hDateFont); GetTextMetricsW(hdc, &tmDate);
    SelectObject(hdc, hOld); ReleaseDC(hwnd, hdc);

    if (s->clock_style == CLOCK_ANALOG) {
        int box = (cr.right - cr.left) - SEP_MARGIN*2;
        s->clockAreaH = clamp_clock_area(hwnd, box + 10);
    } else {
        int gap = tm.tmHeight / 10;
        s->clockAreaH = clamp_clock_area(hwnd,
            tm.tmHeight + gap + tmDate.tmHeight + 6 + 34);
    }

    InvalidateRect(hwnd, NULL, FALSE);
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

    clock_cleanup();
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
    case WM_SOUND_PREVIEW_DONE:
        if (g_state.sound_preview || g_state.hPreviewThread) {
            sound_stop_alarm(&g_state);
        }
        return 0;
    case WM_TRAYICON:
        if (LOWORD(lp) == WM_RBUTTONUP) tray_show_menu(hwnd, &g_state);
        else if (LOWORD(lp) == WM_LBUTTONDBLCLK) {
            show_and_focus(hwnd);
        }
        return 0;
    case MM_MCINOTIFY: sound_on_mci_notify(&g_state); return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
