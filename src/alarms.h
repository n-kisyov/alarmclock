#ifndef ALARMS_H
#define ALARMS_H

#include <windows.h>
#include "main.h"

void alarms_init(AppState *s);
BOOL alarms_check(AppState *s, const SYSTEMTIME *st, int *out_index);
BOOL alarms_next_delta_minutes(const SYSTEMTIME *st, const Alarm *a, int *delta_minutes);

#endif
