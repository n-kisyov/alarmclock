#include "clock_renderer.h"
#include "main.h"
#include <math.h>

#define M_PI 3.14159265358979323846

void clock_draw_digital(HDC hdc, const RECT *rc, const SYSTEMTIME *st, const AppState *s) {

    TCHAR timeBuf[32];
    TCHAR dateBuf[64];
    wsprintf(timeBuf, L"%02d:%02d:%02d", st->wHour, st->wMinute, st->wSecond);

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

    SelectObject(hdc, s->hClockFont);
    SetTextColor(hdc, tc);
    RECT tr = *rc;
    tr.top = startY;
    DrawText(hdc, timeBuf, -1, &tr, DT_CENTER | DT_SINGLELINE | DT_TOP);

    SelectObject(hdc, s->hDateFont);
    SetTextColor(hdc, s->textColor);
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
