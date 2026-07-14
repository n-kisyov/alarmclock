#include "alarms.h"
#include "main.h"

void alarms_init(AppState *s) {
    for (int i = 0; i < MAX_ALARMS; i++) {
        s->alarms[i].hour    = ALARM_UNSET;
        s->alarms[i].minute  = ALARM_UNSET;
        s->alarms[i].enabled = FALSE;
    }
}

BOOL alarms_check(AppState *s, const SYSTEMTIME *st) {

    if (!s->alarms_enabled || s->alarm_active) return FALSE;

    for (int i = 0; i < s->alarm_count; i++) {
        if (s->alarms[i].enabled &&
            s->alarms[i].hour == (int)st->wHour &&
            s->alarms[i].minute == (int)st->wMinute &&
            s->alarms[i].hour != ALARM_UNSET) {

            s->alarms[i].enabled = FALSE;
            s->alarm_active = TRUE;
            return TRUE;
        }
    }
    return FALSE;
}
