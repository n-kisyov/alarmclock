/* Console harness over the settings load/save path. */
#include <windows.h>
#include <stdio.h>
#include "main.h"
#include "json_utils.h"

static int fails = 0;
static void check(const char *what, int cond) {
    printf("  %-58s %s\n", what, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}

static void write_raw(const WCHAR *path, const char *bytes) {
    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    DWORD w = 0;
    WriteFile(h, bytes, (DWORD)strlen(bytes), &w, NULL);
    CloseHandle(h);
}

static BOOL exists(const WCHAR *path) {
    return GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
}

static void defaults(AppState *s) {
    ZeroMemory(s, sizeof(*s));
    s->alarm_count = 5;
    s->alarm_volume = 80;
    s->snooze_minutes = 3;
    s->hour24 = TRUE;
    s->alarms_enabled = TRUE;
    alarms_init(s);
}

int main(void) {
    WCHAR dir[MAX_PATH], path[MAX_PATH], bak[MAX_PATH], tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, dir);
    wcscat(dir, L"acsettings_test");
    CreateDirectoryW(dir, NULL);
    swprintf(path, MAX_PATH, L"%s\\alarmclock_settings.json", dir);
    swprintf(bak,  MAX_PATH, L"%s.bak", path);
    swprintf(tmp,  MAX_PATH, L"%s.tmp", path);
    DeleteFileW(path); DeleteFileW(bak); DeleteFileW(tmp);

    AppState s;

    printf("\nmissing file is not an error\n");
    defaults(&s);
    check("returns SETTINGS_MISSING", json_load_settings(&s, path) == SETTINGS_MISSING);

    printf("\nround trip preserves alarms\n");
    defaults(&s);
    s.alarms[0].hour = 7; s.alarms[0].minute = 30; s.alarms[0].enabled = TRUE;
    s.alarms[0].repeat_days = 0x3E; lstrcpyW(s.alarms[0].label, L"Work \"shift\"");
    s.alarms[2].hour = 0;  s.alarms[2].minute = 0;  s.alarms[2].enabled = TRUE;
    s.snooze_minutes = 15; s.alarm_volume = 60; s.dark_mode = TRUE;
    check("save succeeds", json_save_settings(&s, path));
    check("no .tmp left behind", !exists(tmp));

    AppState r; defaults(&r);
    check("load succeeds", json_load_settings(&r, path) == SETTINGS_OK);
    check("alarm 0 hour survived", r.alarms[0].hour == 7);
    check("alarm 0 minute survived", r.alarms[0].minute == 30);
    check("alarm 0 enabled survived", r.alarms[0].enabled == TRUE);
    check("alarm 0 repeat mask survived", r.alarms[0].repeat_days == 0x3E);
    check("alarm 0 quoted label survived", lstrcmpW(r.alarms[0].label, L"Work \"shift\"") == 0);
    check("midnight alarm survived as 00:00", r.alarms[2].hour == 0 && r.alarms[2].minute == 0);
    check("midnight alarm still enabled", r.alarms[2].enabled == TRUE);
    check("untouched slot stays unset", r.alarms[1].hour == ALARM_UNSET);
    check("snooze survived", r.snooze_minutes == 15);
    check("volume survived", r.alarm_volume == 60);

    printf("\nsecond save leaves a .bak\n");
    check("save succeeds", json_save_settings(&r, path));
    check(".bak now exists", exists(bak));

    printf("\ncorrupt file does not clobber live state\n");
    write_raw(path, "{ \"dark_mode\": true, \"alarm_cou");
    AppState live; defaults(&live);
    live.alarms[0].hour = 6; live.alarms[0].minute = 45; live.alarms[0].enabled = TRUE;
    lstrcpyW(live.alarms[0].label, L"keepme");
    check("returns SETTINGS_CORRUPT", json_load_settings(&live, path) == SETTINGS_CORRUPT);
    check("live alarm hour untouched", live.alarms[0].hour == 6);
    check("live alarm minute untouched", live.alarms[0].minute == 45);
    check("live alarm label untouched", lstrcmpW(live.alarms[0].label, L"keepme") == 0);
    check("live alarm still enabled", live.alarms[0].enabled == TRUE);

    printf("\nthe .bak from before is still loadable\n");
    AppState fromBak; defaults(&fromBak);
    check("bak loads", json_load_settings(&fromBak, bak) == SETTINGS_OK);
    check("bak has the 07:30 alarm", fromBak.alarms[0].hour == 7 && fromBak.alarms[0].minute == 30);

    printf("\nBOM and out-of-range values\n");
    write_raw(path, "\xEF\xBB\xBF{ \"snooze_minutes\": 10, \"alarms\": [ "
                    "{\"hour\": 99, \"minute\": 5, \"enabled\": true, \"label\": \"bad\", \"repeat_days\": 0} ] }");
    AppState b; defaults(&b);
    check("file with BOM parses", json_load_settings(&b, path) == SETTINGS_OK);
    check("value after BOM read", b.snooze_minutes == 10);
    check("hour 99 rejected as unset", b.alarms[0].hour == ALARM_UNSET);
    check("rejected alarm is disabled", b.alarms[0].enabled == FALSE);

    printf("\nmore alarms in file than MAX_ALARMS\n");
    {
        char big[4096] = "{ \"snooze_minutes\": 7, \"alarms\": [";
        for (int i = 0; i < MAX_ALARMS + 4; i++) {
            char row[160];
            sprintf(row, "%s{\"hour\": %d, \"minute\": 0, \"enabled\": false, \"label\": \"\", \"repeat_days\": 0}",
                    i ? "," : "", i % 24);
            strcat(big, row);
        }
        strcat(big, "] }");
        write_raw(path, big);
    }
    AppState o; defaults(&o);
    check("over-long array still parses", json_load_settings(&o, path) == SETTINGS_OK);
    check("key after the array was read", o.snooze_minutes == 7);

    printf("\n%s (%d failing)\n\n", fails ? "FAILED" : "all passed", fails);
    return fails ? 1 : 0;
}
