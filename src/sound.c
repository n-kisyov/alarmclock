#include "sound.h"
#include "main.h"
#include <stdio.h>
#include <stdlib.h>
#include <strsafe.h>

static TCHAR  **g_mp3_paths = NULL;
static int     g_mp3_count  = 0;
static int     g_mp3_index  = 0;

/* MCI takes volume on a 0-1000 scale; alarm_volume is a percentage. */
#define MCI_VOL_MAX       1000
#define CRESCENDO_FLOOR   100

/* Set once the ramp has finished (or been cut short) so that tracks started
   later by the MCI notify open at full volume instead of the ramp floor. */
static BOOL    g_crescendo_done = FALSE;

static int mp3_target_volume(const AppState *s) {
    int v = s->alarm_volume * (MCI_VOL_MAX / 100);
    if (v < 0) v = 0;
    if (v > MCI_VOL_MAX) v = MCI_VOL_MAX;
    return v;
}

static void shuffle_mp3s(void) {
    for (int i = g_mp3_count - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        TCHAR *tmp = g_mp3_paths[i];
        g_mp3_paths[i] = g_mp3_paths[j];
        g_mp3_paths[j] = tmp;
    }
    g_mp3_index = 0;
}

static void free_mp3_paths(void) {
    if (g_mp3_paths) {
        for (int i = 0; i < g_mp3_count; i++) {
            free(g_mp3_paths[i]);
        }
        free(g_mp3_paths);
        g_mp3_paths = NULL;
    }
    g_mp3_count = 0;
    g_mp3_index = 0;
}

static BOOL find_mp3_files(AppState *s) {
    free_mp3_paths();

    TCHAR search[MAX_PATH];
    if (FAILED(StringCchPrintfW(search, MAX_PATH, L"%s\\songs\\*.mp3", s->exe_dir)))
        return FALSE;

    WIN32_FIND_DATAW fd;
    HANDLE hFind = FindFirstFileW(search, &fd);
    if (hFind == INVALID_HANDLE_VALUE) return FALSE;

    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            g_mp3_count++;
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);

    if (g_mp3_count == 0) return FALSE;

    g_mp3_paths = (TCHAR **)malloc(sizeof(TCHAR *) * g_mp3_count);
    if (!g_mp3_paths) { g_mp3_count = 0; return FALSE; }

    int idx = 0;
    hFind = FindFirstFileW(search, &fd);
    if (hFind == INVALID_HANDLE_VALUE) {
        free_mp3_paths();
        return FALSE;
    }

    /* Bounded by the count from the first pass: a file appearing in the songs
       folder between the two scans would otherwise write past the allocation. */
    do {
        if (idx >= g_mp3_count) break;
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            g_mp3_paths[idx] = (TCHAR *)malloc(MAX_PATH * sizeof(TCHAR));
            if (!g_mp3_paths[idx]) {
                FindClose(hFind);
                free_mp3_paths();
                return FALSE;
            }
            if (FAILED(StringCchPrintfW(g_mp3_paths[idx], MAX_PATH,
                                        L"%s\\songs\\%s", s->exe_dir, fd.cFileName))) {
                free(g_mp3_paths[idx]);
                g_mp3_paths[idx] = NULL;
                continue;   /* path too long for this entry; skip it */
            }
            idx++;
        }
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);

    /* Fewer than the first pass counted, if a file was removed in between. */
    g_mp3_count = idx;
    if (g_mp3_count == 0) { free_mp3_paths(); return FALSE; }

    shuffle_mp3s();
    return TRUE;
}

/* Last seen write time of the songs folder, so the scan is repeated only when
   its contents have actually changed. */
static FILETIME g_songs_mtime;

/* The list and the shuffle cursor live across alarms now. find_mp3_files used to
   be re-run - and re-shuffled from index 0 - on every single alarm, so even a
   properly seeded rand() would have dealt a fresh deck each morning and always
   started it from the top. */
static BOOL ensure_mp3_files(AppState *s) {
    TCHAR dir[MAX_PATH];
    WIN32_FILE_ATTRIBUTE_DATA fad;
    BOOL haveStamp = FALSE;

    if (SUCCEEDED(StringCchPrintfW(dir, MAX_PATH, L"%s\\songs", s->exe_dir)) &&
        GetFileAttributesExW(dir, GetFileExInfoStandard, &fad)) {
        haveStamp = TRUE;
        if (g_mp3_count > 0 && CompareFileTime(&fad.ftLastWriteTime, &g_songs_mtime) == 0)
            return TRUE;
    }

    if (!find_mp3_files(s)) return FALSE;
    if (haveStamp) g_songs_mtime = fad.ftLastWriteTime;
    return TRUE;
}

static void set_mp3_volume(int mciVol) {
    WCHAR cmd[64];
    wsprintfW(cmd, L"setaudio alarm_mp3 volume to %d", mciVol);
    mciSendStringW(cmd, NULL, 0, NULL);
}

static void play_mp3_next(AppState *s) {
    mciSendStringW(L"close alarm_mp3", NULL, 0, NULL);

    if (g_mp3_count == 0) return;

    if (g_mp3_index >= g_mp3_count) {
        shuffle_mp3s();
    }

    TCHAR cmd[MAX_PATH + 64];
    if (FAILED(StringCchPrintfW(cmd, ARRAYSIZE(cmd),
                                L"open \"%s\" type mpegvideo alias alarm_mp3",
                                g_mp3_paths[g_mp3_index])))
        return;
    if (mciSendStringW(cmd, NULL, 0, NULL) == 0) {
        /* Only open at the ramp floor while a crescendo is actually still
           climbing. Once it has finished, later tracks start at full volume
           rather than being pinned at 10% for the rest of the alarm. */
        BOOL ramping = s->crescendo && !s->sound_preview && !g_crescendo_done;
        set_mp3_volume(ramping ? CRESCENDO_FLOOR : mp3_target_volume(s));
        mciSendStringW(L"play alarm_mp3 notify", NULL, 0, s->hMainWnd);
        g_mp3_index++;
    }
}

/* Returns TRUE if a stop arrived while waiting. Every Sleep in these threads
   went through here: the crescendo slept a whole second between checks, and a
   dismiss sat blocked on the UI thread for that long waiting to be noticed.
   Beep() itself still blocks for its own duration and cannot be interrupted. */
static BOOL sound_wait(AppState *s, DWORD ms) {
    if (s->hStopEvent)
        return WaitForSingleObject(s->hStopEvent, ms) == WAIT_OBJECT_0;
    Sleep(ms);
    return s->stop_sound != 0;
}

static DWORD WINAPI cresendo_thread(LPVOID param) {
    AppState *s = (AppState *)param;

    /* Ramps to the volume the user chose, not to 100%. */
    int target = mp3_target_volume(s);
    if (target < CRESCENDO_FLOOR) target = CRESCENDO_FLOOR;

    for (int step = 0; step < 15 && !s->stop_sound; step++) {
        set_mp3_volume(CRESCENDO_FLOOR + (target - CRESCENDO_FLOOR) * step / 14);
        if (sound_wait(s, 1000)) break;
    }
    if (!s->stop_sound) set_mp3_volume(target);
    g_crescendo_done = TRUE;
    return 0;
}

static DWORD WINAPI sound_preview_thread(LPVOID param) {
    AppState *s = (AppState *)param;
    if (sound_wait(s, 3000)) return 0;

    if (s->sound_preview && !s->stop_sound && s->hMainWnd) {
        PostMessageW(s->hMainWnd, WM_SOUND_PREVIEW_DONE, 0, 0);
    }
    return 0;
}

static DWORD WINAPI sound_simple_thread(LPVOID param) {
    AppState *s = (AppState *)param;

    if (s->sound_preview) {
        for (int i = 0; i < 6 && !s->stop_sound; i++) {
            Beep(1000, 200);
            if (sound_wait(s, 80)) return 0;
            Beep(1200, 200);
            if (sound_wait(s, 300)) return 0;
        }
        return 0;
    }

    if (s->crescendo) {
        for (int step = 0; step < 15 && !s->stop_sound; step++) {
            Beep(600 + step * 40, 200 + step * 20);
            if (sound_wait(s, 80)) return 0;
            Beep(800 + step * 30, 200 + step * 20);
            if (sound_wait(s, step < 8 ? 0 : 500)) return 0;
        }
    }

    while (!s->stop_sound) {
        Beep(1000, 200); if (s->stop_sound) break;
        if (sound_wait(s, 80)) break;
        Beep(1200, 200); if (s->stop_sound) break;
        if (sound_wait(s, 500)) break;
    }
    /* Deliberately does not clear stop_sound: only the stopper owns that flag.
       Clearing it here could cancel a stop aimed at the crescendo thread, or
       one aimed at a sound that has only just started. */
    return 0;
}

static void wait_and_close_thread(HANDLE *thread_handle) {
    if (!thread_handle || !*thread_handle) {
        return;
    }

    DWORD thread_id = GetThreadId(*thread_handle);
    if (thread_id != 0 && thread_id != GetCurrentThreadId()) {
        WaitForSingleObject(*thread_handle, 3000);
    }
    CloseHandle(*thread_handle);
    *thread_handle = NULL;
}

void sound_play_alarm(AppState *s) {
    /* Cleared here rather than in the simple-tone branch below: the MP3 branch
       returns before ever reaching that assignment, so a leftover TRUE from the
       previous stop made the crescendo and preview threads exit immediately. */
    if (!s->hStopEvent)
        s->hStopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);   /* manual reset */
    if (s->hStopEvent) ResetEvent(s->hStopEvent);
    InterlockedExchange(&s->stop_sound, FALSE);
    g_crescendo_done = FALSE;

    if (s->sound_mode == SOUND_MP3) {
        if (ensure_mp3_files(s)) {
            play_mp3_next(s);
            if (s->crescendo && !s->sound_preview) {
                s->hCrescendoThread = CreateThread(NULL, 0, cresendo_thread, s, 0, NULL);
            }
            if (s->sound_preview) {
                s->hPreviewThread = CreateThread(NULL, 0, sound_preview_thread, s, 0, NULL);
            }
            return;
        }
    }

    s->hSoundThread = CreateThread(NULL, 0, sound_simple_thread, s, 0, NULL);
    if (s->sound_preview) {
        s->hPreviewThread = CreateThread(NULL, 0, sound_preview_thread, s, 0, NULL);
    }
}

void sound_stop_alarm(AppState *s) {

    InterlockedExchange(&s->stop_sound, TRUE);
    if (s->hStopEvent) SetEvent(s->hStopEvent);
    s->sound_preview = FALSE;

    wait_and_close_thread(&s->hSoundThread);
    wait_and_close_thread(&s->hCrescendoThread);
    wait_and_close_thread(&s->hPreviewThread);

    mciSendStringW(L"close alarm_mp3", NULL, 0, NULL);
    /* The playlist deliberately survives a stop, so the next alarm carries on
       through the shuffle instead of restarting it. sound_cleanup frees it. */
}

void sound_on_mci_notify(AppState *s) {
    if (s->alarm_active && s->sound_mode == SOUND_MP3) {
        play_mp3_next(s);
    }
}

void sound_cleanup(AppState *s) {
    free_mp3_paths();
    ZeroMemory(&g_songs_mtime, sizeof(g_songs_mtime));
    if (s->hStopEvent) { CloseHandle(s->hStopEvent); s->hStopEvent = NULL; }
}
