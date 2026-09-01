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

    printf("\nunknown keys are skipped, not fatal\n");
    write_raw(path,
        "{ \"dark_mode\": true, \"future_flag\": true, \"future_num\": 4.5e2,"
        " \"future_str\": \"has } and ] and \\\" inside\","
        " \"future_obj\": {\"a\": [1, {\"b\": null}], \"c\": \"x\"},"
        " \"future_null\": null, \"snooze_minutes\": 20,"
        " \"alarms\": [ {\"hour\": 6, \"minute\": 15, \"enabled\": true,"
        " \"label\": \"gym\", \"repeat_days\": 3, \"future_per_alarm\": {\"z\": 1}} ] }");
    AppState u; defaults(&u);
    check("file with unknown keys still loads", json_load_settings(&u, path) == SETTINGS_OK);
    check("known key before the unknowns read", u.dark_mode == TRUE);
    check("known key after the unknowns read", u.snooze_minutes == 20);
    check("alarm past an unknown per-alarm key read", u.alarms[0].hour == 6 && u.alarms[0].minute == 15);
    check("alarm label past unknown key read", lstrcmpW(u.alarms[0].label, L"gym") == 0);
    check("alarm repeat mask past unknown key read", u.alarms[0].repeat_days == 3);

    printf("\na known key with an unexpected type is skipped, not fatal\n");
    write_raw(path, "{ \"dark_mode\": 1, \"snooze_minutes\": \"ten\", \"alarm_volume\": 40 }");
    AppState ty; defaults(&ty); ty.snooze_minutes = 3;
    check("mistyped values do not condemn the file", json_load_settings(&ty, path) == SETTINGS_OK);
    check("mistyped bool left at its previous value", ty.dark_mode == FALSE);
    check("mistyped int left at its previous value", ty.snooze_minutes == 3);
    check("the well-typed key after them was read", ty.alarm_volume == 40);

    printf("\nover-long strings truncate instead of derailing the parse\n");
    write_raw(path,
        "{ \"alarms\": [ {\"hour\": 8, \"minute\": 0, \"enabled\": true,"
        " \"label\": \"0123456789012345678901234567890123456789 far past 31 chars\","
        " \"repeat_days\": 0} ], \"snooze_minutes\": 9 }");
    AppState lg; defaults(&lg);
    check("over-long label still parses", json_load_settings(&lg, path) == SETTINGS_OK);
    check("label truncated to the field width", lstrlenW(lg.alarms[0].label) == 31);
    check("key after the long label was read", lg.snooze_minutes == 9);
    check("alarm around the long label survived", lg.alarms[0].hour == 8);

    printf("\ncontrol characters survive the round trip\n");
    {
        AppState c1; defaults(&c1);
        c1.alarms[0].hour = 5; c1.alarms[0].minute = 5; c1.alarms[0].enabled = TRUE;
        WCHAR lbl[8]; lbl[0] = L'a'; lbl[1] = 1; lbl[2] = L'b'; lbl[3] = 0;
        lstrcpyW(c1.alarms[0].label, lbl);
        check("save with a control char succeeds", json_save_settings(&c1, path));
        AppState c2; defaults(&c2);
        check("reload succeeds", json_load_settings(&c2, path) == SETTINGS_OK);
        /* The writer emits \u0001; before the reader understood \u it came back
           as the literal text "u0001". */
        check("control char round-tripped, not literal 'u0001'",
              lstrcmpW(c2.alarms[0].label, lbl) == 0);
    }

    printf("\nabsurd numbers saturate instead of wrapping\n");
    write_raw(path, "{ \"cd_hours\": 999999999999, \"cd_mins\": 5, \"cd_secs\": 0 }");
    AppState big; defaults(&big);
    check("huge number parses", json_load_settings(&big, path) == SETTINGS_OK);
    check("cd_hours clamped to CD_MAX_HOURS", big.cd_hours == CD_MAX_HOURS);
    check("countdown total stays positive", cd_total_ms(&big) > 0);
    check("countdown total is the clamped length",
          cd_total_ms(&big) == (CD_MAX_HOURS * 3600 + 5 * 60) * 1000);

    printf("\n%s (%d failing)\n\n", fails ? "FAILED" : "all passed", fails);
    return fails ? 1 : 0;
}
