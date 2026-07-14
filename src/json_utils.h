#ifndef JSON_UTILS_H
#define JSON_UTILS_H

#include <windows.h>
#include "main.h"

BOOL json_load_settings(AppState *s, const TCHAR *path);
BOOL json_save_settings(const AppState *s, const TCHAR *path);

#endif
