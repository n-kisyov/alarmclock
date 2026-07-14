#ifndef ALARM_DIALOG_H
#define ALARM_DIALOG_H

#include <windows.h>

typedef struct {
    int   hour;
    int   minute;
    BOOL  enabled;
    WCHAR label[32];
    int   repeat_mode;
} AlarmEditData;

INT_PTR CALLBACK alarm_dlg_proc(HWND hDlg, UINT msg, WPARAM wp, LPARAM lp);

#endif
