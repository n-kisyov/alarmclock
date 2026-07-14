#include "settings_data.h"
#include "json_utils.h"
#include "main.h"

BOOL settings_load(AppState *s) {
    TCHAR path[MAX_PATH];
    wsprintf(path, L"%s\\alarmclock_settings.json", s->exe_dir);
    return json_load_settings(s, path);
}

BOOL settings_save(const AppState *s) {
    TCHAR path[MAX_PATH];
    wsprintf(path, L"%s\\alarmclock_settings.json", s->exe_dir);
    return json_save_settings(s, path);
}
