/* Exercises the real playback path: device open, Media Foundation decode, the
   render loop and the natural end-of-track signal.

   Gain is held at zero throughout, so this verifies the whole pipeline without
   actually making a noise on whoever's machine is running it. */
#include <windows.h>
#include <stdio.h>
#include "audio.h"
#include "main.h"
#include "sound.h"

static int fails = 0;
static void check(const char *what, int cond) {
    printf("  %-58s %s\n", what, cond ? "ok" : "FAIL");
    if (!cond) fails++;
}

/* Something short, present on every Windows install, and not an mp3 - the point
   being that the old MCI path could only ever open mp3. */
static const WCHAR *find_sample(void) {
    static const WCHAR *candidates[] = {
        L"C:\\Windows\\Media\\Alarm01.wav",
        L"C:\\Windows\\Media\\notify.wav",
        L"C:\\Windows\\Media\\ding.wav",
        L"C:\\Windows\\Media\\chimes.wav",
    };
    for (int i = 0; i < 4; i++) {
        if (GetFileAttributesW(candidates[i]) != INVALID_FILE_ATTRIBUTES)
            return candidates[i];
    }
    return NULL;
}

int main(void) {
    printf("\naudio engine\n");

    check("audio_init succeeds", audio_init());
    audio_set_gain(0.0f);
    check("gain reads back", audio_get_gain() == 0.0f);
    check("nothing playing yet", !audio_is_playing());

    printf("\ngenerated tone\n");
    check("tone starts", audio_play_tone(NULL));
    Sleep(700);
    check("tone still running after 700ms (device opened)", audio_is_playing());
    audio_stop();
    check("tone stops", !audio_is_playing());

    printf("\ndecoded file\n");
    const WCHAR *sample = find_sample();
    if (!sample) {
        printf("  (no sample .wav found on this machine - skipped)\n");
    } else {
        wprintf(L"  using %s\n", sample);
        check("wav starts (Media Foundation decoded it)", audio_play_file(sample, NULL));
        Sleep(400);
        check("wav still running after 400ms", audio_is_playing());

        /* Every one of these clips is well under 15s, so a natural end must
           arrive on its own without anyone calling audio_stop. */
        int waited = 0;
        while (audio_is_playing() && waited < 15000) { Sleep(100); waited += 100; }
        check("wav reached its own end, unaided", !audio_is_playing());
        printf("  (played out in %d ms)\n", waited);
    }

    printf("\na file that is not audio fails cleanly\n");
    check("missing file refused", !audio_play_file(L"C:\\nope\\not-here.mp3", NULL));
    check("empty path refused", !audio_play_file(L"", NULL));
    check("still not playing", !audio_is_playing());

    printf("\ngain ramp\n");
    audio_set_gain(0.0f);
    audio_ramp_gain(0.0f, 1.0f, 400);
    check("ramp is active once started", audio_ramp_active());
    Sleep(600);
    /* The ramp is evaluated by the render thread, which is idle here, so just
       confirm a plain set cancels it. */
    audio_set_gain(0.25f);
    check("a direct set cancels the ramp", !audio_ramp_active());
    check("and takes the new value", audio_get_gain() == 0.25f);
    audio_ramp_gain(0.5f, 0.9f, 0);
    check("a zero-length ramp applies its target at once", audio_get_gain() == 0.9f);

    /* ---- sound.c policy, driven over the real engine ---- */

    printf("\nplaylist and fallback\n");
    {
        WCHAR base[MAX_PATH], songs[MAX_PATH], f[MAX_PATH];
        GetTempPathW(MAX_PATH, base);
        lstrcatW(base, L"acsound_test");
        CreateDirectoryW(base, NULL);
        wsprintfW(songs, L"%s\\songs", base);
        CreateDirectoryW(songs, NULL);

        /* Clear anything a previous run left. */
        WIN32_FIND_DATAW fd;
        wsprintfW(f, L"%s\\*", songs);
        HANDLE hf = FindFirstFileW(f, &fd);
        if (hf != INVALID_HANDLE_VALUE) {
            do {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                    wsprintfW(f, L"%s\\%s", songs, fd.cFileName);
                    DeleteFileW(f);
                }
            } while (FindNextFileW(hf, &fd));
            FindClose(hf);
        }

        AppState s;
        ZeroMemory(&s, sizeof(s));
        lstrcpynW(s.exe_dir, base, MAX_PATH);
        s.sound_mode   = SOUND_MP3;
        s.alarm_volume = 0;          /* silent for the whole section */
        s.alarm_active = TRUE;
        s.hMainWnd     = NULL;

        sound_play_alarm(&s);
        check("empty songs folder still wakes you, via the tone", audio_is_playing());
        check("gain follows alarm_volume", audio_get_gain() == 0.0f);
        sound_stop_alarm(&s);
        check("stop really stops", !audio_is_playing());

        wsprintfW(f, L"%s\\notes.txt", songs);
        HANDLE h = CreateFileW(f, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
        if (h != INVALID_HANDLE_VALUE) { DWORD w; WriteFile(h, "x", 1, &w, NULL); CloseHandle(h); }
        sound_play_alarm(&s);
        check("a folder of non-audio files falls back too", audio_is_playing());
        sound_stop_alarm(&s);

        if (sample) {
            wsprintfW(f, L"%s\\clip.wav", songs);
            CopyFileW(sample, f, FALSE);

            s.crescendo = TRUE;
            sound_play_alarm(&s);
            check("a .wav in songs plays - MCI could only ever open .mp3",
                  audio_is_playing());
            check("crescendo arms a gain ramp", audio_ramp_active());
            sound_stop_alarm(&s);
            check("stop cancels the ramp", !audio_ramp_active());

            s.crescendo = FALSE;
            s.sound_mode = SOUND_SIMPLE;
            sound_play_alarm(&s);
            check("simple mode ignores the folder and uses the tone", audio_is_playing());
            sound_stop_alarm(&s);
        }

        sound_cleanup(&s);
    }

    audio_shutdown();

    printf("\n%s (%d failing)\n\n", fails ? "FAILED" : "all passed", fails);
    return fails ? 1 : 0;
}
