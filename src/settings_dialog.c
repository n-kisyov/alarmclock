#include "settings_dialog.h"
#include "main.h"
#include "theme.h"
#include "sound.h"

static const TCHAR *snooze_items[] = {
    L"1", L"2", L"3", L"5", L"10", L"15", L"20", L"30"
};
static const int snooze_values[] = {1, 2, 3, 5, 10, 15, 20, 30};
static const int snooze_count = 8;

static const TCHAR *vol_items[] = {
    L"10%", L"20%", L"30%", L"40%", L"50%", L"60%", L"70%", L"80%", L"90%", L"100%"
};
static const int vol_values[] = {10,20,30,40,50,60,70,80,90,100};
static const int vol_count = 10;

static const TCHAR *sleep_items[] = { L"15", L"30", L"45", L"60", L"90" };
static const int sleep_values[] = {15, 30, 45, 60, 90};
static const int sleep_count = 5;

/* Kept on the window rather than in a function-level static: WM_CTLCOLOR* is
   not guaranteed to arrive after WM_INITDIALOG, and a static left the pointer
   NULL on the first message of the very first invocation. */
static AppState *dlg_state(HWND hDlg) {
    return (AppState *)GetWindowLongPtrW(hDlg, GWLP_USERDATA);
}

INT_PTR CALLBACK settings_dlg_proc(HWND hDlg, UINT msg, WPARAM wp, LPARAM lp) {
    AppState *s = dlg_state(hDlg);

    switch (msg) {
    case WM_INITDIALOG: {
        s = (AppState *)lp;
        SetWindowLongPtrW(hDlg, GWLP_USERDATA, (LONG_PTR)s);

        theme_dialog_init(hDlg, s);

        CheckDlgButton(hDlg, IDC_DARKMODE,       s->dark_mode ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_ALARMS_ENABLED, s->alarms_enabled ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_HOUR24,         s->hour24 ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_CRESCENDO,      s->crescendo ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_AUTOSTART,      s->autostart ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_START_MINIMIZED,s->start_minimized ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_ACRYLIC,        s->acrylic ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_ALWAYS_ON_TOP,  s->always_on_top ? BST_CHECKED : BST_UNCHECKED);

        if (s->clock_style == CLOCK_ANALOG)
            CheckDlgButton(hDlg, IDC_CLOCK_ANALOG, BST_CHECKED);
        else
            CheckDlgButton(hDlg, IDC_CLOCK_DIGITAL, BST_CHECKED);

        if (s->sound_mode == SOUND_MP3)
            CheckDlgButton(hDlg, IDC_SOUND_MP3, BST_CHECKED);
        else
            CheckDlgButton(hDlg, IDC_SOUND_SIMPLE, BST_CHECKED);

        {
            HWND hCombo = GetDlgItem(hDlg, IDC_ALARM_COUNT);
            for (int i = 1; i <= MAX_ALARMS; i++) {
                TCHAR buf[4]; wsprintf(buf, L"%d", i);
                SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)buf);
            }
            SendMessageW(hCombo, CB_SETCURSEL, s->alarm_count - 1, 0);
        }
        {
            HWND hCombo = GetDlgItem(hDlg, IDC_SNOOZE_MINUTES);
            int sel = 0;
            for (int i = 0; i < snooze_count; i++) {
                SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)snooze_items[i]);
                if (snooze_values[i] == s->snooze_minutes) sel = i;
            }
            SendMessageW(hCombo, CB_SETCURSEL, sel, 0);
        }
        {
            HWND hCombo = GetDlgItem(hDlg, IDC_ALARM_VOLUME);
            int sel = 0;
            for (int i = 0; i < vol_count; i++) {
                SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)vol_items[i]);
                if (vol_values[i] == s->alarm_volume) sel = i;
            }
            SendMessageW(hCombo, CB_SETCURSEL, sel, 0);
        }
        {
            HWND hCombo = GetDlgItem(hDlg, IDC_SLEEP_MINUTES);
            int sel = 1;                     /* 30 minutes */
            for (int i = 0; i < sleep_count; i++) {
                SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)sleep_items[i]);
                if (sleep_values[i] == s->sleep_minutes) sel = i;
            }
            SendMessageW(hCombo, CB_SETCURSEL, sel, 0);
        }

        return TRUE;
    }

    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
        if (!s) break;
        theme_dialog_colors(hDlg, s, (HWND)lp, (HDC)wp);
        return (INT_PTR)s->hBgBrush;

    case WM_CTLCOLOREDIT:
        if (!s) break;
        SetTextColor((HDC)wp, s->textColor);
        SetBkColor((HDC)wp, s->panelBgColor);
        return (INT_PTR)s->hPanelBrush;

    case WM_CTLCOLORDLG:
        if (!s) break;
        return (INT_PTR)s->hBgBrush;

    case WM_MEASUREITEM: {
        MEASUREITEMSTRUCT *mis = (MEASUREITEMSTRUCT *)lp;
        if (mis->CtlType == ODT_COMBOBOX) {
            mis->itemHeight = 20;
            return TRUE;
        }
        break;
    }

    case WM_DRAWITEM:
        if (theme_draw_combo_item(s, (DRAWITEMSTRUCT *)lp)) return TRUE;
        break;

    case WM_COMMAND:
        if (!s) break;
        switch (LOWORD(wp)) {
        case IDOK: {
            BOOL newDark = (IsDlgButtonChecked(hDlg, IDC_DARKMODE) == BST_CHECKED);
            BOOL darkChanged = (newDark != s->dark_mode);
            s->dark_mode = newDark;

            s->hour24          = (IsDlgButtonChecked(hDlg, IDC_HOUR24) == BST_CHECKED);
            s->crescendo       = (IsDlgButtonChecked(hDlg, IDC_CRESCENDO) == BST_CHECKED);
            BOOL newAutostart  = (IsDlgButtonChecked(hDlg, IDC_AUTOSTART) == BST_CHECKED);
            s->start_minimized = (IsDlgButtonChecked(hDlg, IDC_START_MINIMIZED) == BST_CHECKED);

            BOOL acrylicChanged = (IsDlgButtonChecked(hDlg, IDC_ACRYLIC) == BST_CHECKED) != s->acrylic;
            s->acrylic = (IsDlgButtonChecked(hDlg, IDC_ACRYLIC) == BST_CHECKED);

            BOOL topChanged = (IsDlgButtonChecked(hDlg, IDC_ALWAYS_ON_TOP) == BST_CHECKED) != s->always_on_top;
            s->always_on_top = (IsDlgButtonChecked(hDlg, IDC_ALWAYS_ON_TOP) == BST_CHECKED);

            int newStyle = (IsDlgButtonChecked(hDlg, IDC_CLOCK_ANALOG) == BST_CHECKED)
                ? CLOCK_ANALOG : CLOCK_DIGITAL;
            int styleChanged = (newStyle != s->clock_style);
            s->clock_style = newStyle;

            s->alarms_enabled = (IsDlgButtonChecked(hDlg, IDC_ALARMS_ENABLED) == BST_CHECKED);

            s->sound_mode = (IsDlgButtonChecked(hDlg, IDC_SOUND_MP3) == BST_CHECKED)
                ? SOUND_MP3 : SOUND_SIMPLE;

            /* Alarms in slots the panel no longer shows still ring, so say so
               rather than letting them fire from nowhere. */
            {
                HWND hCombo = GetDlgItem(hDlg, IDC_ALARM_COUNT);
                int sel = (int)SendMessageW(hCombo, CB_GETCURSEL, 0, 0);
                if (sel >= 0) {
                    for (int i = sel + 1; i < MAX_ALARMS; i++) {
                        if (s->alarms[i].enabled && s->alarms[i].hour != ALARM_UNSET) {
                            MessageBoxW(hDlg,
                                L"Some enabled alarms sit in slots that this many "
                                L"rows will not show. They will still ring.\n\n"
                                L"Raise the slot count to see them again.",
                                L"AlarmClock", MB_OK | MB_ICONINFORMATION);
                            break;
                        }
                    }
                }
            }

            {
                HWND hCombo = GetDlgItem(hDlg, IDC_ALARM_COUNT);
                int sel = (int)SendMessageW(hCombo, CB_GETCURSEL, 0, 0);
                if (sel >= 0) s->alarm_count = sel + 1;
            }
            {
                HWND hCombo = GetDlgItem(hDlg, IDC_SNOOZE_MINUTES);
                int sel = (int)SendMessageW(hCombo, CB_GETCURSEL, 0, 0);
                if (sel >= 0 && sel < snooze_count) s->snooze_minutes = snooze_values[sel];
            }
            {
                HWND hCombo = GetDlgItem(hDlg, IDC_ALARM_VOLUME);
                int sel = (int)SendMessageW(hCombo, CB_GETCURSEL, 0, 0);
                if (sel >= 0 && sel < vol_count) s->alarm_volume = vol_values[sel];
            }
            {
                HWND hCombo = GetDlgItem(hDlg, IDC_SLEEP_MINUTES);
                int sel = (int)SendMessageW(hCombo, CB_GETCURSEL, 0, 0);
                if (sel >= 0 && sel < sleep_count) s->sleep_minutes = sleep_values[sel];
            }

            if (newAutostart != s->autostart) {
                s->autostart = newAutostart;
                autostart_update(s);
            }

            theme_update_colors(s);

            if (darkChanged || acrylicChanged) theme_apply(s->hMainWnd, s->dark_mode);

            if (topChanged) {
                SetWindowPos(s->hMainWnd, s->always_on_top ? HWND_TOPMOST : HWND_NOTOPMOST,
                             0,0,0,0, SWP_NOMOVE|SWP_NOSIZE);
            }

            if (styleChanged) {
                /* Scaled to the window's dpi. At 150% the raw analog width of
                   500 fell below the minimum track size, so the resize was
                   quietly ignored and the dial stayed cramped. */
                int dpi = s->dpi ? s->dpi : 96;
                BOOL analog = (newStyle == CLOCK_ANALOG);
                int w = MulDiv(analog ? WIN_W_ANALOG : WIN_W_DIGITAL, dpi, 96);
                int h = MulDiv(analog ? WIN_H_ANALOG : WIN_H_DIGITAL, dpi, 96);
                SetWindowPos(s->hMainWnd, NULL, 0, 0, w, h,
                             SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            }

            EndDialog(hDlg, IDOK);
            /* Only the preview - this used to stop a genuinely ringing alarm
               while leaving alarm_active set, so the window kept offering
               SNOOZE and DISMISS over silence. */
            if (s->sound_preview) sound_stop_alarm(s);
            return TRUE;
        }
        case IDCANCEL:
            if (s->sound_preview) sound_stop_alarm(s);
            EndDialog(hDlg, IDCANCEL);
            return TRUE;

        case IDC_PREVIEW_SOUND: {
            if (s->alarm_active) return TRUE;   /* do not talk over a real alarm */

            /* One stop path, so repeated presses cannot leak the crescendo and
               preview thread handles by overwriting them. */
            sound_stop_alarm(s);

            /* Preview the radio button as it currently stands, without
               committing it - Cancel has to be able to undo the choice. */
            int pending = (IsDlgButtonChecked(hDlg, IDC_SOUND_MP3) == BST_CHECKED)
                ? SOUND_MP3 : SOUND_SIMPLE;
            int saved = s->sound_mode;
            s->sound_mode = pending;
            s->sound_preview = TRUE;
            sound_play_alarm(s);
            s->sound_mode = saved;
            return TRUE;
        }
        }
        break;
    }
    return FALSE;
}
