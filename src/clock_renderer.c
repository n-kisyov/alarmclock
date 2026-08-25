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
enum { StringFormatFlagsNoWrap = 0x00001000 };

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

/* The numeral font, its family and the string format do not depend on the
   theme, only on size - and building them cost a font lookup per frame at 20
   frames a second. Cached and rebuilt only when the dial changes size. */
static GpFontFamily   *g_numFamily = NULL;
static GpFont         *g_numFont   = NULL;
static GpFont         *g_apFont    = NULL;
static GpStringFormat *g_numFmt    = NULL;
static int             g_numFontH  = 0;

static void release_num_font_cache(void) {
    if (g_numFmt)    { GdipDeleteStringFormat(g_numFmt);   g_numFmt = NULL; }
    if (g_apFont)    { GdipDeleteFont(g_apFont);           g_apFont = NULL; }
    if (g_numFont)   { GdipDeleteFont(g_numFont);          g_numFont = NULL; }
    if (g_numFamily) { GdipDeleteFontFamily(g_numFamily);  g_numFamily = NULL; }
    g_numFontH = 0;
}

static BOOL ensure_num_font_cache(int numH) {
    if (g_numFont && g_numFontH == numH) return TRUE;
    release_num_font_cache();

    if (GdipCreateFontFamilyFromName(L"Segoe UI", NULL, &g_numFamily) != Ok || !g_numFamily)
        return FALSE;
    GdipCreateFont(g_numFamily, (REAL)numH, FontStyleBold, UnitPixel, &g_numFont);
    GdipCreateFont(g_numFamily, (REAL)numH * 0.62f, 0, UnitPixel, &g_apFont);
    GdipCreateStringFormat(StringFormatFlagsNoWrap, LANG_NEUTRAL, &g_numFmt);
    if (!g_numFont || !g_numFmt) { release_num_font_cache(); return FALSE; }

    GdipSetStringFormatAlign(g_numFmt, StringAlignmentCenter);
    GdipSetStringFormatLineAlign(g_numFmt, StringAlignmentCenter);
    g_numFontH = numH;
    return TRUE;
}

static ARGB colorref_to_argb(COLORREF cr) {
    return 0xFF000000 | ((ARGB)GetRValue(cr) << 16) | ((ARGB)GetGValue(cr) << 8) | (ARGB)GetBValue(cr);
}

void clock_init(void) {
    GdiplusStartupInput gpsi = { 1, NULL, FALSE, FALSE };
    GdiplusStartup(&g_gdipToken, &gpsi, NULL);
}

void clock_cleanup(void) {
    release_num_font_cache();
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

    /* The user's own long-date format, rather than a hardcoded English table. */
    if (GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_LONGDATE, st, NULL,
                        dateBuf, ARRAYSIZE(dateBuf), NULL) == 0) {
        wsprintf(dateBuf, L"%04d-%02d-%02d", st->wYear, st->wMonth, st->wDay);
    }

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

/* The clock font is fitted to "00:00:00", but the stopwatch draws three more
   characters once it passes an hour and ran off both edges of the window. Falls
   back to a proportionally smaller font just for that string. Returns NULL when
   the normal font already fits; the caller deletes any non-NULL result. */
static HFONT fit_text_font(HDC hdc, const AppState *s, const WCHAR *text,
                           int maxW, SIZE *sz) {
    HFONT prev = (HFONT)SelectObject(hdc, s->hClockFont);
    GetTextExtentPoint32W(hdc, text, lstrlenW(text), sz);
    if (sz->cx <= maxW || sz->cx <= 0 || maxW <= 0) {
        SelectObject(hdc, prev);
        return NULL;
    }

    LOGFONTW lf;
    if (GetObjectW(s->hClockFont, sizeof(lf), &lf) == 0) {
        SelectObject(hdc, prev);
        return NULL;
    }

    int h = lf.lfHeight < 0 ? -lf.lfHeight : lf.lfHeight;
    int scaled = (int)((LONGLONG)h * maxW / sz->cx);
    if (scaled < 10) scaled = 10;
    lf.lfHeight = (lf.lfHeight < 0) ? -scaled : scaled;

    HFONT fitted = CreateFontIndirectW(&lf);
    if (!fitted) {
        SelectObject(hdc, prev);
        return NULL;
    }
    SelectObject(hdc, fitted);
    GetTextExtentPoint32W(hdc, text, lstrlenW(text), sz);
    SelectObject(hdc, prev);
    return fitted;
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

    SIZE timeSize;
    HFONT fitted = fit_text_font(hdc, s, buf, rc->right - rc->left, &timeSize);
    HFONT hOldFont = (HFONT)SelectObject(hdc, fitted ? fitted : s->hClockFont);

    int cx = (rc->left + rc->right) / 2;
    int startY = rc->top + (rc->bottom - rc->top - timeSize.cy) / 2;

    SetTextColor(hdc, tc);
    ExtTextOutW(hdc, cx - timeSize.cx / 2, startY, 0, NULL, buf, lstrlenW(buf), NULL);

    SelectObject(hdc, hOldFont);
    if (fitted) DeleteObject(fitted);
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

    SIZE timeSize;
    HFONT fitted = fit_text_font(hdc, s, buf, rc->right - rc->left, &timeSize);
    HFONT hOldFont = (HFONT)SelectObject(hdc, fitted ? fitted : s->hClockFont);

    int cx = (rc->left + rc->right) / 2;
    int startY = rc->top + (rc->bottom - rc->top - timeSize.cy) / 2;

    SetTextColor(hdc, s->clockColor);
    ExtTextOutW(hdc, cx - timeSize.cx / 2, startY, 0, NULL, buf, lstrlenW(buf), NULL);

    SelectObject(hdc, hOldFont);
    if (fitted) DeleteObject(fitted);
}

void clock_draw_analog(HDC hdc, const RECT *rc, const SYSTEMTIME *psst, const AppState *s) {
    /* psst is the caller's whole-second local time. The hands need sub-second
       precision for the sweep, so this reads its own timestamp instead - but it
       must convert to local time first, which is what GetSystemTime* does not
       do on its own. */
    (void)psst;

    int w = rc->right - rc->left;
    int h = rc->bottom - rc->top;
    REAL cx = (REAL)rc->left + w / 2.0f;
    REAL cy = (REAL)rc->top  + h / 2.0f;
    REAL radius = (REAL)((w < h ? w : h) / 2) - 8.0f;
    if (radius < 30.0f) radius = 30.0f;

    FILETIME ftUtc, ftLocal;
    GetSystemTimePreciseAsFileTime(&ftUtc);
    if (!FileTimeToLocalFileTime(&ftUtc, &ftLocal))
        ftLocal = ftUtc;
    ULARGE_INTEGER uli;
    uli.LowPart  = ftLocal.dwLowDateTime;
    uli.HighPart = ftLocal.dwHighDateTime;
    ULONGLONG fileTimeMs = uli.QuadPart / 10000;
    SYSTEMTIME lt;
    FileTimeToSystemTime(&ftLocal, &lt);

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

    /* Light mode keeps the white face. Dark mode gets a dark one, with the
       markings and hands derived from it, rather than a white disc carrying
       cream numerals and near-invisible ticks. */
    ARGB faceArgb   = s->dark_mode ? 0xFF1B1F26 : 0xFFFAFAFF;
    ARGB rimArgb    = s->dark_mode ? 0xFF5A6070 : 0xFFA0A0A8;
    ARGB tickArgb   = s->dark_mode ? 0xFFD2D7E0 : colorref_to_argb(s->textColor);
    ARGB accentArgb = s->dark_mode ? 0xFF4FA3E3 : colorref_to_argb(s->accentColor);
    ARGB minArgb    = s->dark_mode ? 0xFF7FC0EA : 0xFF5BA0D0;
    ARGB secArgb    = s->dark_mode ? 0xFFFF6B6B : 0xFFFF4040;

    GpSolidFill *faceBrush = NULL;
    GdipCreateSolidFill(faceArgb, &faceBrush);
    GpPen *rimPen = NULL;
    GdipCreatePen1(rimArgb, 2.5f, UnitPixel, &rimPen);
    GdipFillEllipse(gr, (GpBrush*)faceBrush, cx - radius, cy - radius, radius * 2, radius * 2);
    GdipDrawEllipse(gr, rimPen, cx - radius, cy - radius, radius * 2, radius * 2);
    GdipDeletePen(rimPen);
    GdipDeleteBrush((GpBrush*)faceBrush);

    /* Tick marks. Two pens for the whole ring rather than one created and
       destroyed per tick - this runs on every frame. */
    GpPen *majorTick = NULL, *minorTick = NULL;
    GdipCreatePen1(tickArgb, 3.5f, UnitPixel, &majorTick);
    GdipCreatePen1(tickArgb, 1.0f, UnitPixel, &minorTick);

    int i;
    for (i = 0; i < 60; i++) {
        double a = i * M_PI / 30.0 - M_PI / 2.0;
        double ca = cos(a), sa = sin(a);
        BOOL major = (i % 5 == 0);
        double inner = major ? (radius - 18) : (radius - 10);
        GdipDrawLine(gr, major ? majorTick : minorTick,
            cx + (REAL)((radius - 4) * ca), cy + (REAL)((radius - 4) * sa),
            cx + (REAL)(inner * ca), cy + (REAL)(inner * sa));
    }

    GdipDeletePen(majorTick);
    GdipDeletePen(minorTick);

    /* Hour numbers */
    int numH = (int)radius / 8;
    if (numH < 12) numH = 12;

    GpSolidFill *numBrush = NULL;
    ARGB numArgb = s->dark_mode ? 0xFFF2E6CC : tickArgb;
    GdipCreateSolidFill(numArgb, &numBrush);

    BOOL haveNumFont = ensure_num_font_cache(numH);
    GpFont         *numFont = g_numFont;
    GpStringFormat *fmt     = g_numFmt;

    for (i = 1; haveNumFont && i <= 12; i++) {
        double a = i * M_PI / 6.0 - M_PI / 2.0;
        WCHAR num[4];
        wsprintfW(num, L"%d", i);
        /* Proportional to the glyph size, not a constant: the numeral font
           scales with the dial, so a fixed box silently dropped the second
           digit of 10, 11 and 12 once the clock got large enough. */
        REAL rw = (REAL)numH * 2.4f;
        REAL rh = (REAL)(numH + 6);
        RectF numRect = {
            cx + (REAL)((radius - 30) * cos(a)) - rw / 2.0f,
            cy + (REAL)((radius - 30) * sin(a)) - rh / 2.0f,
            rw, rh
        };
        GdipDrawString(gr, num, lstrlenW(num), numFont, &numRect, fmt, (GpBrush*)numBrush);
    }

    GdipDeleteBrush((GpBrush*)numBrush);

    /* Hands */
    REAL hLen = radius * 0.50f;
    REAL mLen = radius * 0.75f;
    REAL sLen = radius * 0.82f;

    GpPen *secPen = NULL;
    GdipCreatePen1(secArgb, 1.5f, UnitPixel, &secPen);
    GdipSetPenStartCap(secPen, LineCapRound);
    GdipSetPenEndCap(secPen, LineCapRound);
    GdipDrawLine(gr, secPen, cx, cy,
        cx + (REAL)(sLen * cos(secondAngle)), cy + (REAL)(sLen * sin(secondAngle)));
    GdipDeletePen(secPen);

    GpPen *minPen = NULL;
    GdipCreatePen1(minArgb, 3.0f, UnitPixel, &minPen);
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

    /* AM/PM marker - a 12-hour dial alone cannot tell noon from midnight. */
    if (!s->hour24 && haveNumFont && g_apFont) {
        GpSolidFill *apBrush = NULL;
        GdipCreateSolidFill(tickArgb, &apBrush);
        if (apBrush) {
            const WCHAR *ap = (lt.wHour >= 12) ? L"PM" : L"AM";
            REAL apW = (REAL)numH * 2.0f;
            REAL apH = (REAL)numH;
            RectF apRect = { cx - apW / 2.0f, cy + radius * 0.34f, apW, apH };
            GdipDrawString(gr, ap, 2, g_apFont, &apRect, g_numFmt, (GpBrush*)apBrush);
            GdipDeleteBrush((GpBrush*)apBrush);
        }
    }

    /* Center dot */
    GpSolidFill *dotBrush = NULL;
    GdipCreateSolidFill(accentArgb, &dotBrush);
    GdipFillEllipse(gr, (GpBrush*)dotBrush, cx - 4.0f, cy - 4.0f, 8.0f, 8.0f);
    GdipDeleteBrush((GpBrush*)dotBrush);

    GdipDeleteGraphics(gr);
}
