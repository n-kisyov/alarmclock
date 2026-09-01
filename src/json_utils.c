#include "json_utils.h"
#include "main.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <strsafe.h>
#include <limits.h>

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
    while (r->p < r->end && *r->p != L'"') {
        TCHAR ch;
        if (*r->p == L'\\') {
            r->p++;
            if (r->p >= r->end) return FALSE;
            switch (*r->p) {
            case L'"':  ch = L'"';  break;
            case L'\\': ch = L'\\'; break;
            case L'n':  ch = L'\n'; break;
            case L'r':  ch = L'\r'; break;
            case L't':  ch = L'\t'; break;
            case L'u': {
                /* The writer emits \uXXXX for control characters, so without this
                   a label that round-trips through the file came back as the
                   literal text "u0001". */
                if (r->p + 4 >= r->end) return FALSE;
                int cp = 0;
                for (int k = 1; k <= 4; k++) {
                    TCHAR d = r->p[k];
                    int nib;
                    if      (d >= L'0' && d <= L'9') nib = d - L'0';
                    else if (d >= L'a' && d <= L'f') nib = 10 + (d - L'a');
                    else if (d >= L'A' && d <= L'F') nib = 10 + (d - L'A');
                    else return FALSE;
                    cp = cp * 16 + nib;
                }
                r->p += 4;
                ch = (TCHAR)cp;
                break;
            }
            default: ch = *r->p; break;
            }
        } else {
            ch = *r->p;
        }
        /* Scan on past a full buffer instead of stopping mid-string. Bailing out
           here left the reader parked inside the quotes, which made everything
           after it unparseable and condemned the file - so one over-long label
           threw away every setting in it. Over-long values truncate now. */
        if (i < buf_sz - 1) buf[i++] = ch;
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
    /* Accumulated wide and saturated: a long digit run used to overflow int and
       wrap to whatever fell out, which then sailed through the range checks. */
    long long v = 0;
    while (r->p < r->end && *r->p >= L'0' && *r->p <= L'9') {
        if (v <= (long long)INT_MAX) v = v * 10 + (*r->p - L'0');
        r->p++;
    }
    if (v > (long long)INT_MAX) v = INT_MAX;
    *val = (int)v * sign;
    return TRUE;
}

/* Consumes exactly one value of any type without interpreting it. */
static BOOL json_skip_value(JsonReader *r) {
    json_skip_ws(r);
    if (r->p >= r->end) return FALSE;

    if (*r->p == L'"') {
        TCHAR scratch[8];
        return json_read_string(r, scratch, ARRAYSIZE(scratch));
    }

    if (*r->p == L'{' || *r->p == L'[') {
        int depth = 0;
        while (r->p < r->end) {
            if (*r->p == L'"') {
                TCHAR scratch[8];
                /* Braces inside a string are not structure. */
                if (!json_read_string(r, scratch, ARRAYSIZE(scratch))) return FALSE;
                continue;
            }
            if (*r->p == L'{' || *r->p == L'[') depth++;
            else if (*r->p == L'}' || *r->p == L']') {
                if (--depth == 0) { r->p++; return TRUE; }
            }
            r->p++;
        }
        return FALSE;
    }

    if (*r->p == L'-' || (*r->p >= L'0' && *r->p <= L'9')) {
        /* A number this build has no use for may still be fractional. */
        r->p++;
        while (r->p < r->end &&
               ((*r->p >= L'0' && *r->p <= L'9') || *r->p == L'.' ||
                *r->p == L'e' || *r->p == L'E' || *r->p == L'+' || *r->p == L'-'))
            r->p++;
        return TRUE;
    }

    BOOL ignored;
    if (json_read_bool(r, &ignored)) return TRUE;
    if (r->p + 4 <= r->end && wcsncmp(r->p, L"null", 4) == 0) { r->p += 4; return TRUE; }
    return FALSE;
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

/* Reads the whole file as UTF-8 and returns it as a NUL-terminated wide string,
   or NULL. *out_len receives the character count excluding the terminator. */
static TCHAR *read_text_file_utf8(const TCHAR *path, int *out_len, SettingsLoadResult *why) {
    HANDLE hFile = CreateFile(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        *why = (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND)
                 ? SETTINGS_MISSING : SETTINGS_CORRUPT;
        return NULL;
    }

    *why = SETTINGS_CORRUPT;

    DWORD sz = GetFileSize(hFile, NULL);
    if (sz == INVALID_FILE_SIZE || sz == 0) {
        CloseHandle(hFile);
        return NULL;
    }

    char *raw = (char *)malloc(sz + 1);
    if (!raw) {
        CloseHandle(hFile);
        return NULL;
    }
    DWORD read = 0;
    BOOL read_ok = ReadFile(hFile, raw, sz, &read, NULL);
    CloseHandle(hFile);
    if (!read_ok || read != sz) {
        free(raw);
        return NULL;
    }
    raw[read] = 0;

    /* Skip a UTF-8 BOM if an external editor added one. */
    const char *start = raw;
    if (read >= 3 && (unsigned char)raw[0] == 0xEF &&
        (unsigned char)raw[1] == 0xBB && (unsigned char)raw[2] == 0xBF) {
        start += 3;
    }

    int wide_len = MultiByteToWideChar(CP_UTF8, 0, start, -1, NULL, 0);
    if (wide_len <= 0) {
        free(raw);
        return NULL;
    }

    TCHAR *buf = (TCHAR *)malloc((size_t)(wide_len + 1) * sizeof(TCHAR));
    if (!buf) {
        free(raw);
        return NULL;
    }
    if (MultiByteToWideChar(CP_UTF8, 0, start, -1, buf, wide_len) <= 0) {
        free(raw);
        free(buf);
        return NULL;
    }
    free(raw);

    *out_len = wide_len;
    return buf;
}

/* Parses into a scratch copy and only commits it to *s once the whole file has
   been read successfully, so a truncated or hand-edited file can never leave
   the app running on half-loaded settings - or, worse, save that half back. */
SettingsLoadResult json_load_settings(AppState *s, const TCHAR *path) {
    int wide_len = 0;
    SettingsLoadResult why = SETTINGS_CORRUPT;
    TCHAR *buf = read_text_file_utf8(path, &wide_len, &why);
    if (!buf) return why;

    AppState scratch = *s;
    AppState *t = &scratch;

    JsonReader r;
    r.p = buf;
    r.end = buf + wide_len - 1;

    alarms_init(t);
    if (!json_expect(&r, L'{')) { free(buf); return SETTINGS_CORRUPT; }

    while (1) {
        json_skip_ws(&r);
        if (r.p >= r.end) { free(buf); return SETTINGS_CORRUPT; }
        if (*r.p == L'}') { r.p++; break; }

        TCHAR key[128];
        if (!json_read_string(&r, key, 128)) { free(buf); return SETTINGS_CORRUPT; }
        if (!json_expect(&r, L':')) { free(buf); return SETTINGS_CORRUPT; }

        const TCHAR *valueStart = r.p;

        if (lstrcmp(key, L"dark_mode") == 0) json_read_bool(&r, &t->dark_mode);
        else if (lstrcmp(key, L"hour24") == 0) json_read_bool(&r, &t->hour24);
        else if (lstrcmp(key, L"crescendo") == 0) json_read_bool(&r, &t->crescendo);
        else if (lstrcmp(key, L"autostart") == 0) json_read_bool(&r, &t->autostart);
        else if (lstrcmp(key, L"start_minimized") == 0) json_read_bool(&r, &t->start_minimized);
        else if (lstrcmp(key, L"acrylic") == 0) json_read_bool(&r, &t->acrylic);
        else if (lstrcmp(key, L"always_on_top") == 0) json_read_bool(&r, &t->always_on_top);
        else if (lstrcmp(key, L"alarms_collapsed") == 0) json_read_bool(&r, &t->alarms_collapsed);
        else if (lstrcmp(key, L"clock_style") == 0) { TCHAR v[32]; if (json_read_string(&r, v, 32)) t->clock_style = (lstrcmp(v, L"analog") == 0) ? CLOCK_ANALOG : CLOCK_DIGITAL; }
        else if (lstrcmp(key, L"alarms_enabled") == 0) json_read_bool(&r, &t->alarms_enabled);
        /* Assign only on a successful read. These used to seed a local with a
           hardcoded default and store it regardless, so a value the reader could
           not parse silently reset the setting instead of leaving it alone. */
        else if (lstrcmp(key, L"alarm_count") == 0) { int ac; if (json_read_int(&r, &ac)) { if (ac < 1) ac = 1; if (ac > MAX_ALARMS) ac = MAX_ALARMS; t->alarm_count = ac; } }
        else if (lstrcmp(key, L"alarm_volume") == 0) { int av; if (json_read_int(&r, &av) && av >= 10 && av <= 100) t->alarm_volume = av; }
        else if (lstrcmp(key, L"snooze_minutes") == 0) { int sm; if (json_read_int(&r, &sm) && sm >= 1 && sm <= 60) t->snooze_minutes = sm; }
        else if (lstrcmp(key, L"app_mode") == 0) { int am; if (json_read_int(&r, &am) && am >= 0 && am <= 2) t->app_mode = am; }
        else if (lstrcmp(key, L"sleep_minutes") == 0) { int sl; if (json_read_int(&r, &sl) && sl >= 1 && sl <= 240) t->sleep_minutes = sl; }
        /* Minutes since the FILETIME epoch: about 223 million in this century,
           so it sits comfortably inside an int for the next few thousand years. */
        else if (lstrcmp(key, L"last_seen") == 0) { int ls; if (json_read_int(&r, &ls) && ls > 0) t->last_seen_stamp = (ULONGLONG)ls; }
        else if (lstrcmp(key, L"win_x") == 0) json_read_int(&r, &t->winX);
        else if (lstrcmp(key, L"win_y") == 0) json_read_int(&r, &t->winY);
        else if (lstrcmp(key, L"win_w") == 0) json_read_int(&r, &t->winW);
        else if (lstrcmp(key, L"win_h") == 0) json_read_int(&r, &t->winH);
        else if (lstrcmp(key, L"sound_mode") == 0) { TCHAR v[32]; if (json_read_string(&r, v, 32)) t->sound_mode = (lstrcmp(v, L"mp3") == 0) ? SOUND_MP3 : SOUND_SIMPLE; }
        else if (lstrcmp(key, L"cd_hours") == 0) json_read_int(&r, &t->cd_hours);
        else if (lstrcmp(key, L"cd_mins") == 0) json_read_int(&r, &t->cd_mins);
        else if (lstrcmp(key, L"cd_secs") == 0) json_read_int(&r, &t->cd_secs);
        else if (lstrcmp(key, L"alarms") == 0) {
            if (!json_expect(&r, L'[')) { free(buf); return SETTINGS_CORRUPT; }
            int idx = 0;
            while (1) {
                json_skip_ws(&r);
                if (r.p >= r.end) { free(buf); return SETTINGS_CORRUPT; }
                if (*r.p == L']') { r.p++; break; }
                if (!json_expect(&r, L'{')) { free(buf); return SETTINGS_CORRUPT; }

                /* Entries past MAX_ALARMS are parsed and discarded rather than
                   abandoned mid-array, which would desync the reader. */
                Alarm discard;
                Alarm *a = (idx < MAX_ALARMS) ? &t->alarms[idx] : &discard;
                if (a == &discard) {
                    ZeroMemory(&discard, sizeof(discard));
                    a->hour = ALARM_UNSET; a->minute = ALARM_UNSET;
                    a->volume = -1; a->snooze_minutes = -1;
                }

                BOOL hasRepeatDays = FALSE;
                while (1) {
                    json_skip_ws(&r);
                    if (r.p >= r.end) { free(buf); return SETTINGS_CORRUPT; }
                    if (*r.p == L'}') { r.p++; break; }

                    TCHAR akey[64];
                    if (!json_read_string(&r, akey, 64)) { free(buf); return SETTINGS_CORRUPT; }
                    if (!json_expect(&r, L':')) { free(buf); return SETTINGS_CORRUPT; }

                    const TCHAR *alarmValueStart = r.p;

                    if (lstrcmp(akey, L"hour") == 0) json_read_int(&r, &a->hour);
                    else if (lstrcmp(akey, L"minute") == 0) json_read_int(&r, &a->minute);
                    else if (lstrcmp(akey, L"enabled") == 0) json_read_bool(&r, &a->enabled);
                    else if (lstrcmp(akey, L"label") == 0) { TCHAR lb[32]; if (json_read_string(&r, lb, 32)) lstrcpynW(a->label, lb, 32); }
                    else if (lstrcmp(akey, L"sound") == 0) { TCHAR sp[MAX_PATH]; if (json_read_string(&r, sp, MAX_PATH)) lstrcpynW(a->sound, sp, MAX_PATH); }
                    else if (lstrcmp(akey, L"volume") == 0) { int av; if (json_read_int(&r, &av)) a->volume = (av >= 10 && av <= 100) ? av : -1; }
                    else if (lstrcmp(akey, L"snooze_minutes") == 0) { int sm; if (json_read_int(&r, &sm)) a->snooze_minutes = (sm >= 1 && sm <= 60) ? sm : -1; }
                    else if (lstrcmp(akey, L"skip_next") == 0) json_read_bool(&r, &a->skip_next);
                    else if (lstrcmp(akey, L"repeat_days") == 0) { int rd; if (json_read_int(&r, &rd)) { a->repeat_days = (BYTE)rd; hasRepeatDays = TRUE; } }
                    else if (lstrcmp(akey, L"repeat") == 0) {
                        int rm;
                        if (json_read_int(&r, &rm) && !hasRepeatDays) {
                            if (rm == 0) a->repeat_days = 0;
                            else if (rm == 1) a->repeat_days = 0x7F;
                            else if (rm == 2) a->repeat_days = 0x3E;
                            else if (rm == 3) a->repeat_days = 0x41;
                        }
                    }

                    /* Same fallback as the top level, for per-alarm keys. */
                    json_skip_ws(&r);
                    if (r.p < r.end && *r.p != L'}' && *r.p != L',') {
                        r.p = alarmValueStart;
                        if (!json_skip_value(&r)) { free(buf); return SETTINGS_CORRUPT; }
                        json_skip_ws(&r);
                    }

                    if (r.p < r.end && *r.p != L'}') { if (!json_expect(&r, L',')) { free(buf); return SETTINGS_CORRUPT; } }
                }

                /* An hour or minute outside range means the slot is unset. */
                if (a->hour < 0 || a->hour > 23 || a->minute < 0 || a->minute > 59) {
                    a->hour = ALARM_UNSET;
                    a->minute = ALARM_UNSET;
                    a->enabled = FALSE;
                    a->skip_next = FALSE;
                }

                idx++;
                json_skip_ws(&r);
                if (r.p < r.end && *r.p != L']') { if (!json_expect(&r, L',')) { free(buf); return SETTINGS_CORRUPT; } }
            }
        }

        /* If the reader is not sitting on a separator, nothing above consumed the
           value: the key is one this build does not know, or it carries a type
           this build did not expect. Either way, skip it. Previously the reader
           stayed parked on the value, the separator check below failed, and a
           single unrecognised key condemned the whole file to SETTINGS_CORRUPT -
           which meant no new setting could ever be added without older builds
           discarding the user's alarms. */
        json_skip_ws(&r);
        if (r.p < r.end && *r.p != L'}' && *r.p != L',') {
            r.p = valueStart;
            if (!json_skip_value(&r)) { free(buf); return SETTINGS_CORRUPT; }
            json_skip_ws(&r);
        }

        if (r.p < r.end && *r.p != L'}') { if (!json_expect(&r, L',')) { free(buf); return SETTINGS_CORRUPT; } }
    }

    free(buf);

    cd_clamp(t);
    if (t->cd_remaining_ms == 0)
        t->cd_remaining_ms = cd_total_ms(t);

    *s = scratch;
    return SETTINGS_OK;
}

/* Writes to a sibling .tmp and swaps it into place, so an interrupted write
   leaves the previous settings intact instead of a truncated file. The swap
   also leaves the outgoing file behind as .bak for recovery. */
BOOL json_save_settings(const AppState *s, const TCHAR *path) {
    WStringBuilder sb = {0};
    HANDLE hFile = INVALID_HANDLE_VALUE;
    char *utf8buf = NULL;
    BOOL ok = FALSE;
    WCHAR tmpPath[MAX_PATH];
    WCHAR bakPath[MAX_PATH];

    if (FAILED(StringCchPrintfW(tmpPath, MAX_PATH, L"%s.tmp", path))) return FALSE;
    if (FAILED(StringCchPrintfW(bakPath, MAX_PATH, L"%s.bak", path))) return FALSE;

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
        L"  \"sleep_minutes\": %d,\n"
        L"  \"last_seen\": %d,\n"
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
        s->snooze_minutes, s->sleep_minutes, (int)s->last_seen_stamp, s->app_mode,
        s->winX, s->winY, s->winW, s->winH,
        s->sound_mode == SOUND_MP3 ? L"mp3" : L"simple",
        s->cd_hours, s->cd_mins, s->cd_secs)) goto cleanup;

    for (int i = 0; i < MAX_ALARMS; i++) {
        if (!sb_append_format(&sb,
            L"    {\"hour\": %d, \"minute\": %d, \"enabled\": %s, \"label\": ",
            s->alarms[i].hour, s->alarms[i].minute,
            s->alarms[i].enabled ? L"true" : L"false")) goto cleanup;
        if (!sb_append_json_escaped(&sb, s->alarms[i].label)) goto cleanup;
        if (!sb_append_format(&sb,
            L", \"repeat_days\": %d, \"volume\": %d, "
            L"\"snooze_minutes\": %d, \"skip_next\": %s, \"sound\": ",
            (int)s->alarms[i].repeat_days,
            s->alarms[i].volume,
            s->alarms[i].snooze_minutes,
            s->alarms[i].skip_next ? L"true" : L"false")) goto cleanup;
        if (!sb_append_json_escaped(&sb, s->alarms[i].sound)) goto cleanup;
        if (!sb_append_format(&sb, L"}%s\n",
            (i < MAX_ALARMS - 1) ? L"," : L"")) goto cleanup;
    }

    if (!sb_append_text(&sb, L"  ]\n}\n")) goto cleanup;

    hFile = CreateFile(tmpPath, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) goto cleanup;

    int utf8_len = WideCharToMultiByte(CP_UTF8, 0, sb.buf, -1, NULL, 0, NULL, NULL);
    if (utf8_len <= 0) goto cleanup;

    utf8buf = (char *)malloc((size_t)utf8_len);
    if (!utf8buf) goto cleanup;

    if (WideCharToMultiByte(CP_UTF8, 0, sb.buf, -1, utf8buf, utf8_len, NULL, NULL) <= 0) goto cleanup;

    DWORD to_write = (DWORD)(utf8_len - 1);
    DWORD written = 0;
    if (!WriteFile(hFile, utf8buf, to_write, &written, NULL) || written != to_write) goto cleanup;

    /* Get the bytes on disk before the swap, so a crash during the swap cannot
       promote a partially flushed file. */
    FlushFileBuffers(hFile);
    CloseHandle(hFile);
    hFile = INVALID_HANDLE_VALUE;

    if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES) {
        if (!ReplaceFileW(path, tmpPath, bakPath, REPLACEFILE_IGNORE_MERGE_ERRORS, NULL, NULL))
            goto cleanup;
    } else {
        if (!MoveFileExW(tmpPath, path, MOVEFILE_REPLACE_EXISTING))
            goto cleanup;
    }

    ok = TRUE;

cleanup:
    if (hFile != INVALID_HANDLE_VALUE) CloseHandle(hFile);
    if (!ok) DeleteFileW(tmpPath);
    free(utf8buf);
    free(sb.buf);
    return ok;
}
