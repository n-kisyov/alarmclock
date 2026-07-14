#include "alarm_dialog.h"
#include "main.h"
#include "theme.h"

static const TCHAR *repeat_items[] = {
    L"Once", L"Daily", L"Weekdays", L"Weekends"
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

        {
            HWND hCombo = GetDlgItem(hDlg, IDC_ALARM_REPEAT);
            for (int i = 0; i < 4; i++) {
                SendMessageW(hCombo, CB_ADDSTRING, 0, (LPARAM)repeat_items[i]);
            }
            SendMessageW(hCombo, CB_SETCURSEL, data->repeat_mode, 0);
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

    case WM_MEASUREITEM: {
        MEASUREITEMSTRUCT *mis = (MEASUREITEMSTRUCT *)lp;
        if (mis->CtlType == ODT_COMBOBOX) {
            mis->itemHeight = 20;
            return TRUE;
        }
        break;
    }

    case WM_DRAWITEM: {
        DRAWITEMSTRUCT *dis = (DRAWITEMSTRUCT *)lp;
        if (dis->CtlType != ODT_COMBOBOX) break;

        TCHAR buf[32];
        if (dis->itemID == (UINT)-1 ||
            SendMessageW(dis->hwndItem, CB_GETLBTEXT, dis->itemID, (LPARAM)buf) == CB_ERR) {
            buf[0] = 0;
        }

        BOOL isField = (dis->itemState & ODS_COMBOBOXEDIT) != 0;
        AppState *s = &g_state;

        COLORREF bg, fg;
        if (dis->itemState & ODS_SELECTED && !isField) {
            bg = s->accentColor;
            fg = RGB(255, 255, 255);
        } else {
            bg = isField ? s->panelBgColor : s->bgColor;
            fg = s->textColor;
        }

        HBRUSH hBr = CreateSolidBrush(bg);
        FillRect(dis->hDC, &dis->rcItem, hBr);
        DeleteObject(hBr);

        SetBkMode(dis->hDC, TRANSPARENT);
        SetTextColor(dis->hDC, fg);

        RECT rc = dis->rcItem;
        rc.left += 4;
        DrawTextW(dis->hDC, buf, -1, &rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        if ((dis->itemState & ODS_FOCUS) && !isField) {
            DrawFocusRect(dis->hDC, &dis->rcItem);
        }

        return TRUE;
    }

    case WM_COMMAND:
        switch (LOWORD(wp)) {
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

            data->hour    = h;
            data->minute  = m;
            data->enabled = (IsDlgButtonChecked(hDlg, IDC_ALARM_ENABLED) == BST_CHECKED);

            {
                HWND hCombo = GetDlgItem(hDlg, IDC_ALARM_REPEAT);
                int sel = (int)SendMessageW(hCombo, CB_GETCURSEL, 0, 0);
                if (sel >= 0) data->repeat_mode = sel;
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
