#ifndef ALARMS_H
#define ALARMS_H

#include <windows.h>
#include "main.h"

void alarms_init(AppState *s);
BOOL alarms_check(AppState *s, const SYSTEMTIME *st);

#endif
