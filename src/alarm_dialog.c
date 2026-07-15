#include "alarm_dialog.h"
#include "main.h"
#include "theme.h"

static const int dayIds[7] = {
    IDC_DAY_SUN, IDC_DAY_MON, IDC_DAY_TUE, IDC_DAY_WED,
    IDC_DAY_THU, IDC_DAY_FRI, IDC_DAY_SAT
};

INT_PTR CALLBACK alarm_dlg_proc(HWND hDlg, UINT msg, WPARAM wp, LPARAM lp) {
    static AlarmEditData *data;

    switch (msg) {
    case WM_INITDIALOG: {
        data = (AlarmEditData *)lp;

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

        return TRUE;
    }

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
        case IDC_DAY_ALL:
            for (int i = 0; i < 7; i++)
                CheckDlgButton(hDlg, dayIds[i], BST_CHECKED);
            return TRUE;

        case IDC_DAY_NONE:
            for (int i = 0; i < 7; i++)
                CheckDlgButton(hDlg, dayIds[i], BST_UNCHECKED);
            return TRUE;

        case IDOK: {
            TCHAR buf[64];
            GetDlgItemTextW(hDlg, IDC_ALARM_LABEL, data->label, 31);
            data->label[31] = 0;

            GetDlgItemText(hDlg, IDC_ALARM_HOUR, buf, 16);
            int h = _wtoi(buf);
            GetDlgItemText(hDlg, IDC_ALARM_MINUTE, buf, 16);
            int m = _wtoi(buf);

            if (h < 0 || h > 23 || m < 0 || m > 59) {
                MessageBoxW(hDlg, L"Please enter valid hour (0-23) and minute (0-59).",
                            L"Invalid Time", MB_OK | MB_ICONWARNING);
                return TRUE;
            }

            data->hour   = h;
            data->minute = m;
            data->enabled = (IsDlgButtonChecked(hDlg, IDC_ALARM_ENABLED) == BST_CHECKED);

            data->repeat_days = 0;
            for (int i = 0; i < 7; i++) {
                if (IsDlgButtonChecked(hDlg, dayIds[i]) == BST_CHECKED)
                    data->repeat_days |= (1 << i);
            }

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
