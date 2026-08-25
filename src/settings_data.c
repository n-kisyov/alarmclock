#include "settings_data.h"
#include "json_utils.h"
#include "main.h"
#include <strsafe.h>

static BOOL settings_path(const AppState *s, TCHAR *out, size_t cch) {
    return SUCCEEDED(StringCchPrintfW(out, cch, L"%s\\alarmclock_settings.json", s->exe_dir));
}

BOOL settings_load(AppState *s) {
    TCHAR path[MAX_PATH];
    if (!settings_path(s, path, MAX_PATH)) return FALSE;

    SettingsLoadResult res = json_load_settings(s, path);
    if (res == SETTINGS_OK) return TRUE;

    /* No file yet is the normal first run, not something to report. */
    if (res == SETTINGS_MISSING) return FALSE;

    /* The file exists but could not be read. json_save_settings leaves the
       previous copy as .bak on every swap, so try that before giving up. */
    TCHAR bak[MAX_PATH];
    if (SUCCEEDED(StringCchPrintfW(bak, MAX_PATH, L"%s.bak", path)) &&
        json_load_settings(s, bak) == SETTINGS_OK) {
        MessageBoxW(NULL,
            L"alarmclock_settings.json could not be read, so your settings "
            L"were restored from the automatic backup.\n\n"
            L"Any changes made since the last successful save are lost.",
            L"AlarmClock", MB_OK | MB_ICONWARNING);
        return TRUE;
    }

    MessageBoxW(NULL,
        L"alarmclock_settings.json could not be read and no usable backup was "
        L"found, so AlarmClock has started with default settings.\n\n"
        L"Your alarms will be saved again when you close the app.",
        L"AlarmClock", MB_OK | MB_ICONWARNING);
    return FALSE;
}

BOOL settings_save(const AppState *s) {
    TCHAR path[MAX_PATH];
    if (!settings_path(s, path, MAX_PATH)) return FALSE;
    return json_save_settings(s, path);
}
