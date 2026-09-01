#ifndef POWER_H
#define POWER_H

#include <windows.h>
#include "main.h"

/* Waking the machine for an alarm, and keeping it awake while one rings.

   Without this, alarms_check only ever sees the minutes the process is actually
   running, so anything scheduled while the PC is asleep simply never happens -
   which is a poor showing for an alarm clock. */

/* Arms a wake timer for the next alarm. Cheap, and safe to call whenever the
   schedule might have moved. */
BOOL power_arm_wake_timer(AppState *s);
void power_cancel_wake_timer(void);

/* Seconds from `now` until the wake timer should fire, or -1 when there is
   nothing worth waking for. Split out from the arming so the arithmetic can be
   checked without sleeping a machine to find out. */
LONGLONG power_seconds_until_wake(const AppState *s, const SYSTEMTIME *now);

/* FALSE when Windows itself has wake timers switched off, in which case the
   timer is armed and will never fire - worth saying out loud. */
BOOL power_wake_timers_allowed(void);

/* Holds off sleep and the screen blanking while an alarm is sounding. */
void power_keep_awake(BOOL awake);

void power_cleanup(void);

#endif
