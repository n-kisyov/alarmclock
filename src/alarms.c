#include "alarms.h"
#include "main.h"
#include "settings_data.h"

void alarms_init(AppState *s) {
    for (int i = 0; i < MAX_ALARMS; i++) {
        s->alarms[i].hour        = ALARM_UNSET;
        s->alarms[i].minute      = ALARM_UNSET;
        s->alarms[i].enabled     = FALSE;
        s->alarms[i].label[0]    = 0;
        s->alarms[i].repeat_days = 0;
    }
}

BOOL alarms_check(AppState *s, const SYSTEMTIME *st) {

    if (!s->alarms_enabled || s->alarm_active) return FALSE;

    int nowMin = (int)st->wHour * 60 + (int)st->wMinute;
    if (nowMin == s->last_fire_min) return FALSE;

    /* Every slot, not just the displayed ones: alarm_count is a display
       preference, and lowering it used to strand enabled alarms as invisible
       and permanently silent. */
    for (int i = 0; i < MAX_ALARMS; i++) {
        if (!s->alarms[i].enabled ||
            s->alarms[i].hour != (int)st->wHour ||
            s->alarms[i].minute != (int)st->wMinute ||
            s->alarms[i].hour == ALARM_UNSET)
            continue;

        if (s->alarms[i].repeat_days != 0) {
            int dayBit = 1 << st->wDayOfWeek;
            if (!(s->alarms[i].repeat_days & dayBit)) continue;
        } else {
            /* A one-shot alarm disarms itself. Persist that now rather than
               waiting for the save on exit, so killing the process does not
               leave it armed for tomorrow. */
            s->alarms[i].enabled = FALSE;
            settings_save(s);
        }

        s->alarm_active = TRUE;
        s->last_fire_min = nowMin;
        return TRUE;
    }
    return FALSE;
}
