#include "sound.h"
#include "main.h"
#include <stdio.h>
#include <stdlib.h>

static TCHAR  **g_mp3_paths = NULL;
static int     g_mp3_count  = 0;
static int     g_mp3_index  = 0;

static void shuffle_mp3s(void) {
    for (int i = g_mp3_count - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        TCHAR *tmp = g_mp3_paths[i];
        g_mp3_paths[i] = g_mp3_paths[j];
        g_mp3_paths[j] = tmp;
    }
    g_mp3_index = 0;
}

static BOOL find_mp3_files(AppState *s) {
    if (g_mp3_paths) {
        for (int i = 0; i < g_mp3_count; i++) free(g_mp3_paths[i]);
        free(g_mp3_paths);
        g_mp3_paths = NULL;
    }
    g_mp3_count = 0;

    TCHAR search[MAX_PATH];
    wsprintf(search, L"%s\\songs\\*.mp3", s->exe_dir);

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
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            g_mp3_paths[idx] = (TCHAR *)malloc(MAX_PATH * sizeof(TCHAR));
            wsprintf(g_mp3_paths[idx], L"%s\\songs\\%s", s->exe_dir, fd.cFileName);
            idx++;
        }
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);

    shuffle_mp3s();
    return TRUE;
}

static void play_mp3_next(AppState *s) {
    mciSendStringW(L"close alarm_mp3", NULL, 0, NULL);

    if (g_mp3_count == 0) return;

    if (g_mp3_index >= g_mp3_count) {
        shuffle_mp3s();
    }

    TCHAR cmd[1024];
    wsprintf(cmd, L"open \"%s\" type mpegvideo alias alarm_mp3", g_mp3_paths[g_mp3_index]);
    if (mciSendStringW(cmd, NULL, 0, NULL) == 0) {
        if (s->crescendo) {
            TCHAR vol[64];
            wsprintf(vol, L"setaudio alarm_mp3 volume to %d", 100);
            mciSendStringW(vol, NULL, 0, NULL);
        }
        mciSendStringW(L"play alarm_mp3 notify", NULL, 0, s->hMainWnd);
        g_mp3_index++;
    }
}

static DWORD WINAPI crescendo_thread(LPVOID param) {
    AppState *s = (AppState *)param;

    for (int step = 0; step < 15 && !s->stop_sound; step++) {
        int vol = 100 + (1000 - 100) * step / 14;
        if (vol > 1000) vol = 1000;
        WCHAR cmd[64];
        wsprintfW(cmd, L"setaudio alarm_mp3 volume to %d", vol);
        mciSendStringW(cmd, NULL, 0, NULL);
        Sleep(1000);
    }
    return 0;
}

static DWORD WINAPI sound_simple_thread(LPVOID param) {
    AppState *s = (AppState *)param;

    int phases[15][4] = {
        {200,0,200,0}, {200,0,200,0}, {250,80,250,0},
        {300,80,300,0}, {350,80,350,0}, {400,80,400,0},
        {450,80,450,0}, {500,80,500,0}, {550,80,550,500},
        {600,80,600,500}, {700,80,700,500}, {800,80,800,500},
        {900,80,900,500}, {1000,80,1000,500}, {1100,80,1100,500}
    };

    int step = 0;
    while (!s->stop_sound && step < 15) {
        if (s->crescendo) {
            Beep(600 + step * 40, phases[step][0]);
            if (!s->stop_sound) Sleep(phases[step][1]);
            if (!s->stop_sound) Beep(800 + step * 30, phases[step][2]);
            if (!s->stop_sound) Sleep(phases[step][3]);
        } else {
            Beep(1000, 200); if (s->stop_sound) break;
            Sleep(80);
            Beep(1200, 200); if (s->stop_sound) break;
            Sleep(500);
        }
        step++;
    }

    if (!s->crescendo) {
        while (!s->stop_sound) {
            Beep(1000, 200); if (s->stop_sound) break;
            Sleep(80);
            Beep(1200, 200); if (s->stop_sound) break;
            Sleep(500);
        }
    }
    s->stop_sound = FALSE;
    return 0;
}

void sound_play_alarm(AppState *s) {

    if (s->sound_mode == SOUND_MP3) {
        if (find_mp3_files(s)) {
            play_mp3_next(s);
            if (s->crescendo) {
                s->hCrescendoThread = CreateThread(NULL, 0, crescendo_thread, s, 0, NULL);
            }
            return;
        }
    }

    s->stop_sound = FALSE;
    s->hSoundThread = CreateThread(NULL, 0, sound_simple_thread, s, 0, NULL);
}

void sound_stop_alarm(AppState *s) {

    s->stop_sound = TRUE;

    if (s->hSoundThread) {
        WaitForSingleObject(s->hSoundThread, 3000);
        CloseHandle(s->hSoundThread);
        s->hSoundThread = NULL;
    }
    if (s->hCrescendoThread) {
        WaitForSingleObject(s->hCrescendoThread, 3000);
        CloseHandle(s->hCrescendoThread);
        s->hCrescendoThread = NULL;
    }

    mciSendStringW(L"close alarm_mp3", NULL, 0, NULL);

    if (g_mp3_paths) {
        for (int i = 0; i < g_mp3_count; i++) free(g_mp3_paths[i]);
        free(g_mp3_paths);
        g_mp3_paths = NULL;
    }
    g_mp3_count = 0;
}

void sound_on_mci_notify(AppState *s) {
    if (s->alarm_active && s->sound_mode == SOUND_MP3) {
        play_mp3_next(s);
    }
}
