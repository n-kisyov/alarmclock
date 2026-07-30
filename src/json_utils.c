#include "json_utils.h"
#include "main.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <strsafe.h>

typedef struct { const TCHAR *p, *end; } JsonReader;
typedef struct {
    WCHAR *buf;
    size_t len;
    size_t cap;
} WStringBuilder;

static void json_skip_ws(JsonReader *r) {
    while (r->p < r->end && (*r->p == L' ' || *r->p == L'\t' || *r->p == L'\n' || *r->p == L'\r')) r->p++;
}
static BOOL json_expect(JsonReader *r, TCHAR c) {
    json_skip_ws(r);
    if (r->p >= r->end || *r->p != c) return FALSE;
    r->p++;
    return TRUE;
}
static BOOL json_read_string(JsonReader *r, TCHAR *buf, int buf_sz) {
    json_skip_ws(r);
    if (r->p >= r->end || *r->p != L'"') return FALSE;
    r->p++;
    int i = 0;
    while (r->p < r->end && *r->p != L'"' && i < buf_sz - 1) {
        if (*r->p == L'\\') {
            r->p++;
            if (r->p >= r->end) return FALSE;
            switch (*r->p) {
            case L'"': buf[i++] = L'"'; break;
            case L'\\': buf[i++] = L'\\'; break;
            case L'n': buf[i++] = L'\n'; break;
            case L'r': buf[i++] = L'\r'; break;
            case L't': buf[i++] = L'\t'; break;
            default: buf[i++] = *r->p; break;
            }
        } else {
            buf[i++] = *r->p;
        }
        r->p++;
    }
    buf[i] = 0;
    if (r->p >= r->end || *r->p != L'"') return FALSE;
    r->p++;
    return TRUE;
}
static BOOL json_read_bool(JsonReader *r, BOOL *val) {
    json_skip_ws(r);
    if (r->p + 4 <= r->end && wcsncmp(r->p, L"true", 4) == 0) { *val = TRUE; r->p += 4; return TRUE; }
    if (r->p + 5 <= r->end && wcsncmp(r->p, L"false", 5) == 0) { *val = FALSE; r->p += 5; return TRUE; }
    return FALSE;
}
static BOOL json_read_int(JsonReader *r, int *val) {
    json_skip_ws(r);
    if (r->p >= r->end) return FALSE;
    int sign = 1;
    if (*r->p == L'-') { sign = -1; r->p++; }
    if (r->p >= r->end || (*r->p < L'0' || *r->p > L'9')) return FALSE;
    int v = 0;
    while (r->p < r->end && *r->p >= L'0' && *r->p <= L'9') {
        v = v * 10 + (*r->p - L'0');
        r->p++;
    }
    *val = v * sign;
    return TRUE;
}

static BOOL sb_reserve(WStringBuilder *sb, size_t extra_chars) {
    size_t needed = sb->len + extra_chars + 1;
    if (needed <= sb->cap) return TRUE;

    size_t new_cap = sb->cap ? sb->cap : 512;
    while (new_cap < needed) new_cap *= 2;

    WCHAR *new_buf = (WCHAR *)realloc(sb->buf, new_cap * sizeof(WCHAR));
    if (!new_buf) return FALSE;

    sb->buf = new_buf;
    sb->cap = new_cap;
    return TRUE;
}

static BOOL sb_append_text(WStringBuilder *sb, const WCHAR *text) {
    size_t text_len = wcslen(text);
    if (!sb_reserve(sb, text_len)) return FALSE;
    memcpy(sb->buf + sb->len, text, (text_len + 1) * sizeof(WCHAR));
    sb->len += text_len;
    return TRUE;
}

static BOOL sb_append_char(WStringBuilder *sb, WCHAR ch) {
    if (!sb_reserve(sb, 1)) return FALSE;
    sb->buf[sb->len++] = ch;
    sb->buf[sb->len] = 0;
    return TRUE;
}

static BOOL sb_append_format(WStringBuilder *sb, const WCHAR *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int count = _vscwprintf(fmt, args);
    va_end(args);
    if (count < 0) return FALSE;
    if (!sb_reserve(sb, (size_t)count)) return FALSE;

    va_start(args, fmt);
    HRESULT hr = StringCchVPrintfW(sb->buf + sb->len, sb->cap - sb->len, fmt, args);
    va_end(args);
    if (FAILED(hr)) return FALSE;

    sb->len += (size_t)count;
    return TRUE;
}

static BOOL sb_append_json_escaped(WStringBuilder *sb, const WCHAR *text) {
    if (!sb_append_char(sb, L'"')) return FALSE;
    for (const WCHAR *p = text; *p; ++p) {
        switch (*p) {
        case L'"': if (!sb_append_text(sb, L"\\\"")) return FALSE; break;
        case L'\\': if (!sb_append_text(sb, L"\\\\")) return FALSE; break;
        case L'\n': if (!sb_append_text(sb, L"\\n")) return FALSE; break;
        case L'\r': if (!sb_append_text(sb, L"\\r")) return FALSE; break;
        case L'\t': if (!sb_append_text(sb, L"\\t")) return FALSE; break;
        default:
            if ((unsigned)*p < 0x20) {
                if (!sb_append_format(sb, L"\\u%04X", (unsigned)*p)) return FALSE;
            } else {
                if (!sb_append_char(sb, *p)) return FALSE;
            }
            break;
        }
    }
    return sb_append_char(sb, L'"');
}

BOOL json_load_settings(AppState *s, const TCHAR *path) {
    HANDLE hFile = CreateFile(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return FALSE;

    DWORD sz = GetFileSize(hFile, NULL);
    if (sz == INVALID_FILE_SIZE || sz == 0) {
        CloseHandle(hFile);
        return FALSE;
    }

    char *raw = (char *)malloc(sz + 1);
    if (!raw) {
        CloseHandle(hFile);
        return FALSE;
    }
    DWORD read = 0;
    BOOL read_ok = ReadFile(hFile, raw, sz, &read, NULL);
    CloseHandle(hFile);
    if (!read_ok || read != sz) {
        free(raw);
        return FALSE;
    }
    raw[read] = 0;

    int wide_len = MultiByteToWideChar(CP_UTF8, 0, raw, -1, NULL, 0);
    if (wide_len <= 0) {
        free(raw);
        return FALSE;
    }

    TCHAR *buf = (TCHAR *)malloc((wide_len + 1) * sizeof(TCHAR));
    if (!buf) {
        free(raw);
        return FALSE;
    }
    if (MultiByteToWideChar(CP_UTF8, 0, raw, -1, buf, wide_len) <= 0) {
        free(raw);
        free(buf);
        return FALSE;
    }
    free(raw);

    JsonReader r;
    r.p = buf;
    r.end = buf + wide_len - 1;

    alarms_init(s);
    if (!json_expect(&r, L'{')) { free(buf); return FALSE; }

    while (1) {
        json_skip_ws(&r);
        if (r.p >= r.end) { free(buf); return FALSE; }
        if (*r.p == L'}') { r.p++; break; }

        TCHAR key[128];
        if (!json_read_string(&r, key, 128)) { free(buf); return FALSE; }
        if (!json_expect(&r, L':')) { free(buf); return FALSE; }

        if (lstrcmp(key, L"dark_mode") == 0) json_read_bool(&r, &s->dark_mode);
        else if (lstrcmp(key, L"hour24") == 0) json_read_bool(&r, &s->hour24);
        else if (lstrcmp(key, L"crescendo") == 0) json_read_bool(&r, &s->crescendo);
        else if (lstrcmp(key, L"autostart") == 0) json_read_bool(&r, &s->autostart);
        else if (lstrcmp(key, L"start_minimized") == 0) json_read_bool(&r, &s->start_minimized);
        else if (lstrcmp(key, L"acrylic") == 0) json_read_bool(&r, &s->acrylic);
        else if (lstrcmp(key, L"always_on_top") == 0) json_read_bool(&r, &s->always_on_top);
        else if (lstrcmp(key, L"alarms_collapsed") == 0) json_read_bool(&r, &s->alarms_collapsed);
        else if (lstrcmp(key, L"clock_style") == 0) { TCHAR v[32]; if (json_read_string(&r, v, 32)) s->clock_style = (lstrcmp(v, L"analog") == 0) ? CLOCK_ANALOG : CLOCK_DIGITAL; }
        else if (lstrcmp(key, L"alarms_enabled") == 0) json_read_bool(&r, &s->alarms_enabled);
        else if (lstrcmp(key, L"alarm_count") == 0) { int ac = 5; json_read_int(&r, &ac); if (ac < 1) ac = 1; if (ac > MAX_ALARMS) ac = MAX_ALARMS; s->alarm_count = ac; }
        else if (lstrcmp(key, L"alarm_volume") == 0) { int av = 80; json_read_int(&r, &av); if (av >= 0 && av <= 100) s->alarm_volume = av; }
        else if (lstrcmp(key, L"snooze_minutes") == 0) { int sm = 5; json_read_int(&r, &sm); if (sm >= 1 && sm <= 60) s->snooze_minutes = sm; }
        else if (lstrcmp(key, L"app_mode") == 0) { int am = 0; json_read_int(&r, &am); if (am >= 0 && am <= 2) s->app_mode = am; }
        else if (lstrcmp(key, L"win_x") == 0) json_read_int(&r, &s->winX);
        else if (lstrcmp(key, L"win_y") == 0) json_read_int(&r, &s->winY);
        else if (lstrcmp(key, L"win_w") == 0) json_read_int(&r, &s->winW);
        else if (lstrcmp(key, L"win_h") == 0) json_read_int(&r, &s->winH);
        else if (lstrcmp(key, L"sound_mode") == 0) { TCHAR v[32]; if (json_read_string(&r, v, 32)) s->sound_mode = (lstrcmp(v, L"mp3") == 0) ? SOUND_MP3 : SOUND_SIMPLE; }
        else if (lstrcmp(key, L"cd_hours") == 0) json_read_int(&r, &s->cd_hours);
        else if (lstrcmp(key, L"cd_mins") == 0) json_read_int(&r, &s->cd_mins);
        else if (lstrcmp(key, L"cd_secs") == 0) json_read_int(&r, &s->cd_secs);
        else if (lstrcmp(key, L"alarms") == 0) {
            if (!json_expect(&r, L'[')) { free(buf); return FALSE; }
            int idx = 0;
            while (idx < MAX_ALARMS) {
                json_skip_ws(&r);
                if (r.p >= r.end || *r.p == L']') { r.p++; break; }
                if (!json_expect(&r, L'{')) { free(buf); return FALSE; }
                BOOL hasRepeatDays = FALSE;
                while (1) {
                    json_skip_ws(&r);
                    if (r.p >= r.end) { free(buf); return FALSE; }
                    if (*r.p == L'}') { r.p++; break; }

                    TCHAR akey[64];
                    if (!json_read_string(&r, akey, 64)) { free(buf); return FALSE; }
                    if (!json_expect(&r, L':')) { free(buf); return FALSE; }

                    if (lstrcmp(akey, L"hour") == 0) json_read_int(&r, &s->alarms[idx].hour);
                    else if (lstrcmp(akey, L"minute") == 0) json_read_int(&r, &s->alarms[idx].minute);
                    else if (lstrcmp(akey, L"enabled") == 0) json_read_bool(&r, &s->alarms[idx].enabled);
                    else if (lstrcmp(akey, L"label") == 0) { TCHAR lb[32]; if (json_read_string(&r, lb, 32)) lstrcpynW(s->alarms[idx].label, lb, 32); }
                    else if (lstrcmp(akey, L"repeat_days") == 0) { int rd = 0; json_read_int(&r, &rd); s->alarms[idx].repeat_days = (BYTE)rd; hasRepeatDays = TRUE; }
                    else if (lstrcmp(akey, L"repeat") == 0) {
                        int rm = 0;
                        json_read_int(&r, &rm);
                        if (!hasRepeatDays) {
                            if (rm == 0) s->alarms[idx].repeat_days = 0;
                            else if (rm == 1) s->alarms[idx].repeat_days = 0x7F;
                            else if (rm == 2) s->alarms[idx].repeat_days = 0x3E;
                            else if (rm == 3) s->alarms[idx].repeat_days = 0x41;
                        }
                    }

                    json_skip_ws(&r);
                    if (r.p < r.end && *r.p != L'}') { if (!json_expect(&r, L',')) { free(buf); return FALSE; } }
                }
                idx++;
                json_skip_ws(&r);
                if (r.p < r.end && *r.p != L']') { if (!json_expect(&r, L',')) { free(buf); return FALSE; } }
            }
            json_skip_ws(&r);
            if (r.p < r.end && *r.p == L']') r.p++;
        }

        json_skip_ws(&r);
        if (r.p < r.end && *r.p != L'}') { if (!json_expect(&r, L',')) { free(buf); return FALSE; } }
    }

    free(buf);
    theme_update_colors(s);
    if (s->cd_remaining_ms == 0)
        s->cd_remaining_ms = (s->cd_hours * 3600 + s->cd_mins * 60 + s->cd_secs) * 1000;

    return TRUE;
}
BOOL json_save_settings(const AppState *s, const TCHAR *path) {
    WStringBuilder sb = {0};
    HANDLE hFile = INVALID_HANDLE_VALUE;
    char *utf8buf = NULL;
    BOOL ok = FALSE;

    if (!sb_append_text(&sb,
        L"{\n"
        L"  \"dark_mode\": ")) goto cleanup;
    if (!sb_append_text(&sb, s->dark_mode ? L"true" : L"false")) goto cleanup;
    if (!sb_append_text(&sb, L",\n  \"hour24\": ")) goto cleanup;
    if (!sb_append_text(&sb, s->hour24 ? L"true" : L"false")) goto cleanup;
    if (!sb_append_text(&sb, L",\n  \"crescendo\": ")) goto cleanup;
    if (!sb_append_text(&sb, s->crescendo ? L"true" : L"false")) goto cleanup;
    if (!sb_append_text(&sb, L",\n  \"autostart\": ")) goto cleanup;
    if (!sb_append_text(&sb, s->autostart ? L"true" : L"false")) goto cleanup;
    if (!sb_append_text(&sb, L",\n  \"start_minimized\": ")) goto cleanup;
    if (!sb_append_text(&sb, s->start_minimized ? L"true" : L"false")) goto cleanup;
    if (!sb_append_text(&sb, L",\n  \"acrylic\": ")) goto cleanup;
    if (!sb_append_text(&sb, s->acrylic ? L"true" : L"false")) goto cleanup;
    if (!sb_append_text(&sb, L",\n  \"always_on_top\": ")) goto cleanup;
    if (!sb_append_text(&sb, s->always_on_top ? L"true" : L"false")) goto cleanup;
    if (!sb_append_format(&sb,
        L",\n  \"clock_style\": \"%s\",\n"
        L"  \"alarms_enabled\": %s,\n"
        L"  \"alarm_count\": %d,\n"
        L"  \"alarms_collapsed\": %s,\n"
        L"  \"alarm_volume\": %d,\n"
        L"  \"snooze_minutes\": %d,\n"
        L"  \"app_mode\": %d,\n"
        L"  \"win_x\": %d,\n"
        L"  \"win_y\": %d,\n"
        L"  \"win_w\": %d,\n"
        L"  \"win_h\": %d,\n"
        L"  \"sound_mode\": \"%s\",\n"
        L"  \"cd_hours\": %d,\n"
        L"  \"cd_mins\": %d,\n"
        L"  \"cd_secs\": %d,\n"
        L"  \"alarms\": [\n",
        s->clock_style == CLOCK_ANALOG ? L"analog" : L"digital",
        s->alarms_enabled ? L"true" : L"false", s->alarm_count,
        s->alarms_collapsed ? L"true" : L"false",
        s->alarm_volume,
        s->snooze_minutes, s->app_mode, s->winX, s->winY, s->winW, s->winH,
        s->sound_mode == SOUND_MP3 ? L"mp3" : L"simple",
        s->cd_hours, s->cd_mins, s->cd_secs)) goto cleanup;

    for (int i = 0; i < MAX_ALARMS; i++) {
        if (!sb_append_format(&sb,
            L"    {\"hour\": %d, \"minute\": %d, \"enabled\": %s, \"label\": ",
            s->alarms[i].hour, s->alarms[i].minute,
            s->alarms[i].enabled ? L"true" : L"false")) goto cleanup;
        if (!sb_append_json_escaped(&sb, s->alarms[i].label)) goto cleanup;
        if (!sb_append_format(&sb, L", \"repeat_days\": %d}%s\n",
            (int)s->alarms[i].repeat_days,
            (i < MAX_ALARMS - 1) ? L"," : L"")) goto cleanup;
    }

    if (!sb_append_text(&sb, L"  ]\n}\n")) goto cleanup;

    hFile = CreateFile(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) goto cleanup;

    int utf8_len = WideCharToMultiByte(CP_UTF8, 0, sb.buf, -1, NULL, 0, NULL, NULL);
    if (utf8_len <= 0) goto cleanup;

    utf8buf = (char *)malloc((size_t)utf8_len);
    if (!utf8buf) goto cleanup;

    if (WideCharToMultiByte(CP_UTF8, 0, sb.buf, -1, utf8buf, utf8_len, NULL, NULL) <= 0) goto cleanup;

    DWORD to_write = (DWORD)(utf8_len - 1);
    DWORD written = 0;
    if (!WriteFile(hFile, utf8buf, to_write, &written, NULL) || written != to_write) goto cleanup;

    ok = TRUE;

cleanup:
    if (hFile != INVALID_HANDLE_VALUE) CloseHandle(hFile);
    free(utf8buf);
    free(sb.buf);
    return ok;
}
