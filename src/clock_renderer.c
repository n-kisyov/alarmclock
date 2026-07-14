#include "clock_renderer.h"
#include "main.h"
#include <math.h>

#define M_PI 3.14159265358979323846

void clock_draw_digital(HDC hdc, const RECT *rc, const SYSTEMTIME *st, const AppState *s) {

    TCHAR timeBuf[16];
    TCHAR dateBuf[64];
    WCHAR ampm[4] = L"";

    int h = st->wHour;
    if (!s->hour24) {
        ampm[0] = (st->wHour >= 12) ? L'P' : L'A';
        ampm[1] = L'M';
        if (h == 0) h = 12;
        else if (h > 12) h -= 12;
    }
    wsprintf(timeBuf, L"%02d:%02d:%02d", h, st->wMinute, st->wSecond);

    const TCHAR *months[] = {
        L"January", L"February", L"March", L"April", L"May", L"June",
        L"July", L"August", L"September", L"October", L"November", L"December"
    };
    wsprintf(dateBuf, L"%s %d, %d", months[st->wMonth - 1], st->wDay, st->wYear);

    SetBkMode(hdc, TRANSPARENT);

    COLORREF tc = s->alarm_active ? RGB(0xFF, 0x40, 0x40) : s->clockColor;

    HFONT hOldFont = (HFONT)SelectObject(hdc, s->hClockFont);
    TEXTMETRICW tmClock;
    GetTextMetricsW(hdc, &tmClock);

    SelectObject(hdc, s->hDateFont);
    TEXTMETRICW tmDate;
    GetTextMetricsW(hdc, &tmDate);

    int gap    = tmClock.tmHeight / 10;
    int startY = rc->top + 2;
    int timeBaseline = startY + tmClock.tmAscent;

    /* Measure time digit width */
    SelectObject(hdc, s->hClockFont);
    SIZE timeSize;
    GetTextExtentPoint32W(hdc, timeBuf, lstrlenW(timeBuf), &timeSize);

    /* Measure AM/PM width in small font if needed */
    int ampmW = 0;
    int ampmH = 0;
    TEXTMETRICW tmAm;
    if (ampm[0]) {
        ampmH = tmClock.tmHeight / 5;
        if (ampmH < 12) ampmH = 12;
        HFONT hAmpmFont = CreateFontW(
            ampmH, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        HFONT hPrev = (HFONT)SelectObject(hdc, hAmpmFont);
        SIZE amSize;
        GetTextExtentPoint32W(hdc, ampm, 2, &amSize);
        ampmW = amSize.cx;
        GetTextMetricsW(hdc, &tmAm);
        SelectObject(hdc, hPrev);
        DeleteObject(hAmpmFont);
    }

    int totalW = timeSize.cx + (ampm[0] ? 6 + ampmW : 0);
    int cx = (rc->left + rc->right) / 2;
    int blockX = cx - totalW / 2;

    SetTextColor(hdc, tc);
    SelectObject(hdc, s->hClockFont);
    ExtTextOutW(hdc, blockX, startY, 0, NULL, timeBuf, lstrlenW(timeBuf), NULL);

    if (ampm[0]) {
        HFONT hAmpmFont = CreateFontW(
            ampmH, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        HFONT hOldAmpm = (HFONT)SelectObject(hdc, hAmpmFont);
        SetTextColor(hdc, tc);
        ExtTextOutW(hdc, blockX + timeSize.cx + 6, timeBaseline - tmAm.tmAscent,
                     0, NULL, ampm, lstrlenW(ampm), NULL);
        SelectObject(hdc, hOldAmpm);
        DeleteObject(hAmpmFont);
    }

    SelectObject(hdc, s->hDateFont);
    SetTextColor(hdc, s->textColor);
    RECT tr = *rc;
    tr.top = startY + tmClock.tmHeight + gap;
    DrawText(hdc, dateBuf, -1, &tr, DT_CENTER | DT_SINGLELINE | DT_TOP);

    SelectObject(hdc, hOldFont);
}

void clock_draw_analog(HDC hdc, const RECT *rc, const SYSTEMTIME *st, const AppState *s) {

    int w = rc->right - rc->left;
    int h = rc->bottom - rc->top;
    int cx = rc->left + w / 2;
    int cy = rc->top + h / 2;
    int r  = (w < h ? w : h) / 2 - 8;
    if (r < 30) r = 30;

    /* Smooth sub-second time using GetTickCount */
    DWORD tick = GetTickCount();
    double msFrac = (tick % 1000) / 1000.0;
    double secFrac = st->wSecond + msFrac;
    double minFrac = st->wMinute + secFrac / 60.0;
    double hourFrac = (st->wHour % 12) + minFrac / 60.0;

    double hourAngle   = hourFrac * (M_PI / 6.0)   - M_PI / 2.0;
    double minuteAngle = minFrac * (M_PI / 30.0)   - M_PI / 2.0;
    double secondAngle = secFrac * (M_PI / 30.0)   - M_PI / 2.0;

    /* Face outline */
    HPEN hPen = CreatePen(PS_SOLID, 3, s->accentColor);
    HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
    HBRUSH hOldBr = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Ellipse(hdc, cx - r, cy - r, cx + r, cy + r);

    /* Tick marks */
    for (int i = 0; i < 60; i++) {
        double a = i * M_PI / 30.0 - M_PI / 2.0;
        double ca = cos(a), sa = sin(a);

        if (i % 5 == 0) {
            HPEN hTick = CreatePen(PS_SOLID, 3, s->textColor);
            SelectObject(hdc, hTick);
            MoveToEx(hdc, cx + (int)((r - 4) * ca),  cy + (int)((r - 4) * sa), NULL);
            LineTo(hdc,   cx + (int)((r - 18) * ca), cy + (int)((r - 18) * sa));
            DeleteObject(hTick);
        } else {
            HPEN hTick = CreatePen(PS_SOLID, 1, s->textColor);
            SelectObject(hdc, hTick);
            MoveToEx(hdc, cx + (int)((r - 4) * ca), cy + (int)((r - 4) * sa), NULL);
            LineTo(hdc,   cx + (int)((r - 10) * ca), cy + (int)((r - 10) * sa));
            DeleteObject(hTick);
        }
    }

    /* Hour numbers */
    int numH = r / 8;
    if (numH < 12) numH = 12;
    HFONT hNumFont = CreateFontW(
        numH, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    HFONT hOldNum = (HFONT)SelectObject(hdc, hNumFont);
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, s->textColor);

    for (int i = 1; i <= 12; i++) {
        double a = i * M_PI / 6.0 - M_PI / 2.0;
        WCHAR num[4];
        wsprintfW(num, L"%d", i);
        SIZE sz;
        GetTextExtentPoint32W(hdc, num, lstrlenW(num), &sz);
        int nx = cx + (int)((r - 30) * cos(a)) - sz.cx / 2;
        int ny = cy + (int)((r - 30) * sin(a)) - sz.cy / 2;
        TextOutW(hdc, nx, ny, num, lstrlenW(num));
    }

    SelectObject(hdc, hOldNum);
    DeleteObject(hNumFont);

    /* Hands */
    int hLen = (int)(r * 0.50);
    int mLen = (int)(r * 0.75);
    int sLen = (int)(r * 0.82);

    HPEN hSec = CreatePen(PS_SOLID, 1, RGB(0xE0, 0x40, 0x40));
    SelectObject(hdc, hSec);
    MoveToEx(hdc, cx, cy, NULL);
    LineTo(hdc, cx + (int)(sLen * cos(secondAngle)), cy + (int)(sLen * sin(secondAngle)));
    DeleteObject(hSec);

    HPEN hMin = CreatePen(PS_SOLID, 2, s->textColor);
    SelectObject(hdc, hMin);
    MoveToEx(hdc, cx, cy, NULL);
    LineTo(hdc, cx + (int)(mLen * cos(minuteAngle)), cy + (int)(mLen * sin(minuteAngle)));
    DeleteObject(hMin);

    HPEN hHour = CreatePen(PS_SOLID, 4, s->accentColor);
    SelectObject(hdc, hHour);
    MoveToEx(hdc, cx, cy, NULL);
    LineTo(hdc, cx + (int)(hLen * cos(hourAngle)), cy + (int)(hLen * sin(hourAngle)));
    DeleteObject(hHour);

    /* Center dot */
    HPEN hDot = CreatePen(PS_SOLID, 5, s->accentColor);
    SelectObject(hdc, hDot);
    MoveToEx(hdc, cx, cy, NULL);
    LineTo(hdc, cx + 1, cy);
    DeleteObject(hDot);

    SelectObject(hdc, hOldBr);
    SelectObject(hdc, hOldPen);
    DeleteObject(hPen);
}
