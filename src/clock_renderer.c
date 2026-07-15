#include "clock_renderer.h"
#include "main.h"
#include <math.h>

/* Minimal GDI+ flat C API declarations (MinGW headers are C++-only) */

typedef float REAL;
typedef DWORD ARGB;

typedef struct GpGraphics  GpGraphics;
typedef struct GpPen       GpPen;
typedef struct GpBrush     GpBrush;
typedef struct GpSolidFill GpSolidFill;
typedef struct GpFont      GpFont;
typedef struct GpFontFamily GpFontFamily;
typedef struct GpStringFormat GpStringFormat;

enum GpStatus { Ok = 0 };
typedef enum GpStatus GpStatus;

enum { UnitPixel = 2, UnitPoint = 3 };
enum { SmoothingModeAntiAlias = 4 };
enum { TextRenderingHintAntiAlias = 4 };
enum { LineCapRound = 2 };
enum { FontStyleBold = 1 };
enum { StringAlignmentCenter = 1 };

typedef struct { REAL X, Y, Width, Height; } RectF;

typedef struct {
    UINT GdiplusVersion;
    void *DebugEventCallback;
    BOOL SuppressBackgroundThread;
    BOOL SuppressExternalCodecs;
} GdiplusStartupInput;

#define WINGDIPAPI __stdcall

GpStatus WINGDIPAPI GdipCreateFromHDC(HDC, GpGraphics**);
GpStatus WINGDIPAPI GdipDeleteGraphics(GpGraphics*);
GpStatus WINGDIPAPI GdipSetSmoothingMode(GpGraphics*, int);
GpStatus WINGDIPAPI GdipSetTextRenderingHint(GpGraphics*, int);
GpStatus WINGDIPAPI GdipCreatePen1(ARGB, REAL, int, GpPen**);
GpStatus WINGDIPAPI GdipDeletePen(GpPen*);
GpStatus WINGDIPAPI GdipSetPenStartCap(GpPen*, int);
GpStatus WINGDIPAPI GdipSetPenEndCap(GpPen*, int);
GpStatus WINGDIPAPI GdipCreateSolidFill(ARGB, GpSolidFill**);
GpStatus WINGDIPAPI GdipDeleteBrush(GpBrush*);
GpStatus WINGDIPAPI GdipDrawLine(GpGraphics*, GpPen*, REAL, REAL, REAL, REAL);
GpStatus WINGDIPAPI GdipDrawEllipse(GpGraphics*, GpPen*, REAL, REAL, REAL, REAL);
GpStatus WINGDIPAPI GdipFillEllipse(GpGraphics*, GpBrush*, REAL, REAL, REAL, REAL);
GpStatus WINGDIPAPI GdipCreateFontFamilyFromName(const WCHAR*, void*, GpFontFamily**);
GpStatus WINGDIPAPI GdipDeleteFontFamily(GpFontFamily*);
GpStatus WINGDIPAPI GdipCreateFont(GpFontFamily*, REAL, int, int, GpFont**);
GpStatus WINGDIPAPI GdipDeleteFont(GpFont*);
GpStatus WINGDIPAPI GdipCreateStringFormat(int, LANGID, GpStringFormat**);
GpStatus WINGDIPAPI GdipDeleteStringFormat(GpStringFormat*);
GpStatus WINGDIPAPI GdipSetStringFormatAlign(GpStringFormat*, int);
GpStatus WINGDIPAPI GdipSetStringFormatLineAlign(GpStringFormat*, int);
GpStatus WINGDIPAPI GdipDrawString(GpGraphics*, const WCHAR*, int, const GpFont*, const RectF*, const GpStringFormat*, const GpBrush*);
GpStatus WINGDIPAPI GdiplusStartup(ULONG_PTR*, const GdiplusStartupInput*, void*);
void     WINGDIPAPI GdiplusShutdown(ULONG_PTR);

#define M_PI 3.14159265358979323846

static ULONG_PTR g_gdipToken;

static ARGB colorref_to_argb(COLORREF cr) {
    return 0xFF000000 | ((ARGB)GetRValue(cr) << 16) | ((ARGB)GetGValue(cr) << 8) | (ARGB)GetBValue(cr);
}

void clock_init(void) {
    GdiplusStartupInput gpsi = { 1, NULL, FALSE, FALSE };
    GdiplusStartup(&g_gdipToken, &gpsi, NULL);
}

void clock_cleanup(void) {
    GdiplusShutdown(g_gdipToken);
}

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

    SelectObject(hdc, s->hClockFont);
    SIZE timeSize;
    GetTextExtentPoint32W(hdc, timeBuf, lstrlenW(timeBuf), &timeSize);

    int ampmW = 0, ampmH = 0;
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

void clock_draw_countdown(HDC hdc, const RECT *rc, int remaining_ms, COLORREF tc, const AppState *s) {
    int totalSec = remaining_ms / 1000;
    int hh = totalSec / 3600;
    int mm = (totalSec % 3600) / 60;
    int ss = totalSec % 60;

    TCHAR buf[16];
    if (hh > 0)
        wsprintf(buf, L"%02d:%02d:%02d", hh, mm, ss);
    else
        wsprintf(buf, L"%02d:%02d", mm, ss);

    SetBkMode(hdc, TRANSPARENT);

    HFONT hOldFont = (HFONT)SelectObject(hdc, s->hClockFont);
    SIZE timeSize;
    GetTextExtentPoint32W(hdc, buf, lstrlenW(buf), &timeSize);

    int cx = (rc->left + rc->right) / 2;
    int startY = rc->top + (rc->bottom - rc->top - timeSize.cy) / 2;

    SetTextColor(hdc, tc);
    ExtTextOutW(hdc, cx - timeSize.cx / 2, startY, 0, NULL, buf, lstrlenW(buf), NULL);

    SelectObject(hdc, hOldFont);
}

void clock_draw_stopwatch(HDC hdc, const RECT *rc, DWORD elapsed_ms, const AppState *s) {
    DWORD cs = (elapsed_ms / 10) % 100;
    DWORD sec = (elapsed_ms / 1000) % 60;
    DWORD min = (elapsed_ms / 60000) % 60;
    DWORD hr  = elapsed_ms / 3600000;

    TCHAR buf[24];
    if (hr > 0)
        wsprintf(buf, L"%02d:%02d:%02d.%02d", (int)hr, (int)min, (int)sec, (int)cs);
    else
        wsprintf(buf, L"%02d:%02d.%02d", (int)min, (int)sec, (int)cs);

    SetBkMode(hdc, TRANSPARENT);

    HFONT hOldFont = (HFONT)SelectObject(hdc, s->hClockFont);
    SIZE timeSize;
    GetTextExtentPoint32W(hdc, buf, lstrlenW(buf), &timeSize);

    int cx = (rc->left + rc->right) / 2;
    int startY = rc->top + (rc->bottom - rc->top - timeSize.cy) / 2;

    SetTextColor(hdc, s->clockColor);
    ExtTextOutW(hdc, cx - timeSize.cx / 2, startY, 0, NULL, buf, lstrlenW(buf), NULL);

    SelectObject(hdc, hOldFont);
}

void clock_draw_analog(HDC hdc, const RECT *rc, const SYSTEMTIME *psst, const AppState *s) {
    (void)psst;

    int w = rc->right - rc->left;
    int h = rc->bottom - rc->top;
    REAL cx = (REAL)rc->left + w / 2.0f;
    REAL cy = (REAL)rc->top  + h / 2.0f;
    REAL radius = (REAL)((w < h ? w : h) / 2) - 8.0f;
    if (radius < 30.0f) radius = 30.0f;

    FILETIME ft;
    GetSystemTimePreciseAsFileTime(&ft);
    ULARGE_INTEGER uli;
    uli.LowPart  = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    ULONGLONG fileTimeMs = uli.QuadPart / 10000;
    SYSTEMTIME lt;
    FileTimeToSystemTime(&ft, &lt);

    double msFrac = (double)(fileTimeMs % 1000) / 1000.0;
    double secFrac = lt.wSecond + msFrac;
    double minFrac = lt.wMinute + secFrac / 60.0;
    double hourFrac = (lt.wHour % 12) + minFrac / 60.0;

    double hourAngle   = hourFrac * (M_PI / 6.0) - M_PI / 2.0;
    double minuteAngle = minFrac  * (M_PI / 30.0) - M_PI / 2.0;
    double secondAngle = secFrac  * (M_PI / 30.0) - M_PI / 2.0;

    GpGraphics *gr = NULL;
    GdipCreateFromHDC(hdc, &gr);
    GdipSetSmoothingMode(gr, SmoothingModeAntiAlias);
    GdipSetTextRenderingHint(gr, TextRenderingHintAntiAlias);

    ARGB faceArgb  = 0xFFFAFAFF;
    ARGB rimArgb   = 0xFFB4B4B9;
    ARGB tickArgb  = colorref_to_argb(s->textColor);
    ARGB accentArgb = colorref_to_argb(s->accentColor);

    GpSolidFill *faceBrush = NULL;
    GdipCreateSolidFill(faceArgb, &faceBrush);
    GpPen *rimPen = NULL;
    GdipCreatePen1(rimArgb, 2.5f, UnitPixel, &rimPen);
    GdipFillEllipse(gr, (GpBrush*)faceBrush, cx - radius, cy - radius, radius * 2, radius * 2);
    GdipDrawEllipse(gr, rimPen, cx - radius, cy - radius, radius * 2, radius * 2);
    GdipDeletePen(rimPen);
    GdipDeleteBrush((GpBrush*)faceBrush);

    /* Tick marks */
    int i;
    for (i = 0; i < 60; i++) {
        double a = i * M_PI / 30.0 - M_PI / 2.0;
        double ca = cos(a), sa = sin(a);
        if (i % 5 == 0) {
            GpPen *tickPen = NULL;
            GdipCreatePen1(tickArgb, 3.5f, UnitPixel, &tickPen);
            GdipDrawLine(gr, tickPen,
                cx + (REAL)((radius - 4) * ca), cy + (REAL)((radius - 4) * sa),
                cx + (REAL)((radius - 18) * ca), cy + (REAL)((radius - 18) * sa));
            GdipDeletePen(tickPen);
        } else {
            GpPen *tickPen = NULL;
            GdipCreatePen1(tickArgb, 1.0f, UnitPixel, &tickPen);
            GdipDrawLine(gr, tickPen,
                cx + (REAL)((radius - 4) * ca), cy + (REAL)((radius - 4) * sa),
                cx + (REAL)((radius - 10) * ca), cy + (REAL)((radius - 10) * sa));
            GdipDeletePen(tickPen);
        }
    }

    /* Hour numbers */
    int numH = (int)radius / 8;
    if (numH < 12) numH = 12;

    GpFontFamily *numFamily = NULL;
    GdipCreateFontFamilyFromName(L"Segoe UI", NULL, &numFamily);

    GpFont *numFont = NULL;
    GdipCreateFont(numFamily, (REAL)numH, FontStyleBold, UnitPixel, &numFont);

    GpSolidFill *numBrush = NULL;
    ARGB numArgb = s->dark_mode ? 0xFFF0F0F0 : tickArgb;
    GdipCreateSolidFill(numArgb, &numBrush);

    GpStringFormat *fmt = NULL;
    GdipCreateStringFormat(0, LANG_NEUTRAL, &fmt);
    GdipSetStringFormatAlign(fmt, StringAlignmentCenter);
    GdipSetStringFormatLineAlign(fmt, StringAlignmentCenter);

    for (i = 1; i <= 12; i++) {
        double a = i * M_PI / 6.0 - M_PI / 2.0;
        WCHAR num[4];
        wsprintfW(num, L"%d", i);
        REAL rw = 42.0f;
        REAL rh = (REAL)(numH + 4);
        RectF numRect = {
            cx + (REAL)((radius - 30) * cos(a)) - rw / 2.0f,
            cy + (REAL)((radius - 30) * sin(a)) - rh / 2.0f,
            rw, rh
        };
        GdipDrawString(gr, num, -1, numFont, &numRect, fmt, (GpBrush*)numBrush);
    }

    GdipDeleteStringFormat(fmt);
    GdipDeleteBrush((GpBrush*)numBrush);
    GdipDeleteFont(numFont);
    GdipDeleteFontFamily(numFamily);

    /* Hands */
    REAL hLen = radius * 0.50f;
    REAL mLen = radius * 0.75f;
    REAL sLen = radius * 0.82f;

    GpPen *secPen = NULL;
    GdipCreatePen1(0xFFFF4040, 1.5f, UnitPixel, &secPen);
    GdipSetPenStartCap(secPen, LineCapRound);
    GdipSetPenEndCap(secPen, LineCapRound);
    GdipDrawLine(gr, secPen, cx, cy,
        cx + (REAL)(sLen * cos(secondAngle)), cy + (REAL)(sLen * sin(secondAngle)));
    GdipDeletePen(secPen);

    GpPen *minPen = NULL;
    GdipCreatePen1(0xFF5BA0D0, 3.0f, UnitPixel, &minPen);
    GdipSetPenStartCap(minPen, LineCapRound);
    GdipSetPenEndCap(minPen, LineCapRound);
    GdipDrawLine(gr, minPen, cx, cy,
        cx + (REAL)(mLen * cos(minuteAngle)), cy + (REAL)(mLen * sin(minuteAngle)));
    GdipDeletePen(minPen);

    GpPen *hourPen = NULL;
    GdipCreatePen1(accentArgb, 5.0f, UnitPixel, &hourPen);
    GdipSetPenStartCap(hourPen, LineCapRound);
    GdipSetPenEndCap(hourPen, LineCapRound);
    GdipDrawLine(gr, hourPen, cx, cy,
        cx + (REAL)(hLen * cos(hourAngle)), cy + (REAL)(hLen * sin(hourAngle)));
    GdipDeletePen(hourPen);

    /* Center dot */
    GpSolidFill *dotBrush = NULL;
    GdipCreateSolidFill(accentArgb, &dotBrush);
    GdipFillEllipse(gr, (GpBrush*)dotBrush, cx - 4.0f, cy - 4.0f, 8.0f, 8.0f);
    GdipDeleteBrush((GpBrush*)dotBrush);

    GdipDeleteGraphics(gr);
}
