/* audio_psp.c - sonido en la PSP
 * - Efectos: PCM 16 bits 22050 Hz mono (data/sfx.bin), hasta 8 a la vez, mezclados en un hilo y enviados a un canal
 *   normal a 44100 Hz (cada muestra se repite dos veces: sin coste de conversion).
 * - Musica: MP3 (data/music.mp3) decodificado por el Media Engine (sceMp3) y enviado al canal SRC; se lee de la
 *   Memory Stick a trozos de 16 KB y repite sin fin, como en el original.
 */
#include <pspkernel.h>
#include <pspaudio.h>
#include <pspmp3.h>
#include <psputility.h>
#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <unistd.h>
#include "audio.h"
#include "devlog.h"
#include "fpu.h"

#define FRAMES 512
#define VOICES 8

typedef struct {
    const short *data;
    int len, pos;
    int vol;                        /* 0..256 */
} Voice;

static short *sfx_data[SFX_COUNT];
static int sfx_len[SFX_COUNT];
static void *sfx_blob;
static Voice voices[VOICES];
static volatile int run, sfx_on = 1, music_on = 1;
static SceUID mix_th = -1, mus_th = -1, lock = -1;
static char music_path[2][200];
static volatile int music_want, music_cur = -1;
static int mp3_ok, sfx_valid;   /* efectos con sonido (0: sfx.bin generado sin libvorbis) */
static char status[96] = "sin iniciar";
static volatile int mix_ch = -1, mp3_state;   /* 0 nada, 1 sonando, -1 error */
const char *audio_status(void)
{
    snprintf(status, sizeof(status), "Audio: efectos %s (%d) | musica %s",
             mix_ch < 0 ? "SIN CANAL" : sfx_valid ? "OK" : "SIN DATOS (regenera data/)", sfx_valid,
             mp3_state > 0 ? "MP3 OK" : mp3_state < 0 ? "ERROR" : mp3_ok ? "esperando" : "sin MP3");
    return status;
}

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

static int mixer_thread(SceSize a, void *p)
{
    fpu_no_exceptions();
    static short __attribute__((aligned(64))) out[2][FRAMES * 2];
    static int acc[FRAMES / 2];
    int ch = sceAudioChReserve(PSP_AUDIO_NEXT_CHANNEL, FRAMES, PSP_AUDIO_FORMAT_STEREO);
    mix_ch = ch;
    if (ch < 0) dlog_error("AUDIO sceAudioChReserve 0x%08x: sin efectos", ch);
    int buf = 0;
    while (run) {
        DBG_PHASE(mixer, "mezclando");
        dbg_count_mixer++;
        memset(acc, 0, sizeof(acc));
        sceKernelWaitSema(lock, 1, 0);
        for (int v = 0; v < VOICES; v++) {
            Voice *vc = &voices[v];
            if (!vc->data) continue;
            int n = vc->len - vc->pos;
            if (n > FRAMES / 2) n = FRAMES / 2;
            const short *src = vc->data + vc->pos;
            for (int i = 0; i < n; i++) acc[i] += (src[i] * vc->vol) >> 8;
            vc->pos += n;
            if (vc->pos >= vc->len) vc->data = NULL;
        }
        sceKernelSignalSema(lock, 1);
        short *o = out[buf];
        for (int i = 0; i < FRAMES / 2; i++) {
            int s = acc[i];
            s = s > 32767 ? 32767 : s < -32768 ? -32768 : s;
            o[i * 4] = o[i * 4 + 1] = o[i * 4 + 2] = o[i * 4 + 3] = s;
        }
        DBG_PHASE(mixer, "sceAudioOutputBlocking");
        if (ch >= 0) sceAudioOutputBlocking(ch, PSP_AUDIO_VOLUME_MAX, o);
        else sceKernelDelayThread(10000);
        buf ^= 1;
    }
    if (ch >= 0) sceAudioChRelease(ch);
    return 0;
}

/* ---------------------------------------------------------------- musica MP3 */
static unsigned char __attribute__((aligned(64))) mp3_buf[16 * 1024];
static short __attribute__((aligned(64))) pcm_buf[16 * (1152 / 2)];

static int fill_stream(SceUID fd, int handle)
{
    unsigned char *dst;
    SceInt32 write, pos;
    if (sceMp3GetInfoToAddStreamData(handle, &dst, &write, &pos) < 0) return -1;
    if (sceIoLseek32(fd, pos, PSP_SEEK_SET) < 0) return -1;
    int rd = sceIoRead(fd, dst, write);
    if (rd < 0) return -1;
    sceMp3NotifyAddStreamData(handle, rd);
    return rd;
}

static int music_thread(SceSize a, void *p)
{
    fpu_no_exceptions();
    SceUID fd = -1;
    int handle = -1, src_ch = -1, last_samples = 0, rate = 44100;
    while (run) {
        dbg_count_music++;
        if (music_want != music_cur) {
            DBG_PHASE(music, "cambiando de cancion (abrir/cerrar MP3)");
            if (handle >= 0) { sceMp3ReleaseMp3Handle(handle); handle = -1; }
            if (fd >= 0) { sceIoClose(fd); fd = -1; }
            music_cur = music_want;
            if (mp3_ok) {
                fd = sceIoOpen(music_path[music_cur], PSP_O_RDONLY, 0);
                if (fd < 0 && music_cur) fd = sceIoOpen(music_path[0], PSP_O_RDONLY, 0);
                if (fd < 0) { mp3_state = -1; dlog_error("MUSICA no se encuentra %s", music_path[music_cur]); }
                else {
                    SceMp3InitArg ia;
                    ia.mp3StreamStart = 0;
                    ia.mp3StreamEnd = sceIoLseek32(fd, 0, PSP_SEEK_END);
                    ia.mp3Buf = mp3_buf;
                    ia.mp3BufSize = sizeof(mp3_buf);
                    ia.pcmBuf = (SceUChar8 *)pcm_buf;
                    ia.pcmBufSize = sizeof(pcm_buf);
                    handle = sceMp3ReserveMp3Handle(&ia);
                    if (handle >= 0 && fill_stream(fd, handle) >= 0 && sceMp3Init(handle) >= 0) {
                        sceMp3SetLoopNum(handle, -1);
                        rate = sceMp3GetSamplingRate(handle);
                        if (rate <= 0) rate = 44100;
                        mp3_state = 1;
                        dlog("MUSICA MP3 %s (%d Hz)", music_path[music_cur], rate);
                    } else {
                        mp3_state = -1;
                        dlog_error("MUSICA no se pudo abrir el MP3 %s (handle 0x%08x)", music_path[music_cur], handle);
                        if (handle >= 0) sceMp3ReleaseMp3Handle(handle);
                        handle = -1;
                        sceIoClose(fd);
                        fd = -1;
                    }
                }
            }
        }
        if (handle < 0 || !music_on) {
            DBG_PHASE(music, "esperando (sin musica)");
            sceKernelDelayThread(20000);
            continue;
        }
        DBG_PHASE(music, "leyendo el MP3 de la Memory Stick");
        if (sceMp3CheckStreamDataNeeded(handle) > 0) fill_stream(fd, handle);
        short *buf;
        DBG_PHASE(music, "sceMp3Decode");
        int bytes = sceMp3Decode(handle, &buf);
        if (bytes <= 0) {
            /* fin o error: se vuelve a abrir (repite) */
            sceMp3ReleaseMp3Handle(handle);
            handle = -1;
            sceIoClose(fd);
            fd = -1;
            music_cur = -1;
            continue;
        }
        int samples = bytes / 4;
        if (src_ch < 0 || samples != last_samples) {
            if (src_ch >= 0) sceAudioSRCChRelease();
            src_ch = sceAudioSRCChReserve(samples, rate, 2);
            last_samples = samples;
        }
        DBG_PHASE(music, "sceAudioSRCOutputBlocking");
        sceAudioSRCOutputBlocking(PSP_AUDIO_VOLUME_MAX * 3 / 4, buf);
    }
    if (handle >= 0) sceMp3ReleaseMp3Handle(handle);
    if (fd >= 0) sceIoClose(fd);
    if (src_ch >= 0) sceAudioSRCChRelease();
    return 0;
}

int audio_init(const char *dir)
{
    char base[200];
    /* los hilos no tienen directorio actual en la PSP: ruta absoluta */
    if (!getcwd(base, sizeof(base))) strcpy(base, "ms0:/PSP/GAME/SNAKEPZL");
    char path[240];
    snprintf(path, sizeof(path), "%s/sfx.bin", dir);
    FILE *f = fopen(path, "rb");
    if (!f) {
        snprintf(path, sizeof(path), "%s/%s/sfx.bin", base, dir);
        f = fopen(path, "rb");
    }
    if (f) {
        fseek(f, 0, SEEK_END);
        long n = ftell(f);
        fseek(f, 0, SEEK_SET);
        sfx_blob = memalign(16, n);
        if (sfx_blob && fread(sfx_blob, 1, n, f) == (size_t)n && !memcmp(sfx_blob, "SFX1", 4)) {
            uint8_t *b = sfx_blob;
            int cnt = rd32(b + 4);
            for (int i = 0; i < cnt && i < SFX_COUNT; i++) {
                sfx_data[i] = (short *)(b + rd32(b + 8 + i * 8));
                sfx_len[i] = rd32(b + 12 + i * 8);
                if (sfx_len[i] > 0) sfx_valid++;
            }
            dlog("CARGA sfx.bin %6ld KB, %d efectos (%d con sonido)", n / 1024, cnt, sfx_valid);
            if (!sfx_valid) dlog_error("sfx.bin no tiene sonidos: se genero sin libogg/libvorbis (ver actualizar.sh)");
        } else
            dlog_error("sfx.bin no valido");
        fclose(f);
    } else
        dlog_error("falta %s: el juego ira sin efectos", path);
    snprintf(music_path[0], sizeof(music_path[0]), "%s/%s/music.mp3", base, dir);
    snprintf(music_path[1], sizeof(music_path[1]), "%s/%s/music_lunar.mp3", base, dir);
    int r1 = sceUtilityLoadModule(PSP_MODULE_AV_AVCODEC), r2 = sceUtilityLoadModule(PSP_MODULE_AV_MP3);
    int r3 = sceMp3InitResource();
    (void)r1;
    (void)r2;
    mp3_ok = r3 >= 0;
    dlog("AUDIO modulos: avcodec 0x%08x mp3 0x%08x | sceMp3InitResource 0x%08x", r1, r2, r3);
    if (!mp3_ok) dlog_error("No se pudo iniciar el decodificador MP3: no habra musica");
    lock = sceKernelCreateSema("audio_lock", 0, 1, 1, NULL);
    run = 1;
    mix_th = sceKernelCreateThread("sfx_mixer", mixer_thread, 0x12, 0x4000, PSP_THREAD_ATTR_USER, NULL);
    if (mix_th >= 0) sceKernelStartThread(mix_th, 0, NULL);
    mus_th = sceKernelCreateThread("music", music_thread, 0x12, 0x8000, PSP_THREAD_ATTR_USER, NULL);
    if (mus_th >= 0) sceKernelStartThread(mus_th, 0, NULL);
    return 0;
}

void audio_shutdown(void)
{
    run = 0;
    if (mix_th >= 0) { sceKernelWaitThreadEnd(mix_th, NULL); sceKernelDeleteThread(mix_th); }
    if (mus_th >= 0) { sceKernelWaitThreadEnd(mus_th, NULL); sceKernelDeleteThread(mus_th); }
    if (mp3_ok) sceMp3TermResource();
}

void audio_play(int id, float vol)
{
    if (!sfx_on || id < 0 || id >= SFX_COUNT || !sfx_data[id] || lock < 0) return;
    sceKernelWaitSema(lock, 1, 0);
    int best = 0;
    for (int v = 0; v < VOICES; v++) {
        if (!voices[v].data) { best = v; break; }
        if (voices[v].pos > voices[best].pos) best = v;   /* el mas avanzado deja sitio */
    }
    voices[best].data = sfx_data[id];
    voices[best].len = sfx_len[id];
    voices[best].pos = 0;
    voices[best].vol = (int)(vol * 256);
    sceKernelSignalSema(lock, 1);
}

void audio_music(int which) { music_want = which ? 1 : 0; }

void audio_set_enabled(int sfx, int music)
{
    sfx_on = sfx;
    music_on = music;
}
