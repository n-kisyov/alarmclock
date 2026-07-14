#ifndef TRAY_H
#define TRAY_H

#include <windows.h>
#include "main.h"

void tray_create(HWND hwnd, AppState *s);
void tray_remove(AppState *s);
void tray_show_menu(HWND hwnd, AppState *s);

#endif
