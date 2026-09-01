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
        s->alarms[i].sound[0]    = 0;
        s->alarms[i].volume         = -1;   /* -1: follow the global setting */
        s->alarms[i].snooze_minutes = -1;
        s->alarms[i].skip_next      = FALSE;
    }
}

/* Minutes from now until this alarm next comes round, or FALSE if it never will.
   Lived as a static in tray.c; the tooltip and the wake timer both need it, and
   two copies of a schedule calculation is one too many. */
BOOL alarms_next_delta_minutes(const SYSTEMTIME *st, const Alarm *a, int *delta_minutes) {
    if (!a->enabled || a->hour == ALARM_UNSET || a->minute == ALARM_UNSET) return FALSE;

    int now_min   = (int)st->wHour * 60 + (int)st->wMinute;
    int alarm_min = a->hour * 60 + a->minute;

    if (a->repeat_days == 0) {
        if (alarm_min <= now_min) return FALSE;
        *delta_minutes = alarm_min - now_min;
        return TRUE;
    }

    /* Runs to 7, not 6. Today is skipped once its time has passed, so a weekly
       alarm on this very weekday had no candidate left in a 0..6 sweep and was
       reported as never coming round again - a Wednesday-only alarm vanished
       from the tooltip every Wednesday after it rang. Offset 7 is the same
       weekday, one week out. */
    for (int day_offset = 0; day_offset <= 7; day_offset++) {
        int day = ((int)st->wDayOfWeek + day_offset) % 7;
        if (!(a->repeat_days & (1 << day))) continue;
        if (day_offset == 0 && alarm_min <= now_min) continue;
        *delta_minutes = day_offset * 24 * 60 + (alarm_min - now_min);
        return TRUE;
    }
    return FALSE;
}

/* Whole minutes since the FILETIME epoch, so "the same minute" means the same
   minute of the same day rather than the same time on any day. */
ULONGLONG alarms_minute_stamp(const SYSTEMTIME *st) {
    FILETIME ft;
    if (!SystemTimeToFileTime(st, &ft)) return 0;
    ULARGE_INTEGER u;
    u.LowPart  = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return u.QuadPart / 600000000ULL;      /* 100ns ticks in one minute */
}

BOOL alarms_check(AppState *s, const SYSTEMTIME *st, int *out_index) {
    if (out_index) *out_index = -1;

    if (!s->alarms_enabled || s->alarm_active) return FALSE;

    /* Zero means "no fire recorded", and also what a failed conversion returns,
       so it never suppresses anything. */
    ULONGLONG stamp = alarms_minute_stamp(st);
    if (stamp != 0 && stamp == s->last_fire_stamp) return FALSE;

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
        }

        if (s->alarms[i].skip_next) {
            /* Spend the skip on this occurrence and clear it. The minute is
               stamped exactly as a real fire would stamp it, because otherwise
               the flag is gone and the very next tick inside this same minute
               would ring the alarm we were asked to skip. */
            s->alarms[i].skip_next = FALSE;
            if (s->alarms[i].repeat_days == 0) s->alarms[i].enabled = FALSE;
            s->last_fire_stamp = stamp;
            settings_save(s);
            return FALSE;
        }

        if (s->alarms[i].repeat_days == 0) {
            /* A one-shot alarm disarms itself. Persist that now rather than
               waiting for the save on exit, so killing the process does not
               leave it armed for tomorrow. */
            s->alarms[i].enabled = FALSE;
            settings_save(s);
        }

        s->alarm_active = TRUE;
        s->last_fire_stamp = stamp;
        if (out_index) *out_index = i;
        return TRUE;
    }
    return FALSE;
}
