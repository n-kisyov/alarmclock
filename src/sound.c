/* Alarm sound policy: which track, how loud, and when to stop. The playback
   itself lives in audio.c.

   This used to drive MCI's "mpegvideo" device directly, which meant mp3 and
   nothing else, and the simple-tone mode went through Beep() - which has no
   volume at all, so the volume setting silently did nothing there. Both now go
   through one render path with one software gain, which is also what makes the
   crescendo and the sleep-timer fade the same mechanism. */

#include "sound.h"
#include "audio.h"
#include "main.h"
#include <stdlib.h>
#include <strsafe.h>

static TCHAR  **g_tracks = NULL;
static int      g_count  = 0;
static int      g_index  = 0;

/* Last seen write time of the songs folder, so the scan repeats only when its
   contents have actually changed. */
static FILETIME g_songs_mtime;

/* When the crescendo is due to reach full volume, so a track change mid-ramp
   picks it up where it left off instead of restarting at the floor. */
static ULONGLONG g_crescendo_end = 0;

#define CRESCENDO_MS     15000
#define CRESCENDO_FLOOR  0.10f
#define PREVIEW_MS        3000

/* Media Foundation has decoders for all of these. MCI could only ever open the
   first one. */
static const WCHAR *kAudioExtensions[] = {
    L".mp3", L".wav", L".flac", L".m4a", L".wma", L".aac", L".mp4"
};

static BOOL has_audio_extension(const WCHAR *name) {
    const WCHAR *dot = NULL;
    for (const WCHAR *p = name; *p; p++) {
        if (*p == L'.') dot = p;
    }
    if (!dot) return FALSE;

    for (size_t i = 0; i < ARRAYSIZE(kAudioExtensions); i++) {
        if (lstrcmpiW(dot, kAudioExtensions[i]) == 0) return TRUE;
    }
    return FALSE;
}

static float target_gain(const AppState *s) {
    /* The ringing slot's own volume when it has one, the global setting
       otherwise. A preview has no slot, so it lands on the global. */
    int v = alarm_volume_for(s, s->ringing_alarm);
    if (v < 0)   v = 0;
    if (v > 100) v = 100;
    return (float)v / 100.0f;
}

/* ---------- the playlist ---------- */

static void shuffle_tracks(void) {
    for (int i = g_count - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        TCHAR *tmp = g_tracks[i];
        g_tracks[i] = g_tracks[j];
        g_tracks[j] = tmp;
    }
    g_index = 0;
}

static void free_tracks(void) {
    if (g_tracks) {
        for (int i = 0; i < g_count; i++) free(g_tracks[i]);
        free(g_tracks);
        g_tracks = NULL;
    }
    g_count = 0;
    g_index = 0;
}

static BOOL scan_tracks(AppState *s) {
    free_tracks();

    TCHAR search[MAX_PATH];
    if (FAILED(StringCchPrintfW(search, MAX_PATH, L"%s\\songs\\*", s->exe_dir)))
        return FALSE;

    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(search, &fd);
    if (hFind == INVALID_HANDLE_VALUE) return FALSE;

    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
            has_audio_extension(fd.cFileName))
            g_count++;
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);

    if (g_count == 0) return FALSE;

    g_tracks = (TCHAR **)malloc(sizeof(TCHAR *) * g_count);
    if (!g_tracks) { g_count = 0; return FALSE; }

    int idx = 0;
    hFind = FindFirstFileW(search, &fd);
    if (hFind == INVALID_HANDLE_VALUE) { free_tracks(); return FALSE; }

    /* Bounded by the first pass: a file appearing between the two scans would
       otherwise write past the allocation. */
    do {
        if (idx >= g_count) break;
        if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
            !has_audio_extension(fd.cFileName))
            continue;

        g_tracks[idx] = (TCHAR *)malloc(MAX_PATH * sizeof(TCHAR));
        if (!g_tracks[idx]) { FindClose(hFind); free_tracks(); return FALSE; }

        if (FAILED(StringCchPrintfW(g_tracks[idx], MAX_PATH,
                                    L"%s\\songs\\%s", s->exe_dir, fd.cFileName))) {
            free(g_tracks[idx]);
            g_tracks[idx] = NULL;
            continue;               /* path too long for this entry; skip it */
        }
        idx++;
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);

    /* Fewer than the first pass counted, if a file was removed in between. */
    g_count = idx;
    if (g_count == 0) { free_tracks(); return FALSE; }

    shuffle_tracks();
    return TRUE;
}

/* The list and the shuffle cursor live across alarms, so consecutive alarms
   walk the deck instead of re-cutting it. */
static BOOL ensure_tracks(AppState *s) {
    TCHAR dir[MAX_PATH];
    WIN32_FILE_ATTRIBUTE_DATA fad;
    BOOL haveStamp = FALSE;

    if (SUCCEEDED(StringCchPrintfW(dir, MAX_PATH, L"%s\\songs", s->exe_dir)) &&
        GetFileAttributesExW(dir, GetFileExInfoStandard, &fad)) {
        haveStamp = TRUE;
        if (g_count > 0 && CompareFileTime(&fad.ftLastWriteTime, &g_songs_mtime) == 0)
            return TRUE;
    }

    if (!scan_tracks(s)) return FALSE;
    if (haveStamp) g_songs_mtime = fad.ftLastWriteTime;
    return TRUE;
}

/* Walks the shuffle until something actually opens. A track that will not
   decode is skipped rather than being allowed to end the alarm in silence. */
static BOOL play_next_track(AppState *s) {
    for (int tried = 0; tried < g_count; tried++) {
        if (g_index >= g_count) shuffle_tracks();
        const WCHAR *path = g_tracks[g_index++];
        if (path && audio_play_file(path, s->hMainWnd)) return TRUE;
    }
    return FALSE;
}

/* ---------- public API ---------- */

void sound_play_alarm(AppState *s) {
    BOOL started = FALSE;
    const WCHAR *own = s->sound_preview ? NULL : alarm_sound_for(s, s->ringing_alarm);

    /* This alarm's own sound wins, but only if it still opens - a file that has
       been moved or deleted since it was chosen must not mean silence. */
    if (own) started = audio_play_file(own, s->hMainWnd);

    if (!started && s->sound_mode == SOUND_MP3 && ensure_tracks(s))
        started = play_next_track(s);

    /* An empty songs folder, or one where nothing will decode, still has to
       wake somebody up. */
    if (!started) started = audio_play_tone(s->hMainWnd);
    if (!started) return;

    float target = target_gain(s);
    if (s->crescendo && !s->sound_preview) {
        audio_ramp_gain(CRESCENDO_FLOOR * target, target, CRESCENDO_MS);
        g_crescendo_end = GetTickCount64() + CRESCENDO_MS;
    } else {
        audio_set_gain(target);
        g_crescendo_end = 0;
    }

    /* A timer on the main window, rather than a thread whose only job was to
       sleep and post one message. */
    if (s->sound_preview && s->hMainWnd)
        SetTimer(s->hMainWnd, TIMER_SOUND_PREVIEW, PREVIEW_MS, NULL);
}

void sound_stop_alarm(AppState *s) {
    if (s->hMainWnd) KillTimer(s->hMainWnd, TIMER_SOUND_PREVIEW);
    s->sound_preview = FALSE;
    g_crescendo_end  = 0;
    audio_stop();
}

void sound_on_track_done(AppState *s) {
    if (!s->alarm_active) return;

    const WCHAR *own = alarm_sound_for(s, s->ringing_alarm);
    float carried = audio_get_gain();

    /* A chosen file repeats; the shuffle moves on; the tone never ends by
       itself and so never gets here. */
    BOOL started;
    if (own)                            started = audio_play_file(own, s->hMainWnd);
    else if (s->sound_mode == SOUND_MP3) started = play_next_track(s);
    else                                 return;

    if (!started) return;

    /* Starting a track resets the gain state, so a crescendo is resumed for
       whatever is left of it rather than dropping back to the floor. */
    ULONGLONG now = GetTickCount64();
    if (g_crescendo_end > now)
        audio_ramp_gain(carried, target_gain(s), (DWORD)(g_crescendo_end - now));
    else
        audio_set_gain(target_gain(s));
}

void sound_cleanup(AppState *s) {
    (void)s;
    free_tracks();
    ZeroMemory(&g_songs_mtime, sizeof(g_songs_mtime));
}
