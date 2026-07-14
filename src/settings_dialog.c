#include "settings_dialog.h"
#include "main.h"
#include "theme.h"

static const TCHAR *snooze_items[] = {
    L"1", L"2", L"3", L"5", L"10", L"15", L"20", L"30"
};
static const int snooze_values[] = {1, 2, 3, 5, 10, 15, 20, 30};
static const int snooze_count = 8;

INT_PTR CALLBACK settings_dlg_proc(HWND hDlg, UINT msg, WPARAM wp, LPARAM lp) {
    static AppState *s;

    switch (msg) {
    case WM_INITDIALOG: {
        s = (AppState *)lp;

        theme_dialog_init(hDlg, s);

        CheckDlgButton(hDlg, IDC_DARKMODE,       s->dark_mode ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_ALARMS_ENABLED, s->alarms_enabled ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_HOUR24,         s->hour24 ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_CRESCENDO,      s->crescendo ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_AUTOSTART,      s->autostart ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_START_MINIMIZED,s->start_minimized ? BST_CHECKED : BST_UNCHECKED);
        CheckDlgButton(hDlg, IDC_ACRYLIC,        s->acrylic ? BST_CHECKED : BST_UNCHECKED);

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
                TCHAR buf[4];
                wsprintf(buf, L"%d", i);
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

        return TRUE;
    }

    case WM_CTLCOLORSTATIC:
        theme_dialog_colors(hDlg, s, (HWND)lp, (HDC)wp);
        return (INT_PTR)s->hBgBrush;

    case WM_CTLCOLORBTN:
        theme_dialog_colors(hDlg, s, (HWND)lp, (HDC)wp);
        return (INT_PTR)s->hBgBrush;

    case WM_CTLCOLOREDIT:
        SetTextColor((HDC)wp, s->textColor);
        SetBkColor((HDC)wp, s->panelBgColor);
        return (INT_PTR)s->hPanelBrush;

    case WM_CTLCOLORDLG:
        return (INT_PTR)s->hBgBrush;

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

        TCHAR buf[8];
        if (dis->itemID == (UINT)-1 ||
            SendMessageW(dis->hwndItem, CB_GETLBTEXT, dis->itemID, (LPARAM)buf) == CB_ERR) {
            buf[0] = 0;
        }

        BOOL isField = (dis->itemState & ODS_COMBOBOXEDIT) != 0;

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
            BOOL newDark = (IsDlgButtonChecked(hDlg, IDC_DARKMODE) == BST_CHECKED);
            BOOL darkChanged = (newDark != s->dark_mode);
            s->dark_mode = newDark;

            s->hour24          = (IsDlgButtonChecked(hDlg, IDC_HOUR24) == BST_CHECKED);
            s->crescendo       = (IsDlgButtonChecked(hDlg, IDC_CRESCENDO) == BST_CHECKED);
            BOOL newAutostart  = (IsDlgButtonChecked(hDlg, IDC_AUTOSTART) == BST_CHECKED);
            s->start_minimized = (IsDlgButtonChecked(hDlg, IDC_START_MINIMIZED) == BST_CHECKED);

            BOOL acrylicChanged = (IsDlgButtonChecked(hDlg, IDC_ACRYLIC) == BST_CHECKED) != s->acrylic;
            s->acrylic = (IsDlgButtonChecked(hDlg, IDC_ACRYLIC) == BST_CHECKED);

            int newStyle = (IsDlgButtonChecked(hDlg, IDC_CLOCK_ANALOG) == BST_CHECKED)
                ? CLOCK_ANALOG : CLOCK_DIGITAL;
            int styleChanged = (newStyle != s->clock_style);
            s->clock_style = newStyle;

            s->alarms_enabled = (IsDlgButtonChecked(hDlg, IDC_ALARMS_ENABLED) == BST_CHECKED);

            s->sound_mode = (IsDlgButtonChecked(hDlg, IDC_SOUND_MP3) == BST_CHECKED)
                ? SOUND_MP3 : SOUND_SIMPLE;

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

            if (newAutostart != s->autostart) {
                s->autostart = newAutostart;
                autostart_update(s);
            }

            theme_update_colors(s);

            if (darkChanged || acrylicChanged) theme_apply(s->hMainWnd, s->dark_mode);

            if (styleChanged) {
                int w = (newStyle == CLOCK_ANALOG) ? 500 : 720;
                int h = (newStyle == CLOCK_ANALOG) ? 710 : 520;
                SetWindowPos(s->hMainWnd, NULL, 0, 0, w, h,
                             SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
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
