/* Media Foundation decode -> software gain -> WASAPI shared-mode render.

   The C interface macros (COBJMACROS) and hand-rolled vtable calls match the
   flat GDI+ style already used in clock_renderer.c. initguid.h is included
   first so the interface and media-type GUIDs get storage here rather than
   needing a separate uuid library. */

#define COBJMACROS
#include <initguid.h>
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <math.h>
#include <stdlib.h>

#include "audio.h"
#include "resource.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* How much audio the engine buffers ahead. Long enough to survive a scheduling
   hiccup on a busy machine, short enough that a dismiss is not audibly late. */
#define AUDIO_BUFFER_MS   200
#define REFTIMES_PER_MS   10000

/* The generated alarm pattern, matching the old Beep() cadence. */
#define TONE_A_HZ     1000.0
#define TONE_B_HZ     1200.0
#define TONE_ON_MS     200.0
#define TONE_GAP_MS     80.0
#define TONE_TAIL_MS   500.0
#define TONE_PERIOD_MS (TONE_ON_MS + TONE_GAP_MS + TONE_ON_MS + TONE_TAIL_MS)

/* ---------- module state ---------- */

static BOOL   g_mfStarted   = FALSE;
static HANDLE g_thread      = NULL;
static HANDLE g_stopEvent   = NULL;
static HANDLE g_readyEvent  = NULL;
static volatile LONG g_startOk = 0;
static HWND   g_notifyWnd   = NULL;
static WCHAR  g_path[MAX_PATH];
static BOOL   g_useTone     = FALSE;
static volatile LONG g_playing = 0;

/* Gain and its ramp. Plain volatile rather than a lock: the render thread reads
   these once per buffer, and a torn read costs at worst one buffer at a
   slightly stale level. */
static volatile float  g_gain      = 1.0f;
static volatile float  g_rampFrom  = 1.0f;
static volatile float  g_rampTo    = 1.0f;
static volatile DWORD  g_rampMs    = 0;
static ULONGLONG       g_rampStart = 0;

/* ---------- decoded-file source ---------- */

typedef struct {
    IMFSourceReader *reader;
    IMFMediaBuffer  *buf;
    BYTE            *locked;
    DWORD            lockedBytes;
    DWORD            offsetBytes;
    BOOL             eof;
} FileSource;

static FileSource g_fs;

/* Tone phase, in frames since playback started. */
static ULONGLONG g_toneFrame = 0;

static void file_release_buffer(void) {
    if (g_fs.buf) {
        if (g_fs.locked) IMFMediaBuffer_Unlock(g_fs.buf);
        IMFMediaBuffer_Release(g_fs.buf);
    }
    g_fs.buf = NULL;
    g_fs.locked = NULL;
    g_fs.lockedBytes = 0;
    g_fs.offsetBytes = 0;
}

static void file_close(void) {
    file_release_buffer();
    if (g_fs.reader) { IMFSourceReader_Release(g_fs.reader); g_fs.reader = NULL; }
    g_fs.eof = FALSE;
}

/* Asks the reader for float PCM at the device's own rate and channel count, so
   Media Foundation does the resampling and channel mapping for us. */
static BOOL file_open(const WCHAR *path, UINT32 rate, UINT32 channels) {
    IMFMediaType *mt = NULL;
    HRESULT hr;

    ZeroMemory(&g_fs, sizeof(g_fs));

    hr = MFCreateSourceReaderFromURL(path, NULL, &g_fs.reader);
    if (FAILED(hr) || !g_fs.reader) return FALSE;

    IMFSourceReader_SetStreamSelection(g_fs.reader, (DWORD)MF_SOURCE_READER_ALL_STREAMS, FALSE);
    IMFSourceReader_SetStreamSelection(g_fs.reader, (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE);

    hr = MFCreateMediaType(&mt);
    if (FAILED(hr) || !mt) { file_close(); return FALSE; }

    IMFMediaType_SetGUID(mt, &MF_MT_MAJOR_TYPE, &MFMediaType_Audio);
    IMFMediaType_SetGUID(mt, &MF_MT_SUBTYPE, &MFAudioFormat_Float);
    IMFMediaType_SetUINT32(mt, &MF_MT_AUDIO_SAMPLES_PER_SECOND, rate);
    IMFMediaType_SetUINT32(mt, &MF_MT_AUDIO_NUM_CHANNELS, channels);
    IMFMediaType_SetUINT32(mt, &MF_MT_AUDIO_BITS_PER_SAMPLE, 32);
    IMFMediaType_SetUINT32(mt, &MF_MT_AUDIO_BLOCK_ALIGNMENT, channels * 4);
    IMFMediaType_SetUINT32(mt, &MF_MT_AUDIO_AVG_BYTES_PER_SECOND, rate * channels * 4);
    IMFMediaType_SetUINT32(mt, &MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);

    hr = IMFSourceReader_SetCurrentMediaType(g_fs.reader,
            (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, NULL, mt);
    IMFMediaType_Release(mt);

    if (FAILED(hr)) { file_close(); return FALSE; }
    return TRUE;
}

/* TRUE if a decoded buffer is ready. Sets eof at the end of the stream. */
static BOOL file_next_buffer(void) {
    /* A read can legitimately return no sample (a stream gap or a format
       change) without being the end; give it a few tries before giving up so a
       transient never looks like EOF. */
    for (int attempt = 0; attempt < 8; attempt++) {
        DWORD flags = 0;
        IMFSample *sample = NULL;

        HRESULT hr = IMFSourceReader_ReadSample(g_fs.reader,
                        (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM,
                        0, NULL, &flags, NULL, &sample);
        if (FAILED(hr)) { g_fs.eof = TRUE; return FALSE; }

        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
            if (sample) IMFSample_Release(sample);
            g_fs.eof = TRUE;
            return FALSE;
        }
        if (!sample) continue;

        hr = IMFSample_ConvertToContiguousBuffer(sample, &g_fs.buf);
        IMFSample_Release(sample);
        if (FAILED(hr) || !g_fs.buf) { g_fs.buf = NULL; continue; }

        hr = IMFMediaBuffer_Lock(g_fs.buf, &g_fs.locked, NULL, &g_fs.lockedBytes);
        if (FAILED(hr)) {
            IMFMediaBuffer_Release(g_fs.buf);
            g_fs.buf = NULL; g_fs.locked = NULL;
            continue;
        }
        g_fs.offsetBytes = 0;
        if (g_fs.lockedBytes == 0) { file_release_buffer(); continue; }
        return TRUE;
    }
    g_fs.eof = TRUE;
    return FALSE;
}

static UINT32 file_fill(float *dst, UINT32 frames, UINT32 channels) {
    UINT32 produced = 0;
    const UINT32 frameBytes = channels * (UINT32)sizeof(float);

    while (produced < frames) {
        if (!g_fs.locked) {
            if (g_fs.eof) break;
            if (!file_next_buffer()) break;
        }
        DWORD  availBytes  = g_fs.lockedBytes - g_fs.offsetBytes;
        UINT32 availFrames = availBytes / frameBytes;
        UINT32 want        = frames - produced;
        UINT32 n           = (availFrames < want) ? availFrames : want;

        if (n) {
            memcpy(dst + (size_t)produced * channels,
                   g_fs.locked + g_fs.offsetBytes,
                   (size_t)n * frameBytes);
            produced += n;
            g_fs.offsetBytes += n * frameBytes;
        }
        if (g_fs.offsetBytes + frameBytes > g_fs.lockedBytes) file_release_buffer();
    }
    return produced;
}

/* ---------- generated tone source ---------- */

/* Short attack and release so each burst does not click. */
static double tone_envelope(double posMs, double lenMs) {
    const double edge = 5.0;
    if (posMs < edge)            return posMs / edge;
    if (posMs > lenMs - edge)    return (lenMs - posMs) / edge;
    return 1.0;
}

static UINT32 tone_fill(float *dst, UINT32 frames, UINT32 channels, UINT32 rate) {
    for (UINT32 i = 0; i < frames; i++) {
        double tMs = ((double)(g_toneFrame + i) * 1000.0) / (double)rate;
        double pos = fmod(tMs, TONE_PERIOD_MS);

        double freq = 0.0, local = 0.0;
        if (pos < TONE_ON_MS) {
            freq = TONE_A_HZ; local = pos;
        } else if (pos >= TONE_ON_MS + TONE_GAP_MS &&
                   pos <  TONE_ON_MS + TONE_GAP_MS + TONE_ON_MS) {
            freq = TONE_B_HZ; local = pos - (TONE_ON_MS + TONE_GAP_MS);
        }

        float v = 0.0f;
        if (freq > 0.0) {
            /* Phase measured from the start of this burst, so it always begins
               at zero crossing regardless of where the buffer boundary fell. */
            double env = tone_envelope(local, TONE_ON_MS);
            v = (float)(0.45 * env * sin(2.0 * M_PI * freq * (local / 1000.0)));
        }
        for (UINT32 c = 0; c < channels; c++) dst[(size_t)i * channels + c] = v;
    }
    g_toneFrame += frames;
    return frames;
}

/* ---------- gain ---------- */

static float current_gain(void) {
    DWORD ms = g_rampMs;
    if (ms == 0) return g_gain;

    ULONGLONG elapsed = GetTickCount64() - g_rampStart;
    if (elapsed >= ms) {
        g_gain   = g_rampTo;
        g_rampMs = 0;
        return g_gain;
    }
    float t = (float)elapsed / (float)ms;
    float g = g_rampFrom + (g_rampTo - g_rampFrom) * t;
    g_gain = g;
    return g;
}

void audio_set_gain(float gain) {
    if (gain < 0.0f) gain = 0.0f;
    if (gain > 1.0f) gain = 1.0f;
    g_rampMs = 0;
    g_gain   = gain;
}

float audio_get_gain(void) { return g_gain; }

void audio_ramp_gain(float from, float to, DWORD ms) {
    if (from < 0.0f) from = 0.0f;
    if (from > 1.0f) from = 1.0f;
    if (to   < 0.0f) to   = 0.0f;
    if (to   > 1.0f) to   = 1.0f;

    if (ms == 0) { audio_set_gain(to); return; }
    g_gain      = from;
    g_rampFrom  = from;
    g_rampTo    = to;
    g_rampStart = GetTickCount64();
    g_rampMs    = ms;
}

BOOL audio_ramp_active(void) { return g_rampMs != 0; }

/* ---------- device format ---------- */

/* KSDATAFORMAT_SUBTYPE_PCM and _IEEE_FLOAT differ only in Data1 (1 and 3), so
   this avoids dragging in ksmedia.h for one comparison. */
#define WAVE_SUBFORMAT_IEEE_FLOAT 0x00000003
#define WAVE_SUBFORMAT_PCM        0x00000001

static BOOL format_is_float(const WAVEFORMATEX *wf) {
    if (wf->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) return TRUE;
    if (wf->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        const WAVEFORMATEXTENSIBLE *we = (const WAVEFORMATEXTENSIBLE *)wf;
        return we->SubFormat.Data1 == WAVE_SUBFORMAT_IEEE_FLOAT;
    }
    return FALSE;
}

/* ---------- render thread ---------- */

typedef struct {
    IMMDeviceEnumerator *enumr;
    IMMDevice           *dev;
    IAudioClient        *client;
    IAudioRenderClient  *render;
    WAVEFORMATEX        *mix;
    HANDLE               bufferEvent;
} Device;

static void device_close(Device *d) {
    if (d->client)      IAudioClient_Stop(d->client);
    if (d->render)      IAudioRenderClient_Release(d->render);
    if (d->client)      IAudioClient_Release(d->client);
    if (d->mix)         CoTaskMemFree(d->mix);
    if (d->dev)         IMMDevice_Release(d->dev);
    if (d->enumr)       IMMDeviceEnumerator_Release(d->enumr);
    if (d->bufferEvent) CloseHandle(d->bufferEvent);
    ZeroMemory(d, sizeof(*d));
}

static BOOL device_open(Device *d, UINT32 *bufferFrames) {
    HRESULT hr;
    ZeroMemory(d, sizeof(*d));

    hr = CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL,
                          &IID_IMMDeviceEnumerator, (void **)&d->enumr);
    if (FAILED(hr)) return FALSE;

    hr = IMMDeviceEnumerator_GetDefaultAudioEndpoint(d->enumr, eRender, eConsole, &d->dev);
    if (FAILED(hr)) { device_close(d); return FALSE; }

    hr = IMMDevice_Activate(d->dev, &IID_IAudioClient, CLSCTX_ALL, NULL, (void **)&d->client);
    if (FAILED(hr)) { device_close(d); return FALSE; }

    hr = IAudioClient_GetMixFormat(d->client, &d->mix);
    if (FAILED(hr) || !d->mix) { device_close(d); return FALSE; }

    REFERENCE_TIME dur = (REFERENCE_TIME)AUDIO_BUFFER_MS * REFTIMES_PER_MS;
    hr = IAudioClient_Initialize(d->client, AUDCLNT_SHAREMODE_SHARED,
                                 AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                 dur, 0, d->mix, NULL);
    if (FAILED(hr)) { device_close(d); return FALSE; }

    d->bufferEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (!d->bufferEvent) { device_close(d); return FALSE; }

    hr = IAudioClient_SetEventHandle(d->client, d->bufferEvent);
    if (FAILED(hr)) { device_close(d); return FALSE; }

    hr = IAudioClient_GetBufferSize(d->client, bufferFrames);
    if (FAILED(hr)) { device_close(d); return FALSE; }

    hr = IAudioClient_GetService(d->client, &IID_IAudioRenderClient, (void **)&d->render);
    if (FAILED(hr)) { device_close(d); return FALSE; }

    return TRUE;
}

/* Writes `frames` of gained audio into the device buffer, converting if the
   endpoint is not float. */
static void write_frames(BYTE *out, const float *src, UINT32 frames,
                         UINT32 channels, BOOL isFloat, UINT32 bytesPerSample,
                         float gain) {
    UINT32 samples = frames * channels;

    if (isFloat) {
        float *dst = (float *)out;
        for (UINT32 i = 0; i < samples; i++) dst[i] = src[i] * gain;
        return;
    }

    if (bytesPerSample == 2) {
        short *dst = (short *)out;
        for (UINT32 i = 0; i < samples; i++) {
            float v = src[i] * gain;
            if (v >  1.0f) v =  1.0f;
            if (v < -1.0f) v = -1.0f;
            dst[i] = (short)(v * 32767.0f);
        }
        return;
    }

    /* Anything else: silence rather than noise. */
    ZeroMemory(out, (size_t)frames * channels * bytesPerSample);
}

static DWORD WINAPI render_thread(LPVOID param) {
    (void)param;

    Device d;
    UINT32 bufferFrames = 0;
    UINT32 channels = 0, rate = 0, bytesPerSample = 0;
    BOOL   isFloat    = FALSE;
    float *scratch    = NULL;
    BOOL   started    = FALSE;
    BOOL   reachedEnd = FALSE;

    CoInitializeEx(NULL, COINIT_MULTITHREADED);
    ZeroMemory(&d, sizeof(d));

    if (device_open(&d, &bufferFrames)) {
        channels       = d.mix->nChannels;
        rate           = d.mix->nSamplesPerSec;
        isFloat        = format_is_float(d.mix);
        bytesPerSample = d.mix->wBitsPerSample / 8;

        if (g_useTone || file_open(g_path, rate, channels)) {
            scratch = (float *)malloc((size_t)bufferFrames * channels * sizeof(float));
            if (scratch) started = TRUE;
        }
    }

    /* Whether the device opened and the file actually decoded is only knowable
       here, so the verdict is published before any rendering starts and the
       caller waits for it. sound.c has to be able to fall back to the tone when
       a track will not play, and it cannot do that if starting always
       "succeeds". */
    InterlockedExchange(&g_startOk, started ? 1 : 0);
    if (!started) InterlockedExchange(&g_playing, 0);
    SetEvent(g_readyEvent);

    if (!started) {
        free(scratch);
        file_close();
        device_close(&d);
        CoUninitialize();
        return 1;
    }

    g_toneFrame = 0;
    IAudioClient_Start(d.client);

    HANDLE waits[2] = { g_stopEvent, d.bufferEvent };

    for (;;) {
        DWORD w = WaitForMultipleObjects(2, waits, FALSE, 2000);
        if (w == WAIT_OBJECT_0) break;                 /* stop */
        if (w == WAIT_FAILED)   break;
        /* WAIT_TIMEOUT falls through and tops the buffer up anyway. */

        UINT32 padding = 0;
        if (FAILED(IAudioClient_GetCurrentPadding(d.client, &padding))) break;

        UINT32 avail = bufferFrames - padding;
        if (avail == 0) continue;

        BYTE *out = NULL;
        if (FAILED(IAudioRenderClient_GetBuffer(d.render, avail, &out))) break;

        UINT32 got = g_useTone ? tone_fill(scratch, avail, channels, rate)
                               : file_fill(scratch, avail, channels);

        if (got == 0) {
            /* Nothing left to decode: let what is already queued drain, then
               report the track as finished rather than cutting it off. */
            IAudioRenderClient_ReleaseBuffer(d.render, 0, AUDCLNT_BUFFERFLAGS_SILENT);
            reachedEnd = TRUE;
            break;
        }

        write_frames(out, scratch, got, channels, isFloat, bytesPerSample, current_gain());
        IAudioRenderClient_ReleaseBuffer(d.render, got, 0);
    }

    if (reachedEnd) {
        /* Wait out the audio already handed to the engine so the last second of
           a track is actually heard. */
        UINT32 padding = 0;
        for (int i = 0; i < 100; i++) {
            if (WaitForSingleObject(g_stopEvent, 20) == WAIT_OBJECT_0) break;
            if (FAILED(IAudioClient_GetCurrentPadding(d.client, &padding))) break;
            if (padding == 0) break;
        }
    }

    free(scratch);
    file_close();
    device_close(&d);
    CoUninitialize();

    InterlockedExchange(&g_playing, 0);

    /* Only a natural end asks for the next track; a stop must not restart the
       playlist it just cancelled. */
    if (reachedEnd && g_notifyWnd &&
        WaitForSingleObject(g_stopEvent, 0) != WAIT_OBJECT_0) {
        PostMessageW(g_notifyWnd, WM_AUDIO_TRACK_DONE, 0, 0);
    }
    return 0;
}

/* ---------- public API ---------- */

BOOL audio_init(void) {
    if (FAILED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED))) {
        /* Already initialised on this thread with another model is fine. */
    }
    if (!g_mfStarted) {
        if (FAILED(MFStartup(MF_VERSION, MFSTARTUP_LITE))) return FALSE;
        g_mfStarted = TRUE;
    }
    if (!g_stopEvent) {
        g_stopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);   /* manual reset */
        if (!g_stopEvent) return FALSE;
    }
    if (!g_readyEvent) {
        g_readyEvent = CreateEventW(NULL, TRUE, FALSE, NULL);  /* manual reset */
        if (!g_readyEvent) return FALSE;
    }
    return TRUE;
}

void audio_shutdown(void) {
    audio_stop();
    if (g_stopEvent)  { CloseHandle(g_stopEvent);  g_stopEvent  = NULL; }
    if (g_readyEvent) { CloseHandle(g_readyEvent); g_readyEvent = NULL; }
    if (g_mfStarted) { MFShutdown(); g_mfStarted = FALSE; }
    CoUninitialize();
}

static BOOL audio_start(BOOL tone, const WCHAR *path, HWND notifyWnd) {
    audio_stop();
    if (!g_stopEvent || !g_readyEvent) return FALSE;

    ResetEvent(g_stopEvent);
    ResetEvent(g_readyEvent);
    InterlockedExchange(&g_startOk, 0);

    g_useTone   = tone;
    g_notifyWnd = notifyWnd;
    if (path) lstrcpynW(g_path, path, MAX_PATH); else g_path[0] = 0;

    InterlockedExchange(&g_playing, 1);
    g_thread = CreateThread(NULL, 0, render_thread, NULL, 0, NULL);
    if (!g_thread) {
        InterlockedExchange(&g_playing, 0);
        return FALSE;
    }

    /* Opening a device and a decoder is quick, and waiting for the answer is
       what lets this function mean "it is playing" rather than "a thread was
       created". */
    if (WaitForSingleObject(g_readyEvent, 4000) != WAIT_OBJECT_0) {
        audio_stop();
        return FALSE;
    }
    if (!g_startOk) { audio_stop(); return FALSE; }
    return TRUE;
}

BOOL audio_play_file(const WCHAR *path, HWND notifyWnd) {
    if (!path || !path[0]) return FALSE;
    return audio_start(FALSE, path, notifyWnd);
}

BOOL audio_play_tone(HWND notifyWnd) {
    return audio_start(TRUE, NULL, notifyWnd);
}

void audio_stop(void) {
    if (g_stopEvent) SetEvent(g_stopEvent);
    if (g_thread) {
        WaitForSingleObject(g_thread, 3000);
        CloseHandle(g_thread);
        g_thread = NULL;
    }
    InterlockedExchange(&g_playing, 0);
    g_rampMs = 0;
}

BOOL audio_is_playing(void) { return g_playing != 0; }
