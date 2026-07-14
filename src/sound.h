#ifndef SOUND_H
#define SOUND_H

#include <windows.h>
#include "main.h"

void sound_play_alarm(AppState *s);
void sound_stop_alarm(AppState *s);
void sound_on_mci_notify(AppState *s);

#endif
