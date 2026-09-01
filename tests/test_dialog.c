/* Shows the alarm dialog modelessly so its layout can actually be looked at,
   and reports whether every control the code talks to really exists. Not part
   of the -Test run: it puts a window on screen for a few seconds. */
#include <windows.h>
#include <stdio.h>
#include "main.h"
#include "alarm_dialog.h"
#include "settings_dialog.h"
#include "theme.h"

AppState g_state;

/* The settings dialog reaches for this on OK; the harness never presses OK. */
void autostart_update(AppState *s) { (void)s; }

static int show_settings(HINSTANCE hInst) {
    HWND dlg = CreateDialogParamW(hInst, MAKEINTRESOURCEW(IDD_SETTINGS), NULL,
                                  settings_dlg_proc, (LPARAM)&g_state);
    if (!dlg) {
        printf("FAIL: settings dialog did not create (error %lu)\n", GetLastError());
        return 1;
    }
    ShowWindow(dlg, SW_SHOW);
    UpdateWindow(dlg);

    int fails = 0;
    if (!GetDlgItem(dlg, IDC_SLEEP_MINUTES)) {
        printf("  FAIL: sleep timer control missing\n");
        fails++;
    }
    /* The dialog grew; make sure OK is still inside it. */
    RECT dr, ok;
    GetClientRect(dlg, &dr);
    GetWindowRect(GetDlgItem(dlg, IDOK), &ok);
    MapWindowPoints(NULL, dlg, (POINT *)&ok, 2);
    printf("  client %ld x %ld, OK button bottom at %ld\n",
           dr.right, dr.bottom, ok.bottom);
    if (ok.bottom > dr.bottom) { printf("  FAIL: OK button is clipped\n"); fails++; }

    DWORD end = GetTickCount() + 9000;
    MSG msg;
    while (GetTickCount() < end) {
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (!IsDialogMessageW(dlg, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
        }
        Sleep(10);
    }
    printf("%s\n", fails ? "FAILED" : "ok");
    return fails;
}

int main(int argc, char **argv) {
    HINSTANCE hInst = GetModuleHandleW(NULL);
    INITCOMMONCONTROLSEX icc;
    icc.dwSize = sizeof(icc);
    icc.dwICC  = ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    ZeroMemory(&g_state, sizeof(g_state));
    g_state.dark_mode     = TRUE;
    g_state.alarm_count   = 5;
    g_state.alarm_volume  = 80;
    g_state.snooze_minutes = 3;
    g_state.sleep_minutes = 45;
    theme_update_colors(&g_state);

    if (argc > 1 && strcmp(argv[1], "settings") == 0) return show_settings(hInst);

    AlarmEditData d;
    ZeroMemory(&d, sizeof(d));
    d.hour = 7; d.minute = 30; d.enabled = TRUE;
    d.repeat_days = 0x3E;
    lstrcpyW(d.label, L"Work shift");
    lstrcpyW(d.sound, L"C:\\music\\wake up.flac");
    d.volume = 40;
    d.snooze_minutes = 20;
    d.skip_next = TRUE;

    HWND dlg = CreateDialogParamW(hInst, MAKEINTRESOURCEW(IDD_ALARM), NULL,
                                  alarm_dlg_proc, (LPARAM)&d);
    if (!dlg) {
        printf("FAIL: dialog did not create (error %lu)\n", GetLastError());
        return 1;
    }
    ShowWindow(dlg, SW_SHOW);
    UpdateWindow(dlg);

    int fails = 0;
    static const struct { int id; const char *name; } ctrls[] = {
        { IDC_ALARM_LABEL,       "label"        },
        { IDC_ALARM_HOUR,        "hour"         },
        { IDC_ALARM_MINUTE,      "minute"       },
        { IDC_ALARM_ENABLED,     "enabled"      },
        { IDC_ALARM_SKIP,        "skip next"    },
        { IDC_ALARM_SOUND,       "sound path"   },
        { IDC_ALARM_SOUND_PICK,  "browse"       },
        { IDC_ALARM_SOUND_CLEAR, "clear"        },
        { IDC_ALARM_VOL,         "volume"       },
        { IDC_ALARM_SNOOZE,      "snooze"       },
        { IDOK,                  "OK"           },
        { IDCANCEL,              "Cancel"       },
    };
    for (size_t i = 0; i < ARRAYSIZE(ctrls); i++) {
        if (!GetDlgItem(dlg, ctrls[i].id)) {
            printf("  FAIL: control missing: %s (%d)\n", ctrls[i].name, ctrls[i].id);
            fails++;
        }
    }
    if (!fails) printf("  every control present\n");

    /* Default plus the ten volume steps, with 40%% preselected (index 4). */
    int volCount = (int)SendDlgItemMessageW(dlg, IDC_ALARM_VOL, CB_GETCOUNT, 0, 0);
    int volSel   = (int)SendDlgItemMessageW(dlg, IDC_ALARM_VOL, CB_GETCURSEL, 0, 0);
    printf("  volume combo: %d items, selection %d (want 11 / 4)\n", volCount, volSel);
    if (volCount != 11 || volSel != 4) fails++;

    /* Default plus eight snooze steps, with 20 min preselected (index 7). */
    int snCount = (int)SendDlgItemMessageW(dlg, IDC_ALARM_SNOOZE, CB_GETCOUNT, 0, 0);
    int snSel   = (int)SendDlgItemMessageW(dlg, IDC_ALARM_SNOOZE, CB_GETCURSEL, 0, 0);
    printf("  snooze combo: %d items, selection %d (want 9 / 7)\n", snCount, snSel);
    if (snCount != 9 || snSel != 7) fails++;

    if (IsDlgButtonChecked(dlg, IDC_ALARM_SKIP) != BST_CHECKED) {
        printf("  FAIL: skip_next did not reach the checkbox\n");
        fails++;
    }

    RECT r; GetWindowRect(dlg, &r);
    printf("  dialog is %ld x %ld px\n", r.right - r.left, r.bottom - r.top);

    /* Stay up long enough to be captured from outside. */
    DWORD end = GetTickCount() + 9000;
    MSG msg;
    while (GetTickCount() < end) {
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (!IsDialogMessageW(dlg, &msg)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
        Sleep(10);
    }
    printf("%s\n", fails ? "FAILED" : "ok");
    return fails ? 1 : 0;
}
