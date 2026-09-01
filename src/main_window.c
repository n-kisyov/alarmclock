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
#include "power.h"
#include <strsafe.h>

/* Coming back after a week away should not set off an alarm for a Tuesday that
   is long past, so the catch-up walk only looks back this far. */
#define CATCHUP_MAX_MINUTES  (12 * 60)

/* Layout constants are authored at 96 dpi and scaled through S() at the point
   of use. The manifest claims PerMonitorV2, which tells Windows not to scale
   the app for us, so without this everything stayed physically small on a
   high-dpi display while the dialogs - which use dialog units - grew. */
#define ALARM_PAD_X      10
#define ALARM_PAD_Y      8
#define ALARM_ROW_H      30
#define ALARM_HEADER_H   22
#define ALARM_CHK_SIZE   18
#define ALARM_BTN_W      52
#define ALARM_BTN_H      22
#define ALARM_BTN_GAP    5
#define SEP_MARGIN       8

#define S(v)  MulDiv((v), g_state.dpi ? g_state.dpi : 96, 96)

static int window_dpi(HWND hwnd) {
    typedef UINT (WINAPI *PFN_GetDpiForWindow)(HWND);
    static PFN_GetDpiForWindow pGetDpiForWindow = NULL;
    static BOOL resolved = FALSE;

    if (!resolved) {
        resolved = TRUE;
        HMODULE user32 = GetModuleHandleW(L"user32.dll");
        if (user32)
            pGetDpiForWindow =
                (PFN_GetDpiForWindow)(void *)GetProcAddress(user32, "GetDpiForWindow");
    }
    if (pGetDpiForWindow) {
        UINT d = pGetDpiForWindow(hwnd);
        if (d) return (int)d;
    }
    HDC hdc = GetDC(hwnd);
    int d = hdc ? GetDeviceCaps(hdc, LOGPIXELSY) : 96;
    if (hdc) ReleaseDC(hwnd, hdc);
    return d ? d : 96;
}

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
    int panelMin = S(4 + ALARM_HEADER_H + 17 + ALARM_PAD_Y);
    int maxH = (cr.bottom - cr.top) - panelMin;
    int floorH = S(80);
    if (maxH < floorH) maxH = floorH;
    if (desired > maxH) desired = maxH;
    if (desired < floorH) desired = floorH;
    return desired;
}

static HFONT create_fitted_clock_font(HWND hwnd, const WCHAR *faceName) {
    RECT cr;
    GetClientRect(hwnd, &cr);
    int availW = cr.right - cr.left - S(SEP_MARGIN) * 2 - S(8);

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
    int panW = cw - S(ALARM_PAD_X) * 2;
    int panX = S(ALARM_PAD_X);
    int panY = sepY + S(4);
    int availH = cr.bottom - panY - S(ALARM_PAD_Y);

    int rowCount = g_state.alarms_collapsed ? 0 : g_state.alarm_count;
    int minH = S(ALARM_HEADER_H + 17);
    int maxH = S(ALARM_HEADER_H + 6) + rowCount * S(ALARM_ROW_H) + S(21);

    int panH = availH;
    if (panH < minH) panH = minH;
    if (panH > maxH) panH = maxH;
    panel->left = panX; panel->top = panY;
    panel->right = panX + panW; panel->bottom = panY + panH;
    header->left = panX + S(12); header->top = panY + S(6);
    header->right = panX + panW - S(12); header->bottom = header->top + S(ALARM_HEADER_H);
}

/* How many rows actually fit inside the panel. On a short window the panel
   shrinks to its header, and rows drawn past its bottom edge spilled onto the
   background. */
static int alarm_visible_rows(const RECT *panel, const RECT *header) {
    int avail = panel->bottom - (header->bottom + S(2));
    int n = (avail > 0) ? avail / S(ALARM_ROW_H) : 0;
    if (n > g_state.alarm_count) n = g_state.alarm_count;
    return n;
}

static RECT get_alarm_row_rect(const RECT *panel, const RECT *header, int idx) {
    RECT r;
    int baseY = header->bottom + S(2);
    r.left = panel->left + S(8); r.top = baseY + idx * S(ALARM_ROW_H);
    r.right = panel->right - S(8); r.bottom = r.top + S(ALARM_ROW_H);
    return r;
}
static RECT get_check_rect(const RECT *alarmRow) {
    RECT r;
    int cy = (alarmRow->top + alarmRow->bottom) / 2;
    r.left = alarmRow->left + S(ALARM_PAD_X); r.top = cy - S(ALARM_CHK_SIZE) / 2;
    r.right = r.left + S(ALARM_CHK_SIZE); r.bottom = r.top + S(ALARM_CHK_SIZE);
    return r;
}
/* Clear is furthest from the alarm it destroys, and Edit - the one you
   actually want most of the time - reads first. */
static RECT get_clear_rect(const RECT *alarmRow) {
    RECT r;
    r.right = alarmRow->right - S(8); r.left = r.right - S(ALARM_BTN_W);
    r.top = alarmRow->top + (S(ALARM_ROW_H) - S(ALARM_BTN_H)) / 2;
    r.bottom = r.top + S(ALARM_BTN_H);
    return r;
}
static RECT get_edit_rect(const RECT *alarmRow) {
    RECT clear = get_clear_rect(alarmRow);
    RECT r;
    r.right = clear.left - S(ALARM_BTN_GAP); r.left = r.right - S(ALARM_BTN_W);
    r.top = clear.top; r.bottom = clear.bottom;
    return r;
}

/* Settings moved off the menu bar and into the panel header. */
static RECT get_settings_rect(const RECT *header) {
    RECT r;
    r.right  = header->right - S(26);
    r.left   = r.right - S(62);
    r.top    = header->top;
    r.bottom = header->top + S(ALARM_HEADER_H);
    return r;
}

/* ---------- pointer state ----------

   Controls act on button-up, not button-down, and only if the cursor is still
   over the control that was pressed - so a stray press on DISMISS can be
   dragged off and cancelled. Hover and pressed are tracked so the drawn
   controls actually look like controls. */

typedef enum {
    HT_NONE = 0,
    HT_MODE,          /* index is a ModeButtonId */
    HT_SETTINGS,
    HT_COLLAPSE,
    HT_ALARM_CHECK,   /* index is the alarm slot */
    HT_ALARM_EDIT,
    HT_ALARM_CLEAR
} HitKind;

typedef struct {
    HitKind kind;
    int     index;
    RECT    rect;
} HitTarget;

typedef enum { BTN_NORMAL, BTN_HOVER, BTN_PRESSED } BtnState;

static HitTarget g_pressed;     /* control currently held down */
static BOOL      g_pressedIn;   /* ...and whether the cursor is still on it */
static HitTarget g_hover;
static BOOL      g_tracking;    /* WM_MOUSELEAVE requested */

static BtnState btn_state(HitKind kind, int index) {
    if (g_pressed.kind == kind && g_pressed.index == index)
        return g_pressedIn ? BTN_PRESSED : BTN_HOVER;
    if (g_pressed.kind == HT_NONE && g_hover.kind == kind && g_hover.index == index)
        return BTN_HOVER;
    return BTN_NORMAL;
}

static COLORREF shade(COLORREF c, int delta) {
    int r = GetRValue(c) + delta;
    int g = GetGValue(c) + delta;
    int b = GetBValue(c) + delta;
    if (r < 0) r = 0;
    if (r > 255) r = 255;
    if (g < 0) g = 0;
    if (g > 255) g = 255;
    if (b < 0) b = 0;
    if (b > 255) b = 255;
    return RGB(r, g, b);
}

/* ---------- drawing helpers ---------- */

static void draw_button(HDC hdc, const RECT *r, const TCHAR *text,
                         COLORREF bg, COLORREF fg, BtnState st) {
    if (st == BTN_HOVER)        bg = shade(bg,  22);
    else if (st == BTN_PRESSED) bg = shade(bg, -28);

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
                                     COLORREF bg, COLORREF fg, BtnState st) {
    draw_button(hdc, r, text, shade(bg, 40), fg, st);
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
    hdr.right = settingsR.left - S(8);
    DrawText(hdc, L"Alarms", -1, &hdr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, hOldFont);

    draw_button(hdc, &settingsR, L"Settings",
                s->dark_mode ? RGB(0x45,0x45,0x45) : RGB(0xE0,0xE0,0xE0), s->textColor,
                btn_state(HT_SETTINGS, 0));

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, s->textColor);
    SelectObject(hdc, s->hGuiFont);

    WCHAR *arrow = s->alarms_collapsed ? L"\x25B6" : L"\x25BC";
    hdr = header;
    hdr.left = hdr.right - S(22);
    DrawText(hdc, arrow, 1, &hdr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(hdc, hOldFont);

    if (s->alarms_collapsed) {
        SelectObject(hdc, hOldBr);
        SelectObject(hdc, hOldPn);
        return;
    }

    /* Hoisted out of the row loop: one pen, two brushes and one font now serve
       every row, instead of a create/delete pair per row on every frame. The
       tick font is dpi-scaled too, which the fixed 14px never was. */
    HPEN   hChkPen   = CreatePen(PS_SOLID, 2, s->textColor);
    HBRUSH hChkOn    = CreateSolidBrush(s->accentColor);
    HBRUSH hChkOff   = CreateSolidBrush(s->bgColor);
    HFONT  hTickFont = CreateFontW(S(14), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
                                   OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                   DEFAULT_PITCH, L"Segoe UI");

    int visible = alarm_visible_rows(&panel, &header);
    for (int i = 0; i < visible; i++) {
        RECT rowR  = get_alarm_row_rect(&panel, &header, i);
        RECT chkR  = get_check_rect(&rowR);
        RECT editR = get_edit_rect(&rowR);
        RECT clrR  = get_clear_rect(&rowR);

        BOOL armed = (s->alarms[i].enabled && s->alarms[i].hour != ALARM_UNSET);

        /* Re-selected every row: the draw_button calls at the foot of the loop
           restore whatever was selected before them. */
        SelectObject(hdc, armed ? hChkOn : hChkOff);
        SelectObject(hdc, hChkPen);
        Rectangle(hdc, chkR.left, chkR.top, chkR.right, chkR.bottom);

        if (armed) {
            SetTextColor(hdc, RGB(0xFF, 0xFF, 0xFF));
            SetBkMode(hdc, TRANSPARENT);
            HFONT hOld = (HFONT)SelectObject(hdc, hTickFont);
            DrawText(hdc, L"\x2713", 1, (RECT *)&chkR, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(hdc, hOld);
        }

        TCHAR timeStr[96];
        if (s->alarms[i].hour >= 0 && s->alarms[i].minute >= 0) {
            int h = s->alarms[i].hour;
            const WCHAR *ap = L"";
            if (!s->hour24) {
                ap = (h >= 12) ? L" PM" : L" AM";
                if (h == 0) h = 12; else if (h > 12) h -= 12;
            }
            /* The suffix carries its own leading space, so 24-hour rows no
               longer render with a double space and a trailing one. */
            /* Markers, so a row that departs from the global settings says so
               instead of looking identical to one that does not. */
            const WCHAR *note = s->alarms[i].sound[0]  ? L"  \x266A" : L"";
            const WCHAR *skip = s->alarms[i].skip_next ? L"  (skip next)" : L"";

            if (s->alarms[i].label[0])
                StringCchPrintfW(timeStr, ARRAYSIZE(timeStr), L"%02d:%02d%s  %s%s%s",
                                 h, s->alarms[i].minute, ap, s->alarms[i].label, note, skip);
            else
                StringCchPrintfW(timeStr, ARRAYSIZE(timeStr), L"%02d:%02d%s%s%s",
                                 h, s->alarms[i].minute, ap, note, skip);
        } else {
            lstrcpy(timeStr, L"--:--");
        }

        RECT timeR;
        timeR.left = chkR.right + S(8); timeR.top = rowR.top + S(4);
        timeR.right = editR.left - S(8); timeR.bottom = rowR.bottom - S(4);

        SetBkMode(hdc, TRANSPARENT); SetTextColor(hdc, s->textColor);
        HFONT hRowFont = (HFONT)SelectObject(hdc, s->hGuiFont);
        DrawText(hdc, timeStr, -1, &timeR, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        SelectObject(hdc, hRowFont);

        COLORREF btnBg = s->dark_mode ? RGB(0x45, 0x45, 0x45) : RGB(0xE0, 0xE0, 0xE0);
        draw_button(hdc, &editR, L"Edit",  btnBg, s->textColor, btn_state(HT_ALARM_EDIT, i));
        draw_button(hdc, &clrR,  L"Clear", btnBg, s->textColor, btn_state(HT_ALARM_CLEAR, i));
    }

    /* Deselect before deleting, which is the documented contract. */
    SelectObject(hdc, hOldBr); SelectObject(hdc, hOldPn);
    DeleteObject(hChkPen);
    DeleteObject(hChkOn);
    DeleteObject(hChkOff);
    DeleteObject(hTickFont);
}

/* ---------- mode action bar ---------- */

/* ---------- snooze / dismiss (forward) ---------- */
static void snooze_alarm(void);
static void dismiss_alarm(void);
static void show_and_focus(HWND hwnd);
static UINT timer_interval(const AppState *s);
static void show_alarm_balloon(AppState *s, int idx, const SYSTEMTIME *missedAt);

/* ---------- mode action bar ----------

   The painter and the hit-tester used to recompute these rectangles from the
   same constants independently, and had already drifted apart: the Reset button
   shown while the countdown was stopped got drawn but never hit-tested, so it
   did nothing. Both now build one table and read positions out of it. */

typedef enum {
    MB_NONE = 0,
    MB_SNOOZE, MB_DISMISS,
    MB_TO_CLOCK, MB_TO_TIMER, MB_TO_STOPWATCH, MB_SLEEP,
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

/* For a caption built on the fly: mb_add only keeps the pointer, so a caller's
   local buffer would be dangling by the time the bar is drawn. */
static void mb_add_copy(ModeBar *bar, ModeButtonId id, const WCHAR *text, int width,
                        COLORREF bg, COLORREF fg, BOOL highlight) {
    lstrcpynW(bar->labelBuf, text, ARRAYSIZE(bar->labelBuf));
    mb_add(bar, id, bar->labelBuf, width, bg, fg, highlight);
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
        bar->items[i].width = S(bar->items[i].width);
        total += bar->items[i].width;
        if (i) total += S(MODE_BAR_GAP);
    }
    int cx  = clockRect->left + (clockRect->right - clockRect->left) / 2;
    int top = clockRect->bottom - S(MODE_BAR_H) - S(MODE_BAR_BOTTOM);
    int x   = cx - total / 2;

    for (int i = 0; i < bar->count; i++) {
        ModeItem *it = &bar->items[i];
        it->rect.left   = x;
        it->rect.right  = x + it->width;
        it->rect.top    = top;
        it->rect.bottom = top + S(MODE_BAR_H);
        x += it->width + S(MODE_BAR_GAP);
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

        if (s->sleep_running) {
            ULONGLONG now    = GetTickCount64();
            ULONGLONG remain = (s->sleep_end_ms > now) ? (s->sleep_end_ms - now) : 0;
            int rs = (int)(remain / 1000);
            WCHAR buf[32];
            wsprintfW(buf, L"Sleep %d:%02d", rs / 60, rs % 60);
            mb_add_copy(bar, MB_SLEEP, buf, 92, RGB(0x5A,0x3E,0x8C), white, TRUE);
        } else {
            mb_add(bar, MB_SLEEP, L"Sleep", 60, neutral, s->textColor, FALSE);
        }
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
            draw_highlighted_button(hdc, &it->rect, it->text, it->bg, it->fg,
                                    btn_state(HT_MODE, (int)it->id));
        } else {
            draw_button(hdc, &it->rect, it->text, it->bg, it->fg,
                        btn_state(HT_MODE, (int)it->id));
        }
    }
}

/* ---------- mode bar actions ---------- */

static void mode_action(HWND hwnd, ModeButtonId hit) {
    AppState *s = &g_state;

    switch (hit) {
    case MB_SNOOZE:  snooze_alarm();  return;
    case MB_DISMISS: dismiss_alarm(); return;

    case MB_TO_CLOCK:
        s->app_mode = APP_MODE_CLOCK;
        break;
    case MB_TO_TIMER:
        s->app_mode = APP_MODE_COUNTDOWN;
        if (s->cd_remaining_ms == 0)
            s->cd_remaining_ms = cd_total_ms(s);
        break;
    case MB_TO_STOPWATCH:
        s->app_mode = APP_MODE_STOPWATCH;
        break;

    case MB_SLEEP:
        if (s->sleep_running) {
            sound_stop_sleep_timer(s);
        } else if (!sound_start_sleep_timer(s)) {
            MessageBoxW(hwnd,
                L"The sleep timer plays music, and the songs folder next to "
                L"the executable has nothing playable in it.\n\nAdd some "
                L"audio files there and try again.",
                L"AlarmClock", MB_OK | MB_ICONINFORMATION);
        }
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
        s->cd_remaining_ms = cd_total_ms(s);
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
        return;
    }

    InvalidateRect(hwnd, NULL, FALSE);
}

/* ---------- snooze / dismiss ---------- */

static void snooze_alarm(void) {
    AppState *s = &g_state;
    s->snooze_total_sec = alarm_snooze_for(s, s->ringing_alarm) * 60;
    s->snooze_end_ms = GetTickCount64() + (ULONGLONG)s->snooze_total_sec * 1000ULL;
    s->snooze_pending = TRUE;
    s->alarm_active = FALSE;
    power_keep_awake(FALSE);
    sound_stop_alarm(s);
    InvalidateRect(s->hMainWnd, NULL, FALSE);
}

static void dismiss_alarm(void) {
    AppState *s = &g_state;
    s->alarm_active = FALSE;
    s->snooze_pending = FALSE;
    s->auto_snooze_count = 0;
    s->ringing_alarm = -1;
    power_keep_awake(FALSE);
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
        /* Two digits is all any of these fields can mean; the edits were
           unbounded, and a long enough run of digits overflowed the total. */
        SendDlgItemMessageW(hDlg, IDC_CD_HOURS, EM_SETLIMITTEXT, 2, 0);
        SendDlgItemMessageW(hDlg, IDC_CD_MINS,  EM_SETLIMITTEXT, 2, 0);
        SendDlgItemMessageW(hDlg, IDC_CD_SECS,  EM_SETLIMITTEXT, 2, 0);
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
        /* Presets fill the fields rather than committing, so they can still be
           adjusted before OK. */
        case IDC_CD_PRESET_5:
        case IDC_CD_PRESET_10:
        case IDC_CD_PRESET_25: {
            int mins = (LOWORD(wp) == IDC_CD_PRESET_5)  ? 5 :
                       (LOWORD(wp) == IDC_CD_PRESET_10) ? 10 : 25;
            SetDlgItemText(hDlg, IDC_CD_HOURS, L"0");
            { TCHAR b[8]; wsprintf(b, L"%d", mins); SetDlgItemText(hDlg, IDC_CD_MINS, b); }
            SetDlgItemText(hDlg, IDC_CD_SECS, L"0");
            return TRUE;
        }
        case IDOK: {
            TCHAR b[16];
            GetDlgItemText(hDlg,IDC_CD_HOURS,b,16); s->cd_hours = _wtoi(b);
            GetDlgItemText(hDlg,IDC_CD_MINS,b,16);  s->cd_mins  = _wtoi(b);
            GetDlgItemText(hDlg,IDC_CD_SECS,b,16);  s->cd_secs  = _wtoi(b);
            cd_clamp(s);
            if (s->cd_hours==0 && s->cd_mins==0 && s->cd_secs==0) s->cd_mins=1;
            s->cd_remaining_ms = cd_total_ms(s);
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

/* The back buffer was allocated and thrown away on every paint, twenty times a
   second. It is kept between frames now and only rebuilt when the size
   changes. */
static HDC     g_memDC     = NULL;
static HBITMAP g_memBmp    = NULL;
static HBITMAP g_memOldBmp = NULL;
static int     g_memW = 0, g_memH = 0;

static void free_backbuffer(void) {
    if (!g_memDC) return;
    SelectObject(g_memDC, g_memOldBmp);
    DeleteObject(g_memBmp);
    DeleteDC(g_memDC);
    g_memDC = NULL; g_memBmp = NULL; g_memOldBmp = NULL;
    g_memW = g_memH = 0;
}

static BOOL ensure_backbuffer(HDC hdcScreen, int w, int h) {
    if (g_memDC && g_memW == w && g_memH == h) return TRUE;
    free_backbuffer();

    g_memDC = CreateCompatibleDC(hdcScreen);
    if (!g_memDC) return FALSE;
    g_memBmp = CreateCompatibleBitmap(hdcScreen, w, h);
    if (!g_memBmp) { DeleteDC(g_memDC); g_memDC = NULL; return FALSE; }

    g_memOldBmp = (HBITMAP)SelectObject(g_memDC, g_memBmp);
    g_memW = w; g_memH = h;
    return TRUE;
}

static void on_paint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdcScreen = BeginPaint(hwnd, &ps);
    RECT cr; GetClientRect(hwnd, &cr);
    int cw = cr.right - cr.left, ch = cr.bottom - cr.top;

    if (cw <= 0 || ch <= 0 || !ensure_backbuffer(hdcScreen, cw, ch)) {
        EndPaint(hwnd, &ps);
        return;
    }
    HDC hdcMem = g_memDC;

    HBRUSH hBgBr = CreateSolidBrush(g_state.bgColor);
    FillRect(hdcMem, &cr, hBgBr);
    DeleteObject(hBgBr);

    RECT clockRect;
    calc_clock_rect(hwnd, &clockRect);

    HPEN hSep = CreatePen(PS_SOLID, 1,
        g_state.dark_mode ? RGB(0x50,0x50,0x50) : RGB(0xC0,0xC0,0xC0));
    /* Deselected before the delete, per the documented contract. */
    HPEN hOldSep = (HPEN)SelectObject(hdcMem, hSep);
    MoveToEx(hdcMem, cr.left + S(SEP_MARGIN), clockRect.bottom, NULL);
    LineTo(hdcMem, cr.right - S(SEP_MARGIN), clockRect.bottom);
    SelectObject(hdcMem, hOldSep);
    DeleteObject(hSep);

    SYSTEMTIME st;
    GetLocalTime(&st);

    RECT clkInner = clockRect;
    clkInner.top += S(3); clkInner.bottom -= S(36);

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
    EndPaint(hwnd, &ps);
}

/* ---------- message handlers ---------- */

/* Pure geometry: works out what is under a point without changing anything, so
   the same routine can drive hover, press and release. */
static BOOL hit_test(HWND hwnd, int mx, int my, HitTarget *out) {
    POINT pt = { mx, my };
    out->kind = HT_NONE;
    out->index = 0;
    SetRectEmpty(&out->rect);

    RECT clockRect;
    calc_clock_rect(hwnd, &clockRect);

    ModeBar bar;
    build_mode_bar(&g_state, &clockRect, &bar);
    for (int i = 0; i < bar.count; i++) {
        if (!bar.items[i].isLabel && PtInRect(&bar.items[i].rect, pt)) {
            out->kind  = HT_MODE;
            out->index = (int)bar.items[i].id;
            out->rect  = bar.items[i].rect;
            return TRUE;
        }
    }

    RECT panel, header;
    calc_alarm_rects(hwnd, &panel, &header, &clockRect);

    RECT settingsR = get_settings_rect(&header);
    if (PtInRect(&settingsR, pt)) {
        out->kind = HT_SETTINGS;
        out->rect = settingsR;
        return TRUE;
    }

    RECT arrowR = header;
    arrowR.left = arrowR.right - S(24);
    if (PtInRect(&arrowR, pt)) {
        out->kind = HT_COLLAPSE;
        out->rect = arrowR;
        return TRUE;
    }

    /* Collapsed means no rows are drawn, so none can be clicked either - the
       old hit test went on matching rows that were not on screen. */
    if (g_state.alarms_collapsed) return FALSE;

    int rows = alarm_visible_rows(&panel, &header);
    for (int i = 0; i < rows; i++) {
        RECT rowR  = get_alarm_row_rect(&panel, &header, i);
        RECT chkR  = get_check_rect(&rowR);
        RECT editR = get_edit_rect(&rowR);
        RECT clrR  = get_clear_rect(&rowR);

        if (PtInRect(&chkR, pt))  { out->kind = HT_ALARM_CHECK; out->index = i; out->rect = chkR;  return TRUE; }
        if (PtInRect(&editR, pt)) { out->kind = HT_ALARM_EDIT;  out->index = i; out->rect = editR; return TRUE; }
        if (PtInRect(&clrR, pt))  { out->kind = HT_ALARM_CLEAR; out->index = i; out->rect = clrR;  return TRUE; }
    }
    return FALSE;
}

static void toggle_alarm_panel(HWND hwnd) {
    g_state.alarms_collapsed = !g_state.alarms_collapsed;

    int panelH = S(ALARM_HEADER_H + 17);
    if (!g_state.alarms_collapsed)
        panelH = S(ALARM_HEADER_H) + g_state.alarm_count * S(ALARM_ROW_H) + S(27);

    int clientH = g_state.clockAreaH + S(4) + panelH;

    RECT wr, cr;
    GetWindowRect(hwnd, &wr);
    GetClientRect(hwnd, &cr);
    int chromeH = (wr.bottom - wr.top) - (cr.bottom - cr.top);

    if (!IsZoomed(hwnd)) {
        SetWindowPos(hwnd, NULL, 0, 0, wr.right - wr.left,
                     clientH + chromeH, SWP_NOMOVE | SWP_NOZORDER);
    }
    InvalidateRect(hwnd, NULL, FALSE);
}

static void edit_alarm(HWND hwnd, int i) {
    AlarmEditData data;
    data.hour        = g_state.alarms[i].hour;
    data.minute      = g_state.alarms[i].minute;
    data.enabled     = g_state.alarms[i].enabled;
    data.repeat_days = g_state.alarms[i].repeat_days;
    lstrcpyW(data.label, g_state.alarms[i].label);
    lstrcpynW(data.sound, g_state.alarms[i].sound, MAX_PATH);
    data.volume         = g_state.alarms[i].volume;
    data.snooze_minutes = g_state.alarms[i].snooze_minutes;
    data.skip_next      = g_state.alarms[i].skip_next;

    if (DialogBoxParamW(GetModuleHandle(NULL), MAKEINTRESOURCEW(IDD_ALARM),
                        hwnd, alarm_dlg_proc, (LPARAM)&data) == IDOK) {
        g_state.alarms[i].hour        = data.hour;
        g_state.alarms[i].minute      = data.minute;
        g_state.alarms[i].enabled     = data.enabled;
        g_state.alarms[i].repeat_days = data.repeat_days;
        lstrcpyW(g_state.alarms[i].label, data.label);
        lstrcpynW(g_state.alarms[i].sound, data.sound, MAX_PATH);
        g_state.alarms[i].volume         = data.volume;
        g_state.alarms[i].snooze_minutes = data.snooze_minutes;
        g_state.alarms[i].skip_next      = data.skip_next;
        settings_save(&g_state);
        power_arm_wake_timer(&g_state);
        InvalidateRect(hwnd, NULL, FALSE);
    }
}

static void clear_alarm(HWND hwnd, int i) {
    if (g_state.alarms[i].hour != ALARM_UNSET) {
        if (MessageBoxW(hwnd,
                L"Clear this alarm?\n\nIts time, label and repeat days "
                L"will be discarded.",
                L"AlarmClock", MB_OKCANCEL | MB_ICONWARNING) != IDOK)
            return;
    }
    g_state.alarms[i].hour        = ALARM_UNSET;
    g_state.alarms[i].minute      = ALARM_UNSET;
    g_state.alarms[i].enabled     = FALSE;
    g_state.alarms[i].repeat_days = 0;
    g_state.alarms[i].label[0]    = 0;
    g_state.alarms[i].sound[0]    = 0;
    g_state.alarms[i].volume         = -1;
    g_state.alarms[i].snooze_minutes = -1;
    g_state.alarms[i].skip_next      = FALSE;
    settings_save(&g_state);
    InvalidateRect(hwnd, NULL, FALSE);
}

static void perform_hit(HWND hwnd, const HitTarget *t) {
    switch (t->kind) {
    case HT_MODE:
        mode_action(hwnd, (ModeButtonId)t->index);
        break;
    case HT_SETTINGS:
        SendMessageW(hwnd, WM_COMMAND, IDM_SETTINGS, 0);
        break;
    case HT_COLLAPSE:
        toggle_alarm_panel(hwnd);
        break;
    case HT_ALARM_CHECK:
        if (g_state.alarms[t->index].hour != ALARM_UNSET) {
            g_state.alarms[t->index].enabled = !g_state.alarms[t->index].enabled;
            settings_save(&g_state);
            InvalidateRect(hwnd, NULL, FALSE);
        }
        break;
    case HT_ALARM_EDIT:
        edit_alarm(hwnd, t->index);
        break;
    case HT_ALARM_CLEAR:
        clear_alarm(hwnd, t->index);
        break;
    default:
        break;
    }
}

static void on_lbuttondown(HWND hwnd, LPARAM lp) {
    HitTarget t;
    if (!hit_test(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), &t)) return;

    g_pressed   = t;
    g_pressedIn = TRUE;
    SetCapture(hwnd);
    InvalidateRect(hwnd, NULL, FALSE);
}

static void on_lbuttonup(HWND hwnd, LPARAM lp) {
    if (g_pressed.kind == HT_NONE) return;

    HitTarget pressed = g_pressed;
    BOOL inside = g_pressedIn;

    g_pressed.kind = HT_NONE;
    g_pressedIn    = FALSE;
    if (GetCapture() == hwnd) ReleaseCapture();
    InvalidateRect(hwnd, NULL, FALSE);

    if (!inside) return;

    /* Re-test: the layout can shift under the cursor while the button is held. */
    HitTarget now;
    if (hit_test(hwnd, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), &now) &&
        now.kind == pressed.kind && now.index == pressed.index) {
        perform_hit(hwnd, &pressed);
    }
}

static void on_mousemove(HWND hwnd, LPARAM lp) {
    int mx = GET_X_LPARAM(lp), my = GET_Y_LPARAM(lp);

    if (!g_tracking) {
        TRACKMOUSEEVENT tme;
        tme.cbSize      = sizeof(tme);
        tme.dwFlags     = TME_LEAVE;
        tme.hwndTrack   = hwnd;
        tme.dwHoverTime = 0;
        if (TrackMouseEvent(&tme)) g_tracking = TRUE;
    }

    if (g_pressed.kind != HT_NONE) {
        POINT pt = { mx, my };
        BOOL in = PtInRect(&g_pressed.rect, pt);
        if (in != g_pressedIn) {
            g_pressedIn = in;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return;
    }

    HitTarget t;
    hit_test(hwnd, mx, my, &t);
    if (t.kind != g_hover.kind || t.index != g_hover.index) {
        g_hover = t;
        InvalidateRect(hwnd, NULL, FALSE);
    }
}

static void on_mouseleave(HWND hwnd) {
    g_tracking = FALSE;
    if (g_hover.kind != HT_NONE) {
        g_hover.kind = HT_NONE;
        InvalidateRect(hwnd, NULL, FALSE);
    }
}

static void on_keydown(HWND hwnd, WPARAM key) {
    AppState *s = &g_state;
    switch (key) {
    case VK_ESCAPE:
        /* The one that has to work half asleep. */
        if (s->alarm_active || s->snooze_pending) dismiss_alarm();
        break;
    case VK_SPACE:
    case VK_RETURN:
        if (s->alarm_active) snooze_alarm();
        break;
    case 'S':
        if (!s->alarm_active) SendMessageW(hwnd, WM_COMMAND, IDM_SETTINGS, 0);
        break;
    default:
        break;
    }
}

/* Rebuilding the fonts and the clock area was duplicated between WM_CREATE and
   WM_SIZE, and neither rebuilt the GUI font - so a dpi change left the panel
   text at its old size. One routine now serves create, resize and
   WM_DPICHANGED. */
static void rebuild_fonts(HWND hwnd) {
    AppState *s = &g_state;

    if (s->hClockFont) { DeleteObject(s->hClockFont); s->hClockFont = NULL; }
    if (s->hDateFont)  { DeleteObject(s->hDateFont);  s->hDateFont  = NULL; }
    if (s->hGuiFont)   { DeleteObject(s->hGuiFont);   s->hGuiFont   = NULL; }

    s->hClockFont = create_fitted_clock_font(hwnd, s->clockFaceName);

    HDC hdc = GetDC(hwnd);
    TEXTMETRICW tmClock, tmDate;
    HFONT hOld = (HFONT)SelectObject(hdc, s->hClockFont);
    GetTextMetricsW(hdc, &tmClock);
    int dateH = tmClock.tmHeight / 6;
    if (dateH < S(18)) dateH = S(18);
    SelectObject(hdc, hOld);
    ReleaseDC(hwnd, hdc);

    s->hDateFont = CreateFontW(dateH,0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");

    hdc = GetDC(hwnd);
    hOld = (HFONT)SelectObject(hdc, s->hClockFont);
    GetTextMetricsW(hdc, &tmClock);
    SelectObject(hdc, s->hDateFont);
    GetTextMetricsW(hdc, &tmDate);
    SelectObject(hdc, hOld);
    ReleaseDC(hwnd, hdc);

    s->hGuiFont = CreateFontW(S(16),0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH|FF_DONTCARE,L"Segoe UI");

    RECT cr;
    GetClientRect(hwnd, &cr);
    if (s->clock_style == CLOCK_ANALOG) {
        int box = (cr.right - cr.left) - S(SEP_MARGIN) * 2;
        s->clockAreaH = clamp_clock_area(hwnd, box + S(10));
    } else {
        int gap = tmClock.tmHeight / 10;
        s->clockAreaH = clamp_clock_area(hwnd,
            tmClock.tmHeight + gap + tmDate.tmHeight + S(6 + 34));
    }
}

static LRESULT on_create(HWND hwnd) {
    AppState *s = &g_state;
    s->hMainWnd = hwnd;
    s->dpi = window_dpi(hwnd);
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

    rebuild_fonts(hwnd);

    theme_apply(hwnd, s->dark_mode);
    tray_create(hwnd, s);
    SetTimer(hwnd, TIMER_CLOCK, timer_interval(s), NULL);
    return 0;
}

static void on_command(HWND hwnd, WPARAM wp) {
    AppState *s = &g_state;
    switch (LOWORD(wp)) {
    case IDM_SETTINGS:
        if (DialogBoxParamW(GetModuleHandle(NULL), MAKEINTRESOURCEW(IDD_SETTINGS), hwnd, settings_dlg_proc, (LPARAM)s) == IDOK) {
            settings_save(s);
            power_arm_wake_timer(s);
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
/* SetForegroundWindow from a process that is not already in the foreground is
   normally refused, which for an alarm means the window never actually comes
   up. Attaching to the foreground thread's input queue lifts the restriction;
   if it still loses the race, flash the taskbar button rather than ring behind
   whatever the user is looking at. */
static void force_foreground(HWND hwnd) {
    HWND  fg    = GetForegroundWindow();
    DWORD fgTid = fg ? GetWindowThreadProcessId(fg, NULL) : 0;
    DWORD myTid = GetCurrentThreadId();

    if (fgTid && fgTid != myTid && AttachThreadInput(myTid, fgTid, TRUE)) {
        SetForegroundWindow(hwnd);
        BringWindowToTop(hwnd);
        AttachThreadInput(myTid, fgTid, FALSE);
    } else {
        SetForegroundWindow(hwnd);
    }

    if (GetForegroundWindow() != hwnd) {
        FLASHWINFO fi;
        ZeroMemory(&fi, sizeof(fi));
        fi.cbSize  = sizeof(fi);
        fi.hwnd    = hwnd;
        fi.dwFlags = FLASHW_ALL | FLASHW_TIMERNOFG;
        fi.uCount  = 5;
        FlashWindowEx(&fi);
    }
}

static void show_and_focus(HWND hwnd) {
    if (IsIconic(hwnd)) {
        ShowWindow(hwnd, SW_RESTORE);
    } else if (!IsWindowVisible(hwnd)) {
        AnimateWindow(hwnd, 200, AW_BLEND);
        ShowWindow(hwnd, SW_SHOW);
    } else {
        ShowWindow(hwnd, SW_SHOW);
    }
    force_foreground(hwnd);
}

/* An alarm nobody is home for used to ring until the machine slept. */
#define ALARM_MAX_RING_MS     (5 * 60 * 1000)
#define ALARM_MAX_AUTO_SNOOZE 3

/* A digital clock changes once a second and does not need a 20Hz repaint of the
   whole window; the analog sweep hand and the stopwatch centiseconds do. */
static UINT timer_interval(const AppState *s) {
    if (s->app_mode == APP_MODE_STOPWATCH) return 50;
    if (s->alarm_active)                   return 250;
    if (s->sleep_running)                  return 250;
    if (s->app_mode == APP_MODE_COUNTDOWN) return 200;
    if (s->snooze_pending)                 return 250;
    return (s->clock_style == CLOCK_ANALOG) ? 50 : 250;
}

static void update_timer_interval(HWND hwnd) {
    static UINT current = 0;
    UINT want = timer_interval(&g_state);
    if (want == current) return;
    current = want;
    SetTimer(hwnd, TIMER_CLOCK, want, NULL);
}

static void begin_alarm(AppState *s, BOOL fresh) {
    s->alarm_active     = TRUE;
    s->alarm_started_ms = GetTickCount64();
    if (fresh) s->auto_snooze_count = 0;
    /* Nothing is more useless than an alarm that lets the machine drop back to
       sleep halfway through ringing. */
    power_keep_awake(TRUE);
}

/* Rings whatever came due while the machine was asleep or the app was not
   running. Always raises the balloon, visible window or not: an alarm that has
   already happened has to say when, and the window alone does not carry that. */
static void check_missed_alarms(HWND hwnd) {
    AppState *s = &g_state;

    SYSTEMTIME now;
    GetLocalTime(&now);

    int idx = -1;
    SYSTEMTIME when;
    if (!alarms_catch_up(s, alarms_minute_stamp(&now), CATCHUP_MAX_MINUTES, &idx, &when))
        return;

    begin_alarm(s, TRUE);
    s->ringing_alarm = idx;
    sound_play_alarm(s);
    show_and_focus(hwnd);
    show_alarm_balloon(s, idx, &when);
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
            BOOL wasInBackground = s->hMainWnd &&
                (IsIconic(s->hMainWnd) || !IsWindowVisible(s->hMainWnd));

            begin_alarm(s, TRUE);
            s->ringing_alarm = -1;          /* the timer, not a slot */
            sound_play_alarm(s);
            /* Bring the window up like a scheduled alarm does, so there is
               something to press Dismiss on. */
            s->app_mode = APP_MODE_COUNTDOWN;
            if (s->hMainWnd) show_and_focus(s->hMainWnd);

            /* A timer finishing while hidden was silent in the tray. */
            if (wasInBackground) show_alarm_balloon(s, -1, NULL);
        }
    }
}

/* Takes the slot that actually fired. It used to re-scan for any alarm whose
   hour and minute matched the clock, ignoring enabled and repeat_days, so with
   two alarms set to the same time it could name the wrong one - or a disabled
   one - even though alarms_check already knew which had fired. */
static void show_alarm_balloon(AppState *s, int idx, const SYSTEMTIME *missedAt) {
    s->nid.uFlags |= NIF_INFO;
    s->nid.dwInfoFlags = NIIF_USER;
    lstrcpyW(s->nid.szInfoTitle, L"AlarmClock");
    s->nid.szInfo[0] = 0;
    if (idx >= 0 && idx < MAX_ALARMS) {
        const WCHAR *label = s->alarms[idx].label[0] ? s->alarms[idx].label : L"Alarm";
        if (missedAt)
            wsprintfW(s->nid.szInfo, L"Missed %02d:%02d  %s",
                      missedAt->wHour, missedAt->wMinute, label);
        else
            wsprintfW(s->nid.szInfo, L"%02d:%02d  %s",
                      s->alarms[idx].hour, s->alarms[idx].minute, label);
    } else {
        lstrcpyW(s->nid.szInfo, L"Timer finished");
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
    static int lastPaintedSec = -1;
    static int lastSeenMin = -1;
    static BOOL caughtUp = FALSE;

    /* The first tick is the earliest point where the window, the tray and the
       loaded settings all exist, which is what the catch-up needs. */
    if (!caughtUp) {
        caughtUp = TRUE;
        check_missed_alarms(hwnd);
        power_arm_wake_timer(s);
    }

    tick_countdown();

    if ((int)st.wSecond != lastAlarmSec) {
        lastAlarmSec = (int)st.wSecond;

        int fired = -1;
        if (alarms_check(s, &st, &fired)) {
            /* Sampled before showing the window, or the test below would
               always see a visible window and never notify. */
            BOOL wasInBackground = IsIconic(hwnd) || !IsWindowVisible(hwnd);

            begin_alarm(s, TRUE);
            s->ringing_alarm = fired;
            sound_play_alarm(s);
            show_and_focus(hwnd);

            if (wasInBackground) show_alarm_balloon(s, fired, NULL);
        }

        if (s->snooze_pending && GetTickCount64() >= s->snooze_end_ms) {
            s->snooze_pending = FALSE;
            begin_alarm(s, FALSE);
            s->last_fire_stamp = alarms_minute_stamp(&st);
            sound_play_alarm(s);
            show_and_focus(hwnd);
        }

        tray_update_tooltip(s);
    }

    if ((int)st.wMinute != lastSeenMin) {
        lastSeenMin = (int)st.wMinute;
        /* Written every minute but only persisted by whatever save happens
           next; if the process is killed in between, the gap simply looks a
           little wider, which is the safe direction to be wrong in. */
        s->last_seen_stamp = alarms_minute_stamp(&st);
        power_arm_wake_timer(s);
    }

    if (s->sleep_running && GetTickCount64() >= s->sleep_end_ms) {
        /* The fade has reached silence; stop the device rather than leaving it
           open rendering nothing. */
        sound_stop_sleep_timer(s);
        InvalidateRect(hwnd, NULL, FALSE);
    }

    /* Cap how long an alarm can ring unattended: snooze it a few times, then
       give up rather than sounding forever in an empty house. */
    if (s->alarm_active && s->alarm_started_ms &&
        GetTickCount64() - s->alarm_started_ms > ALARM_MAX_RING_MS) {
        if (s->auto_snooze_count < ALARM_MAX_AUTO_SNOOZE) {
            int keep = s->auto_snooze_count + 1;
            snooze_alarm();
            s->auto_snooze_count = keep;
        } else {
            dismiss_alarm();
        }
    }

    update_timer_interval(hwnd);

    /* Only repaint when something on screen has actually moved. A digital clock
       changes once a second; it used to redraw the whole window at 20Hz. */
    BOOL secondChanged = ((int)st.wSecond != lastPaintedSec);
    BOOL animating = s->alarm_active || s->snooze_pending ||
                     s->app_mode == APP_MODE_STOPWATCH ||
                     s->cd_running || s->sleep_running ||
                     (s->app_mode == APP_MODE_CLOCK && s->clock_style == CLOCK_ANALOG);

    if (secondChanged || animating) {
        lastPaintedSec = (int)st.wSecond;
        InvalidateRect(hwnd, NULL, FALSE);
    }
}

static void on_size(HWND hwnd, WPARAM wp) {
    AppState *s = &g_state;
    if (wp == SIZE_MINIMIZED) return;
    if (s->hMainWnd != hwnd) return;

    RECT cr; GetClientRect(hwnd, &cr);
    int availW = cr.right - cr.left - S(SEP_MARGIN)*2 - S(8);
    if (availW < S(40)) return;

    static int lastW = 0, lastH = 0;
    if (cr.right - cr.left == lastW && cr.bottom - cr.top == lastH) return;
    lastW = cr.right - cr.left;
    lastH = cr.bottom - cr.top;

    rebuild_fonts(hwnd);
    InvalidateRect(hwnd, NULL, FALSE);
}

/* Shared by the normal exit path and the shutdown path, which never sees a
   WM_DESTROY at all. */
static void capture_placement(HWND hwnd, AppState *s) {
    WINDOWPLACEMENT wp; wp.length = sizeof(wp);
    if (GetWindowPlacement(hwnd, &wp)) {
        s->winX = wp.rcNormalPosition.left;
        s->winY = wp.rcNormalPosition.top;
        s->winW = wp.rcNormalPosition.right - wp.rcNormalPosition.left;
        s->winH = wp.rcNormalPosition.bottom - wp.rcNormalPosition.top;
    }
}

static void on_destroy(HWND hwnd) {
    AppState *s = &g_state;
    KillTimer(hwnd, TIMER_CLOCK);
    sound_stop_alarm(s);
    sound_cleanup(s);
    power_cleanup();

    /* One last stamp, so a restart knows exactly how long it was away. */
    SYSTEMTIME nowExit;
    GetLocalTime(&nowExit);
    s->last_seen_stamp = alarms_minute_stamp(&nowExit);

    capture_placement(hwnd, s);
    settings_save(s);
    tray_remove(s);

    if (s->hClockFont) DeleteObject(s->hClockFont);
    if (s->hDateFont)  DeleteObject(s->hDateFont);
    if (s->hGuiFont)   DeleteObject(s->hGuiFont);
    if (s->hBgBrush)   DeleteObject(s->hBgBrush);
    if (s->hPanelBrush)DeleteObject(s->hPanelBrush);
    free_backbuffer();

    clock_cleanup();
    PostQuitMessage(0);
}

LRESULT CALLBACK main_wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    /* Explorer restarting takes every tray icon with it. Without re-adding ours
       the app becomes unreachable: WM_CLOSE only hides the window, so the tray
       menu is the only remaining route to Settings or Exit. */
    UINT taskbarCreated = tray_taskbar_created_msg();
    if (taskbarCreated && msg == taskbarCreated) {
        g_state.tray_added = FALSE;
        tray_create(hwnd, &g_state);
        tray_update_tooltip(&g_state);
        return 0;
    }

    switch (msg) {
    case WM_CREATE: return on_create(hwnd);
    case WM_PAINT: on_paint(hwnd); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_TIMER:
        if (wp == TIMER_CLOCK) {
            on_timer(hwnd);
        } else if (wp == TIMER_SOUND_PREVIEW) {
            KillTimer(hwnd, TIMER_SOUND_PREVIEW);
            sound_stop_alarm(&g_state);
        }
        return 0;
    case WM_GETMINMAXINFO: {
        MINMAXINFO *mmi = (MINMAXINFO *)lp;
        mmi->ptMinTrackSize.x = S(400);
        mmi->ptMinTrackSize.y = S(340);
        return 0;
    }
    case WM_DPICHANGED: {
        /* Moving between monitors of different scaling. Take the size Windows
           suggests, then rebuild every font against the new dpi. */
        g_state.dpi = (int)HIWORD(wp);
        RECT *sug = (RECT *)lp;
        SetWindowPos(hwnd, NULL, sug->left, sug->top,
                     sug->right - sug->left, sug->bottom - sug->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        rebuild_fonts(hwnd);
        InvalidateRect(hwnd, NULL, TRUE);
        return 0;
    }
    case WM_SIZE: on_size(hwnd, wp); return 0;
    case WM_ENDSESSION:
        /* Shutdown and logoff kill the process without a WM_DESTROY, so the save
           in on_destroy never ran: window geometry, mode and countdown were lost
           every time the machine was shut down. */
        if (wp) {
            SYSTEMTIME nowEnd;
            GetLocalTime(&nowEnd);
            g_state.last_seen_stamp = alarms_minute_stamp(&nowEnd);
            capture_placement(hwnd, &g_state);
            settings_save(&g_state);
        }
        return 0;
    case WM_POWERBROADCAST:
        if (wp == PBT_APMRESUMEAUTOMATIC || wp == PBT_APMRESUMESUSPEND) {
            /* Back from sleep: the schedule may well have gone by while the
               machine was off, and the wake timer needs arming for the next. */
            check_missed_alarms(hwnd);
            power_arm_wake_timer(&g_state);
        }
        return TRUE;
    case WM_TIMECHANGE:
        /* A clock jump - manual change, timezone, DST - leaves the stamp
           describing a moment that no longer relates to now, which can suppress
           the next legitimate fire. */
        g_state.last_fire_stamp = 0;
        power_arm_wake_timer(&g_state);
        tray_update_tooltip(&g_state);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_COMMAND: on_command(hwnd, wp); return 0;
    case WM_LBUTTONDOWN: on_lbuttondown(hwnd, lp); return 0;
    case WM_LBUTTONUP:   on_lbuttonup(hwnd, lp);   return 0;
    case WM_MOUSEMOVE:   on_mousemove(hwnd, lp);   return 0;
    case WM_MOUSELEAVE:  on_mouseleave(hwnd);      return 0;
    case WM_CAPTURECHANGED:
        if (g_pressed.kind != HT_NONE) {
            g_pressed.kind = HT_NONE;
            g_pressedIn = FALSE;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    case WM_KEYDOWN: on_keydown(hwnd, wp); return 0;
    case WM_SETCURSOR:
        /* What is interactive should look interactive. */
        if (LOWORD(lp) == HTCLIENT && g_hover.kind != HT_NONE) {
            SetCursor(LoadCursorW(NULL, IDC_HAND));
            return TRUE;
        }
        break;
    case WM_CLOSE:
        AnimateWindow(hwnd, 200, AW_HIDE | AW_BLEND);
        ShowWindow(hwnd, SW_HIDE); return 0;
    case WM_DESTROY: on_destroy(hwnd); return 0;
    case WM_TRAYICON:
        if (LOWORD(lp) == WM_RBUTTONUP) tray_show_menu(hwnd, &g_state);
        else if (LOWORD(lp) == WM_LBUTTONDBLCLK) {
            show_and_focus(hwnd);
        }
        return 0;
    case WM_AUDIO_TRACK_DONE: sound_on_track_done(&g_state); return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
