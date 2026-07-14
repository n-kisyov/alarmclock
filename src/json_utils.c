#include "json_utils.h"
#include "main.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const TCHAR *p;
    const TCHAR *end;
} JsonReader;

static void json_skip_ws(JsonReader *r) {
    while (r->p < r->end && (*r->p == L' ' || *r->p == L'\t' || *r->p == L'\n' || *r->p == L'\r'))
        r->p++;
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
    if (r->p + 4 <= r->end && wcsncmp(r->p, L"true", 4) == 0) {
        *val = TRUE; r->p += 4; return TRUE;
    }
    if (r->p + 5 <= r->end && wcsncmp(r->p, L"false", 5) == 0) {
        *val = FALSE; r->p += 5; return TRUE;
    }
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

BOOL json_load_settings(AppState *s, const TCHAR *path) {

    HANDLE hFile = CreateFile(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return FALSE;

    DWORD sz = GetFileSize(hFile, NULL);
    if (sz == INVALID_FILE_SIZE || sz == 0) { CloseHandle(hFile); return FALSE; }

    TCHAR *buf = (TCHAR *)malloc(sz + sizeof(TCHAR));
    if (!buf) { CloseHandle(hFile); return FALSE; }

    DWORD read;
    ReadFile(hFile, buf, sz, &read, NULL);
    CloseHandle(hFile);
    buf[read / sizeof(TCHAR)] = 0;

    JsonReader r;
    r.p = buf;
    r.end = buf + (read / sizeof(TCHAR));

    alarms_init(s);

    if (!json_expect(&r, L'{')) { free(buf); return FALSE; }

    while (1) {
        json_skip_ws(&r);
        if (r.p >= r.end) { free(buf); return FALSE; }
        if (*r.p == L'}') { r.p++; break; }

        TCHAR key[128];
        if (!json_read_string(&r, key, 128)) { free(buf); return FALSE; }
        if (!json_expect(&r, L':')) { free(buf); return FALSE; }

        if (lstrcmp(key, L"dark_mode") == 0) {
            json_read_bool(&r, &s->dark_mode);
        } else if (lstrcmp(key, L"clock_style") == 0) {
            TCHAR val[32];
            if (json_read_string(&r, val, 32)) {
                s->clock_style = (lstrcmp(val, L"analog") == 0) ? CLOCK_ANALOG : CLOCK_DIGITAL;
            }
        } else if (lstrcmp(key, L"alarms_enabled") == 0) {
            json_read_bool(&r, &s->alarms_enabled);
        } else if (lstrcmp(key, L"alarm_count") == 0) {
            int ac = 5;
            json_read_int(&r, &ac);
            if (ac < 1) ac = 1;
            if (ac > MAX_ALARMS) ac = MAX_ALARMS;
            s->alarm_count = ac;
        } else if (lstrcmp(key, L"sound_mode") == 0) {
            TCHAR val[32];
            if (json_read_string(&r, val, 32)) {
                s->sound_mode = (lstrcmp(val, L"mp3") == 0) ? SOUND_MP3 : SOUND_SIMPLE;
            }
        } else if (lstrcmp(key, L"alarms") == 0) {
            if (!json_expect(&r, L'[')) { free(buf); return FALSE; }
            int idx = 0;
            while (idx < MAX_ALARMS) {
                json_skip_ws(&r);
                if (r.p >= r.end) { free(buf); return FALSE; }
                if (*r.p == L']') { r.p++; break; }

                if (!json_expect(&r, L'{')) { free(buf); return FALSE; }

                while (1) {
                    json_skip_ws(&r);
                    if (r.p >= r.end) { free(buf); return FALSE; }
                    if (*r.p == L'}') { r.p++; break; }

                    TCHAR akey[64];
                    if (!json_read_string(&r, akey, 64)) { free(buf); return FALSE; }
                    if (!json_expect(&r, L':')) { free(buf); return FALSE; }

                    if (lstrcmp(akey, L"hour") == 0) {
                        json_read_int(&r, &s->alarms[idx].hour);
                    } else if (lstrcmp(akey, L"minute") == 0) {
                        json_read_int(&r, &s->alarms[idx].minute);
                    } else if (lstrcmp(akey, L"enabled") == 0) {
                        json_read_bool(&r, &s->alarms[idx].enabled);
                    }

                    json_skip_ws(&r);
                    if (r.p < r.end && *r.p != L'}') {
                        if (!json_expect(&r, L',')) { free(buf); return FALSE; }
                    }
                }
                idx++;
                json_skip_ws(&r);
                if (r.p < r.end && *r.p != L']') {
                    if (!json_expect(&r, L',')) { free(buf); return FALSE; }
                }
            }
        }

        json_skip_ws(&r);
        if (r.p < r.end && *r.p != L'}') {
            if (!json_expect(&r, L',')) { free(buf); return FALSE; }
        }
    }

    free(buf);
    theme_update_colors(s);
    return TRUE;
}

BOOL json_save_settings(const AppState *s, const TCHAR *path) {

    HANDLE hFile = CreateFile(path, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) return FALSE;

    TCHAR buf[4096];
    int len = 0;

    len = wsprintf(buf,
        L"{\r\n"
        L"  \"dark_mode\": %s,\r\n"
        L"  \"clock_style\": \"%s\",\r\n"
        L"  \"alarms_enabled\": %s,\r\n"
        L"  \"alarm_count\": %d,\r\n"
        L"  \"sound_mode\": \"%s\",\r\n"
        L"  \"alarms\": [\r\n",
        s->dark_mode ? L"true" : L"false",
        s->clock_style == CLOCK_ANALOG ? L"analog" : L"digital",
        s->alarms_enabled ? L"true" : L"false",
        s->alarm_count,
        s->sound_mode == SOUND_MP3 ? L"mp3" : L"simple");

    for (int i = 0; i < MAX_ALARMS; i++) {
        TCHAR comma = (i < MAX_ALARMS - 1) ? L',' : L' ';
        len += wsprintf(buf + len,
            L"    {\"hour\": %d, \"minute\": %d, \"enabled\": %s}%c\r\n",
            s->alarms[i].hour, s->alarms[i].minute,
            s->alarms[i].enabled ? L"true" : L"false", comma);
    }

    len += wsprintf(buf + len, L"  ]\r\n}\r\n");

    DWORD written;
    DWORD bytes = (DWORD)(len * sizeof(TCHAR));
    WriteFile(hFile, buf, bytes, &written, NULL);
    CloseHandle(hFile);
    return TRUE;
}
