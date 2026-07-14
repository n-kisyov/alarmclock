#ifndef SETTINGS_DATA_H
#define SETTINGS_DATA_H

#include <windows.h>
#include "main.h"

BOOL settings_load(AppState *s);
BOOL settings_save(const AppState *s);

#endif
