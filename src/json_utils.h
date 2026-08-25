#ifndef JSON_UTILS_H
#define JSON_UTILS_H

#include <windows.h>
#include "main.h"

typedef enum {
    SETTINGS_OK = 0,   /* parsed cleanly; *s has been updated */
    SETTINGS_MISSING,  /* no file yet - first run, not an error */
    SETTINGS_CORRUPT   /* file present but unreadable; *s left untouched */
} SettingsLoadResult;

SettingsLoadResult json_load_settings(AppState *s, const TCHAR *path);
BOOL json_save_settings(const AppState *s, const TCHAR *path);

#endif
