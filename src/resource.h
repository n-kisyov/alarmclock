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

#define IDD_ALARM            300
#define IDC_ALARM_HOUR       301
#define IDC_ALARM_MINUTE     302
#define IDC_ALARM_ENABLED    303
#define IDC_ALARM_LABEL      304
#define IDC_ALARM_REPEAT     305

#define REPEAT_ONCE          0
#define REPEAT_DAILY         1
#define REPEAT_WEEKDAYS      2
#define REPEAT_WEEKENDS      3

#define IDM_SETTINGS         1001
#define IDM_EXIT             1002
#define IDM_ABOUT            1003

#define IDM_TRAY_SHOW        2001
#define IDM_TRAY_EXIT        2002

#define WM_TRAYICON          (WM_APP + 1)
#define TIMER_CLOCK          1

#endif
