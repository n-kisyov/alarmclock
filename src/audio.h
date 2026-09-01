#ifndef AUDIO_H
#define AUDIO_H

#include <windows.h>

/* Playback through Media Foundation (decode) and WASAPI (render), with gain
   applied to the float buffer on the way out.

   The single software gain is the point of the exercise: MCI could only set a
   volume on an mp3, and Beep() could not be given one at all, so the volume
   setting silently did nothing in tone mode. One knob now serves the volume
   setting, the crescendo, the sleep-timer fade and per-alarm volume, and the
   format list widens to whatever Media Foundation has a decoder for. */

BOOL  audio_init(void);            /* once per process, before anything below */
void  audio_shutdown(void);

/* Decodes and plays one file. notifyWnd receives WM_AUDIO_TRACK_DONE when the
   track reaches its end on its own (never when audio_stop cut it short). */
BOOL  audio_play_file(const WCHAR *path, HWND notifyWnd);

/* The built-in two-tone alarm, generated rather than decoded so it travels the
   same gain path as a file. Repeats until stopped. */
BOOL  audio_play_tone(HWND notifyWnd);

void  audio_stop(void);
BOOL  audio_is_playing(void);

/* 0.0 - 1.0. Safe from any thread; takes effect on the next buffer. */
void  audio_set_gain(float gain);
float audio_get_gain(void);

/* Linear ramp, evaluated in the render thread. ms of 0 applies `to` at once. */
void  audio_ramp_gain(float from, float to, DWORD ms);
BOOL  audio_ramp_active(void);

#endif
