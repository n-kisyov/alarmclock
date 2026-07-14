#ifndef CLOCK_RENDERER_H
#define CLOCK_RENDERER_H

#include <windows.h>
#include "main.h"

void clock_draw_digital(HDC hdc, const RECT *rc, const SYSTEMTIME *st, const AppState *s);
void clock_draw_analog(HDC hdc, const RECT *rc, const SYSTEMTIME *st, const AppState *s);

#endif
