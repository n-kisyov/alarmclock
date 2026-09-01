#include "alarm_dialog.h"
#include "main.h"
#include "theme.h"
#include <commdlg.h>

static const int dayIds[7] = {
    IDC_DAY_SUN, IDC_DAY_MON, IDC_DAY_TUE, IDC_DAY_WED,
    IDC_DAY_THU, IDC_DAY_FRI, IDC_DAY_SAT
};

/* The same vocabulary the Settings dialog offers, behind a Default entry, so an
   override reads as a deliberate departure from the global value. */
static const int volValues[]    = { 10, 20, 30, 40, 50, 60, 70, 80, 90, 100 };
static const int snoozeValues[] = { 1, 2, 3, 5, 10, 15, 20, 30 };

static void fill_override_combo(HWND hDlg, int id, const int *values, int count,
                                int current, const WCHAR *suffix) {
    HWND h = GetDlgItem(hDlg, id);
    SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)L"Default");

    int sel = 0;
    for (int i = 0; i < count; i++) {
        WCHAR buf[16];
        wsprintfW(buf, L"%d%s", values[i], suffix);
        SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)buf);
        if (values[i] == current) sel = i + 1;
    }
    SendMessageW(h, CB_SETCURSEL, sel, 0);
}

/* -1 for the Default entry, which is how "follow the global setting" is stored. */
static int read_override_combo(HWND hDlg, int id, const int *values, int count) {
    int sel = (int)SendDlgItemMessageW(hDlg, id, CB_GETCURSEL, 0, 0);
    if (sel <= 0 || sel > count) return -1;
    return values[sel - 1];
}

static void browse_for_sound(HWND hDlg, AlarmEditData *data) {
    WCHAR file[MAX_PATH];
    lstrcpynW(file, data->sound, MAX_PATH);

    OPENFILENAMEW ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = hDlg;
    ofn.lpstrFilter = L"Audio files\0*.mp3;*.wav;*.flac;*.m4a;*.wma;*.aac;*.mp4\0"
                      L"All files\0*.*\0";
    ofn.lpstrFile   = file;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrTitle  = L"Sound for this alarm";
    /* NOCHANGEDIR matters: the picker would otherwise move the process working
       directory, and the songs folder is resolved relative to the exe. */
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER | OFN_NOCHANGEDIR;

    if (GetOpenFileNameW(&ofn)) {
        lstrcpynW(data->sound, file, MAX_PATH);
        SetDlgItemTextW(hDlg, IDC_ALARM_SOUND, data->sound);
    }
}

INT_PTR CALLBACK alarm_dlg_proc(HWND hDlg, UINT msg, WPARAM wp, LPARAM lp) {
    /* On the window rather than in a function-level static, for the same reason
       the settings and countdown dialogs moved off one. */
    AlarmEditData *data = (AlarmEditData *)GetWindowLongPtrW(hDlg, GWLP_USERDATA);

    switch (msg) {
    case WM_INITDIALOG: {
        data = (AlarmEditData *)lp;
        SetWindowLongPtrW(hDlg, GWLP_USERDATA, (LONG_PTR)data);

        AppState *s = &g_state;
        theme_dialog_init(hDlg, s);

        if (data->label[0])
            SetDlgItemTextW(hDlg, IDC_ALARM_LABEL, data->label);

        TCHAR buf[16];
        if (data->hour >= 0)
            wsprintf(buf, L"%d", data->hour);
        else
            buf[0] = 0;
        SetDlgItemText(hDlg, IDC_ALARM_HOUR, buf);

        if (data->minute >= 0)
            wsprintf(buf, L"%d", data->minute);
        else
            buf[0] = 0;
        SetDlgItemText(hDlg, IDC_ALARM_MINUTE, buf);

        for (int i = 0; i < 7; i++) {
            CheckDlgButton(hDlg, dayIds[i],
                (data->repeat_days & (1 << i)) ? BST_CHECKED : BST_UNCHECKED);
        }

        CheckDlgButton(hDlg, IDC_ALARM_ENABLED, data->enabled ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_ALARM_SKIP, data->skip_next ? BST_CHECKED : BST_UNCHECKED);

        SetDlgItemTextW(hDlg, IDC_ALARM_SOUND, data->sound);
        fill_override_combo(hDlg, IDC_ALARM_VOL, volValues, ARRAYSIZE(volValues),
                            data->volume, L"%");
        fill_override_combo(hDlg, IDC_ALARM_SNOOZE, snoozeValues, ARRAYSIZE(snoozeValues),
                            data->snooze_minutes, L" min");
        return TRUE;
    }

    case WM_MEASUREITEM: {
        MEASUREITEMSTRUCT *mis = (MEASUREITEMSTRUCT *)lp;
        if (mis->CtlType == ODT_COMBOBOX) { mis->itemHeight = 20; return TRUE; }
        break;
    }

    case WM_DRAWITEM:
        if (theme_draw_combo_item(&g_state, (DRAWITEMSTRUCT *)lp)) return TRUE;
        break;

    case WM_CTLCOLORSTATIC:
        theme_dialog_colors(hDlg, &g_state, (HWND)lp, (HDC)wp);
        return (INT_PTR)g_state.hBgBrush;

    case WM_CTLCOLORBTN:
        theme_dialog_colors(hDlg, &g_state, (HWND)lp, (HDC)wp);
        return (INT_PTR)g_state.hBgBrush;

    case WM_CTLCOLOREDIT:
        SetTextColor((HDC)wp, g_state.textColor);
        SetBkColor((HDC)wp, g_state.panelBgColor);
        return (INT_PTR)g_state.hPanelBrush;

    case WM_CTLCOLORDLG:
        return (INT_PTR)g_state.hBgBrush;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_ALARM_HOUR:
            if (HIWORD(wp) == EN_KILLFOCUS) {
                TCHAR b[16];
                GetDlgItemText(hDlg, IDC_ALARM_HOUR, b, 16);
                int v = _wtoi(b);
                if (v > 23) SetDlgItemText(hDlg, IDC_ALARM_HOUR, L"23");
            }
            break;
        case IDC_ALARM_MINUTE:
            if (HIWORD(wp) == EN_KILLFOCUS) {
                TCHAR b[16];
                GetDlgItemText(hDlg, IDC_ALARM_MINUTE, b, 16);
                int v = _wtoi(b);
                if (v > 59) SetDlgItemText(hDlg, IDC_ALARM_MINUTE, L"59");
            }
            break;
        case IDC_DAY_ALL:
            for (int i = 0; i < 7; i++)
                CheckDlgButton(hDlg, dayIds[i], BST_CHECKED);
            return TRUE;

        case IDC_DAY_NONE:
            for (int i = 0; i < 7; i++)
                CheckDlgButton(hDlg, dayIds[i], BST_UNCHECKED);
            return TRUE;

        case IDC_ALARM_SOUND_PICK:
            if (data) browse_for_sound(hDlg, data);
            return TRUE;

        case IDC_ALARM_SOUND_CLEAR:
            if (data) {
                data->sound[0] = 0;
                SetDlgItemTextW(hDlg, IDC_ALARM_SOUND, L"");
            }
            return TRUE;

        case IDOK: {
            if (!data) return TRUE;
            GetDlgItemTextW(hDlg, IDC_ALARM_LABEL, data->label,
                            ARRAYSIZE(data->label));

            TCHAR hbuf[16], mbuf[16];
            GetDlgItemText(hDlg, IDC_ALARM_HOUR, hbuf, ARRAYSIZE(hbuf));
            GetDlgItemText(hDlg, IDC_ALARM_MINUTE, mbuf, ARRAYSIZE(mbuf));

            /* An empty field is not zero. _wtoi("") returns 0, which passed the
               range check below and quietly turned "I opened Edit and pressed
               OK" into a real 00:00 alarm. */
            if (hbuf[0] == 0 || mbuf[0] == 0) {
                MessageBoxW(hDlg, L"Enter both an hour (0-23) and a minute (0-59).",
                            L"Invalid Time", MB_OK | MB_ICONWARNING);
                return TRUE;
            }

            int h = _wtoi(hbuf);
            int m = _wtoi(mbuf);

            if (h < 0 || h > 23 || m < 0 || m > 59) {
                MessageBoxW(hDlg, L"Please enter valid hour (0-23) and minute (0-59).",
                            L"Invalid Time", MB_OK | MB_ICONWARNING);
                return TRUE;
            }

            data->hour   = h;
            data->minute = m;
            data->enabled   = (IsDlgButtonChecked(hDlg, IDC_ALARM_ENABLED) == BST_CHECKED);
            data->skip_next = (IsDlgButtonChecked(hDlg, IDC_ALARM_SKIP) == BST_CHECKED);

            data->repeat_days = 0;
            for (int i = 0; i < 7; i++) {
                if (IsDlgButtonChecked(hDlg, dayIds[i]) == BST_CHECKED)
                    data->repeat_days |= (1 << i);
            }

            data->volume = read_override_combo(hDlg, IDC_ALARM_VOL,
                                               volValues, ARRAYSIZE(volValues));
            data->snooze_minutes = read_override_combo(hDlg, IDC_ALARM_SNOOZE,
                                               snoozeValues, ARRAYSIZE(snoozeValues));

            EndDialog(hDlg, IDOK);
            return TRUE;
        }
        case IDCANCEL:
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    return FALSE;
}
