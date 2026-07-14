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

    /* Center the combined block: time + gap + ampm */
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
    int r  = (w < h ? w : h) / 2 - 15;
    if (r < 20) r = 20;

    HPEN hPen = CreatePen(PS_SOLID, 3, s->accentColor);
    HPEN hOldPen = (HPEN)SelectObject(hdc, hPen);
    HBRUSH hOldBr = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));

    Ellipse(hdc, cx - r, cy - r, cx + r, cy + r);

    double hourAngle   = ((st->wHour % 12) + st->wMinute / 60.0) * (M_PI / 6.0) - M_PI / 2.0;
    double minuteAngle = (st->wMinute + st->wSecond / 60.0) * (M_PI / 30.0) - M_PI / 2.0;
    double secondAngle = st->wSecond * (M_PI / 30.0) - M_PI / 2.0;

    int hLen = (int)(r * 0.50);
    int mLen = (int)(r * 0.75);
    int sLen = (int)(r * 0.85);

    HPEN hThin = CreatePen(PS_SOLID, 1, s->textColor);
    SelectObject(hdc, hThin);
    MoveToEx(hdc, cx, cy, NULL);
    LineTo(hdc, cx + (int)(sLen * cos(secondAngle)), cy + (int)(sLen * sin(secondAngle)));
    DeleteObject(hThin);

    HPEN hMed = CreatePen(PS_SOLID, 2, s->textColor);
    SelectObject(hdc, hMed);
    MoveToEx(hdc, cx, cy, NULL);
    LineTo(hdc, cx + (int)(mLen * cos(minuteAngle)), cy + (int)(mLen * sin(minuteAngle)));
    DeleteObject(hMed);

    HPEN hThick = CreatePen(PS_SOLID, 4, s->accentColor);
    SelectObject(hdc, hThick);
    MoveToEx(hdc, cx, cy, NULL);
    LineTo(hdc, cx + (int)(hLen * cos(hourAngle)), cy + (int)(hLen * sin(hourAngle)));
    DeleteObject(hThick);

    SelectObject(hdc, hOldBr);
    SelectObject(hdc, hOldPen);
    DeleteObject(hPen);
}
