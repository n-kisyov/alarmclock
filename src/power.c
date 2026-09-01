#include "power.h"
#include "main.h"
#include <powrprof.h>
#include <limits.h>

/* winnt.h declares these but only gives them storage under INITGUID, which
   audio.c already claims for this link - so they are spelled out here rather
   than fought over. */
static const GUID kSleepSubgroup =
    { 0x238c9fa8, 0x0aad, 0x41ed, { 0x83, 0xf4, 0x97, 0xbe, 0x24, 0x2c, 0x8f, 0x20 } };
static const GUID kAllowRtcWake =
    { 0xbd3b718a, 0x0680, 0x4d9d, { 0x8a, 0xb2, 0xe1, 0xd2, 0xb4, 0xac, 0x80, 0x6d } };

static HANDLE g_wakeTimer = NULL;

/* Resuming is not instant, so wake a little ahead of the minute: the alarm
   should be sounding at the minute, not starting to think about it. */
#define WAKE_LEAD_SECONDS 20

static HANDLE ensure_wake_timer(void) {
    if (g_wakeTimer) return g_wakeTimer;

    g_wakeTimer = CreateWaitableTimerExW(NULL, NULL,
        CREATE_WAITABLE_TIMER_MANUAL_RESET | CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
        TIMER_ALL_ACCESS);
    if (!g_wakeTimer)
        g_wakeTimer = CreateWaitableTimerW(NULL, TRUE, NULL);
    return g_wakeTimer;
}

LONGLONG power_seconds_until_wake(const AppState *s, const SYSTEMTIME *now) {
    if (!s->alarms_enabled) return -1;

    int best = INT_MAX;
    for (int i = 0; i < MAX_ALARMS; i++) {
        int delta;
        if (alarms_next_delta_minutes(now, &s->alarms[i], &delta) && delta < best)
            best = delta;
    }
    if (best == INT_MAX) return -1;

    LONGLONG secs = (LONGLONG)best * 60LL - WAKE_LEAD_SECONDS;
    if (secs < 1) secs = 1;                 /* never inside a second */
    return secs;
}

BOOL power_arm_wake_timer(AppState *s) {
    HANDLE h = ensure_wake_timer();
    if (!h) return FALSE;

    SYSTEMTIME now;
    GetLocalTime(&now);

    LONGLONG secs = power_seconds_until_wake(s, &now);
    if (secs < 0) { CancelWaitableTimer(h); return FALSE; }

    LARGE_INTEGER li;
    li.QuadPart = -(secs * 10000000LL);     /* negative is relative to now */

    /* The last argument is the whole point: fResume asks the system to come out
       of sleep for this. Nothing waits on the handle - once the machine is
       running again the ordinary tick notices the alarm by itself. */
    return SetWaitableTimer(h, &li, 0, NULL, NULL, TRUE);
}

void power_cancel_wake_timer(void) {
    if (g_wakeTimer) CancelWaitableTimer(g_wakeTimer);
}

BOOL power_wake_timers_allowed(void) {
    GUID *scheme = NULL;
    DWORD value = 1;              /* if it cannot be read, assume the best */

    if (PowerGetActiveScheme(NULL, &scheme) == ERROR_SUCCESS && scheme) {
        DWORD v = 0;
        if (PowerReadACValueIndex(NULL, scheme, &kSleepSubgroup, &kAllowRtcWake, &v)
                == ERROR_SUCCESS)
            value = v;
        LocalFree(scheme);
    }
    return value != 0;
}

void power_keep_awake(BOOL awake) {
    /* Per-thread, so this belongs on the UI thread - which is where alarms
       start and stop. */
    SetThreadExecutionState(awake
        ? (ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED)
        : ES_CONTINUOUS);
}

void power_cleanup(void) {
    power_keep_awake(FALSE);
    if (g_wakeTimer) {
        CancelWaitableTimer(g_wakeTimer);
        CloseHandle(g_wakeTimer);
        g_wakeTimer = NULL;
    }
}
