#include "alarms.h"
#include "main.h"

void alarms_init(AppState *s) {
    for (int i = 0; i < MAX_ALARMS; i++) {
        s->alarms[i].hour        = ALARM_UNSET;
        s->alarms[i].minute      = ALARM_UNSET;
        s->alarms[i].enabled     = FALSE;
        s->alarms[i].label[0]    = 0;
        s->alarms[i].repeat_mode = REPEAT_ONCE;
    }
}

BOOL alarms_check(AppState *s, const SYSTEMTIME *st) {

    if (!s->alarms_enabled || s->alarm_active) return FALSE;

    int nowMin = (int)st->wHour * 60 + (int)st->wMinute;
    if (nowMin == s->last_fire_min) return FALSE;

    for (int i = 0; i < s->alarm_count; i++) {
        if (!s->alarms[i].enabled ||
            s->alarms[i].hour != (int)st->wHour ||
            s->alarms[i].minute != (int)st->wMinute ||
            s->alarms[i].hour == ALARM_UNSET)
            continue;

        int rm = s->alarms[i].repeat_mode;
        if (rm != REPEAT_ONCE && rm != REPEAT_DAILY) {
            int dow = st->wDayOfWeek;
            if (rm == REPEAT_WEEKDAYS && (dow == 0 || dow == 6)) continue;
            if (rm == REPEAT_WEEKENDS  && (dow >= 1 && dow <= 5)) continue;
        }

        if (rm == REPEAT_ONCE) s->alarms[i].enabled = FALSE;

        s->alarm_active = TRUE;
        s->last_fire_min = nowMin;
        return TRUE;
    }
    return FALSE;
}
