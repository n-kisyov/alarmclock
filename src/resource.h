#ifndef RESOURCE_H
#define RESOURCE_H

#define IDI_APPICON          101
#define IDR_DIGITALFONT      102

#define IDR_APP_MANIFEST     1

#define IDD_SETTINGS         200
#define IDC_DARKMODE         201
#define IDC_CLOCK_DIGITAL    202
#define IDC_CLOCK_ANALOG     203
#define IDC_ALARMS_ENABLED   204
#define IDC_SOUND_SIMPLE     205
#define IDC_SOUND_MP3        206
#define IDC_ALARM_COUNT      207
#define IDC_HOUR24           208
#define IDC_SNOOZE_MINUTES   209
#define IDC_CRESCENDO        210
#define IDC_AUTOSTART        211
#define IDC_START_MINIMIZED  212
#define IDC_ACRYLIC          213
#define IDC_ALWAYS_ON_TOP    214
#define IDC_ALARM_VOLUME     215
#define IDC_PREVIEW_SOUND    216

#define IDD_ALARM            300
#define IDC_ALARM_HOUR       301
#define IDC_ALARM_MINUTE     302
#define IDC_ALARM_ENABLED    303
#define IDC_ALARM_LABEL      304
#define IDC_ALARM_REPEAT     305
#define IDC_DAY_SUN          310
#define IDC_DAY_MON          311
#define IDC_DAY_TUE          312
#define IDC_DAY_WED          313
#define IDC_DAY_THU          314
#define IDC_DAY_FRI          315
#define IDC_DAY_SAT          316
#define IDC_DAY_ALL          317
#define IDC_DAY_NONE         318

#define IDD_COUNTDOWN_SET    400
#define IDC_CD_HOURS         401
#define IDC_CD_MINS          402
#define IDC_CD_SECS          403

#define APP_MODE_CLOCK       0
#define APP_MODE_COUNTDOWN   1
#define APP_MODE_STOPWATCH   2

#define IDM_SETTINGS         1001
#define IDM_EXIT             1002
#define IDM_ABOUT            1003

#define IDM_TRAY_SHOW        2001
#define IDM_TRAY_EXIT        2002

#define WM_TRAYICON          (WM_APP + 1)
#define TIMER_CLOCK          1

#endif
