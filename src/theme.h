#ifndef THEME_H
#define THEME_H

#include <windows.h>
#include "main.h"

void theme_apply(HWND hwnd, BOOL dark);
void theme_update_colors(AppState *s);
void theme_dialog_colors(HWND hDlg, AppState *s, HWND ctrl, HDC hdc);
void theme_dialog_init(HWND hDlg, AppState *s);

#endif
