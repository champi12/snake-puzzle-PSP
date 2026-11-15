/* main.c - Snake Puzzle para PSP: arranque, bucle principal, botones y medidor de rendimiento (SELECT) */
#include <pspkernel.h>
#include <psputility.h>
#include <pspctrl.h>
#include <psppower.h>
#include <pspdisplay.h>
#include <pspdebug.h>
#include <psprtc.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <malloc.h>
#include "gfx.h"
#include "game.h"
#include "audio.h"
#include "devlog.h"
#include "lang.h"
#include "fpu.h"

PSP_MODULE_INFO("SnakePuzzle", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);
PSP_HEAP_SIZE_KB(-1024);

static volatile int running = 1;
static char write_dir[192];

/* La carpeta del juego si se puede escribir en ella; si no (PPSSPP abriendo el EBOOT desde otra carpeta la monta
 * como solo lectura), la del juego en la Memory Stick. Ahi van devlog.txt, crash.txt y save.dat. */
const char *plat_write_dir(void) { return write_dir; }
static void pick_write_dir(void)
{
    char cwd[160], test[200];
    if (!getcwd(cwd, sizeof(cwd))) strcpy(cwd, "ms0:/PSP/GAME/SNAKEPZL");
    snprintf(test, sizeof(test), "%s/.prueba_escritura", cwd);
    SceUID fd = sceIoOpen(test, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
    if (fd >= 0 && sceIoWrite(fd, "ok", 2) == 2) {
        sceIoClose(fd);
        sceIoRemove(test);
        snprintf(write_dir, sizeof(write_dir), "%s", cwd);
        return;
    }
    if (fd >= 0) sceIoClose(fd);
    sceIoMkdir("ms0:/PSP", 0777);
    sceIoMkdir("ms0:/PSP/GAME", 0777);
    sceIoMkdir("ms0:/PSP/GAME/SNAKEPZL", 0777);
    strcpy(write_dir, "ms0:/PSP/GAME/SNAKEPZL");
}

unsigned plat_ms(void) { return sceKernelGetSystemTimeLow() / 1000; }
unsigned plat_us(void) { return sceKernelGetSystemTimeLow(); }
void plat_exit(void) { running = 0; }

void plat_date(int *y, int *m, int *d)
{
    ScePspDateTime t;
    if (sceRtcGetCurrentClockLocalTime(&t) < 0) {
        *y = 2026; *m = 1; *d = 1;
        return;
    }
    *y = t.year;
    *m = t.month;
    *d = t.day;
}

int plat_secs_of_day(void)
{
    ScePspDateTime t;
    if (sceRtcGetCurrentClockLocalTime(&t) < 0) return 0;
    return t.hour * 3600 + t.minute * 60 + t.second;
}

static int exit_cb(int a1, int a2, void *c) { running = 0; return 0; }
static int cb_thread(SceSize a, void *p)
{
    fpu_no_exceptions();
    int id = sceKernelCreateCallback("exit", exit_cb, NULL);
    sceKernelRegisterExitCallback(id);
    sceKernelSleepThreadCB();
    return 0;
}

void fatal_error(const char *msg)
{
    dlog_error("%s", msg);
    dlog_close();
    pspDebugScreenInit();
    pspDebugScreenPrintf("Snake Puzzle PSP\n\nError: %s\n\n", msg);
    pspDebugScreenPrintf("Hay que generar la carpeta data/ con tools/extract.py\na partir del XAPK original (ver README.md).\n");
    while (running) sceDisplayWaitVblankStart();
    sceKernelExitGame();
}

#if DEVLOG
#include "../exhandler/exhandler.h"
/* Carga exhandler.prx (modulo de kernel): si la CPU lanza una excepcion, en vez de congelarse la consola se
 * escribe crash.txt con la direccion exacta del fallo. Solo en firmware personalizado; si no se puede, se sigue. */
static void load_exhandler(void)
{
    static ExhArgs a;
    char dir[160];
    if (!getcwd(dir, sizeof(dir))) strcpy(dir, "ms0:/PSP/GAME/SNAKEPZL");
    char prx[180];
    snprintf(prx, sizeof(prx), "%s/exhandler.prx", dir);
    snprintf(a.crash_path, sizeof(a.crash_path), "%.170s/crash.txt", write_dir);
    snprintf(a.devlog_path, sizeof(a.devlog_path), "%.170s/devlog.txt", write_dir);
    a.phase_main = &dbg_phase_main;
    a.phase_music = &dbg_phase_music;
    a.phase_mixer = &dbg_phase_mixer;
    SceUID m = sceKernelLoadModule(prx, 0, NULL);
    if (m >= 0) {
        int st = 0;
        int r = sceKernelStartModule(m, sizeof(a), &a, &st, NULL);
        (void)r;
        dlog("Capturador de excepciones: %s (carga 0x%08x, inicio 0x%08x, estado %d)",
             r >= 0 && st == 0 ? "activo, un cuelgue escribira crash.txt" : "NO activo", m, r, st);
        return;
    }
    dlog("Capturador de excepciones: sceKernelLoadModule no pudo (0x%08x); se prueba con kubridge", m);
    static ExLoaderArgs la;
    static int res[3] = {-1, -1, -1};
    snprintf(la.prx_path, sizeof(la.prx_path), "%s", prx);
    la.exh = a;
    la.result = res;
    char ldr[200];
    snprintf(ldr, sizeof(ldr), "%s/exloader.prx", dir);
    SceUID lm = sceKernelLoadModule(ldr, 0, NULL);
    ExLoaderArgs *lp = &la;
    int lst = 0, lr = lm >= 0 ? sceKernelStartModule(lm, sizeof(lp), &lp, &lst, NULL) : lm;
    if (lr >= 0 && la.load) la.load(&la);
    dlog("Capturador de excepciones (kubridge): %s (exloader 0x%08x/0x%08x, carga 0x%08x, inicio 0x%08x, estado %d)",
         res[0] >= 0 && res[1] >= 0 && res[2] == 0 ? "activo, un cuelgue escribira crash.txt" : "NO activo",
         lm, lr, res[0], res[1], res[2]);
}
#endif

/* ---------------------------------------------------------------- medidor de rendimiento (SELECT) */
static uint32_t ok_col(float frac) { return frac < 0.75f ? RGBA(120, 255, 120, 255) : frac < 0.9f ? RGBA(255, 230, 80, 255) : RGBA(255, 90, 90, 255); }

static void perf_draw(int page, float cpu_ms, float wait_ms, float fps)
{
    char b[128];
    MemInfo *m = &dbg_mem;
    if (page == 1) {
        gfx_fill(0, 24, 300, 60, RGBA(0, 0, 0, 170));
        snprintf(b, sizeof(b), "FPS %.0f   CPU %.1f ms   espera %.1f ms", fps, cpu_ms, wait_ms);
        text_draw(6, 28, FONT_S, b, fps >= 55 ? RGBA(120, 255, 120, 255) : fps >= 30 ? RGBA(255, 230, 80, 255) : RGBA(255, 90, 90, 255), 0);
        snprintf(b, sizeof(b), "dibujos %d  vert %d  texturas %d", gstat.draws, gstat.verts, gstat.binds);
        text_draw(6, 44, FONT_S, b, WHITE, 0);
        float rf = m->heap_total ? (float)m->heap_used / m->heap_total : 0, vf = (gstat.vram_fb + gstat.vram_tex) / 2097152.0f;
        snprintf(b, sizeof(b), "RAM %u/%u KB   VRAM %u/2048 KB", m->heap_used / 1024, m->heap_total / 1024,
                 (unsigned)(gstat.vram_fb + gstat.vram_tex) / 1024);
        text_draw(6, 60, FONT_S, b, ok_col(rf > vf ? rf : vf), 0);
    } else if (page == 2) {
        gfx_fill(0, 24, 480, 164, RGBA(0, 0, 0, 170));
        float y = 28;
        snprintf(b, sizeof(b), "RAM monton: %u / %u KB (pico %u)", m->heap_used / 1024, m->heap_total / 1024, m->heap_peak / 1024);
        text_draw(6, y, FONT_S, b, ok_col(m->heap_total ? (float)m->heap_peak / m->heap_total : 0), 0);
        y += 16;
        snprintf(b, sizeof(b), "Sistema libre: %u KB (bloque mayor %u KB)", m->sys_free / 1024, m->sys_max_block / 1024);
        text_draw(6, y, FONT_S, b, m->sys_free < 256 * 1024 ? RGBA(255, 90, 90, 255) : WHITE, 0);
        y += 16;
        snprintf(b, sizeof(b), "VRAM: %u / 2048 KB (pantallas %u, texturas %u en %d)", (unsigned)(gstat.vram_fb + gstat.vram_tex) / 1024,
                 (unsigned)gstat.vram_fb / 1024, (unsigned)gstat.vram_tex / 1024, gstat.vram_tex_n);
        text_draw(6, y, FONT_S, b, WHITE, 0);
        y += 16;
        snprintf(b, sizeof(b), "Texturas en RAM: %u KB (%d)", (unsigned)gstat.ram_tex / 1024, gstat.ram_tex_n);
        text_draw(6, y, FONT_S, b, WHITE, 0);
        y += 16;
        snprintf(b, sizeof(b), "Lista GE: %d%% (pico %d%%)", gstat.dlist_max ? gstat.dlist_used * 100 / gstat.dlist_max : 0,
                 gstat.dlist_max ? gstat.dlist_peak * 100 / gstat.dlist_max : 0);
        text_draw(6, y, FONT_S, b, ok_col(gstat.dlist_max ? (float)gstat.dlist_peak / gstat.dlist_max : 0), 0);
        y += 16;
        snprintf(b, sizeof(b), "Tirones (>50 ms): %d", dbg_hitches);
        text_draw(6, y, FONT_S, b, dbg_hitches ? RGBA(255, 230, 80, 255) : WHITE, 0);
        y += 16;
        snprintf(b, sizeof(b), "CPU %d MHz  bus %d MHz  bateria %d%% %d C %s", dbg_cpu_mhz, dbg_bus_mhz, dbg_bat_pct, dbg_bat_temp,
                 dbg_plugged ? "(cargando)" : "");
        text_draw(6, y, FONT_S, b, WHITE, 0);
        y += 16;
        text_draw(6, y, FONT_S, audio_status(), WHITE, 0);
        y += 16;
        snprintf(b, sizeof(b), "Devlog: %s%s", dlog_path(), DEVLOG ? (dlog_ok() ? " (escribiendo)" : " (NO se puede escribir)") : "");
        text_draw(6, y, FONT_S, b, DEVLOG && !dlog_ok() ? RGBA(255, 90, 90, 255) : WHITE, 0);
    } else if (page == 3) {
        gfx_fill(0, 24, 280, 100, RGBA(0, 0, 0, 170));
        text_draw(6, 26, FONT_S, "ms por fotograma (verde < 16,7 | amarillo < 33,3)", WHITE, 0);
        for (int i = 0; i < 128; i++) {
            float ms = dbg_frame_hist[(dbg_hist_pos + i) & 127];
            float h = ms * 2;
            if (h > 76) h = 76;
            gfx_fill(10 + i * 2, 120 - h, 2, h, ms < 16.8f ? RGBA(120, 255, 120, 255) : ms < 33.4f ? RGBA(255, 230, 80, 255) : RGBA(255, 90, 90, 255));
        }
        gfx_fill(10, 120 - 33.3f, 256, 1, RGBA(255, 255, 255, 120));
    }
}

int main(int argc, char **argv)
{
    unsigned int fcr31 = fpu_get_fcr31();
    fpu_no_exceptions();
    mem_init();
    pick_write_dir();
    SceUID th = sceKernelCreateThread("cb", cb_thread, 0x11, 0xFA0, 0, 0);
    if (th >= 0) sceKernelStartThread(th, 0, 0);
    int clk = scePowerSetClockFrequency(333, 333, 166);
    sceCtrlSetSamplingCycle(0);
#if ANALOG
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
#else
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_DIGITAL);   /* stick desactivado (ANALOG=0): solo la cruceta */
#endif
    dlog_init();
    dlog("CONTROLES: stick analogico %s", ANALOG ? "activado" : "DESACTIVADO (solo cruceta; make ANALOG=1 para activarlo)");
#if DEVLOG
    load_exhandler();
#endif
    dlog("Carpeta para escribir (devlog, partida): %s", write_dir);
    dlog("FPU: FCR31 al arrancar 0x%08x (excepciones activadas 0x%02x) -> desactivadas en todos los hilos", fcr31, (fcr31 >> 7) & 31);
    if (clk < 0) dlog("AVISO no se pudo poner la CPU a 333 MHz (0x%08x)", clk);
    unsigned tl = sceKernelGetSystemTimeLow();
    (void)tl;
    (void)fcr31;
    {
        /* idioma de la consola: espanol o, si es otro, ingles (como el original) */
        int l = 1;
        if (sceUtilityGetSystemParamInt(PSP_SYSTEMPARAM_ID_INT_LANGUAGE, &l) < 0) l = 1;
        lang_init(l);
        dlog("Idioma de la consola %d -> textos en %s", l, lang_en ? "ingles" : "espanol");
    }
    gfx_init();
    dlog("GE iniciado: VRAM pantallas %u KB (16 bits, doble buffer, sin Z), %u KB libres para texturas",
         (unsigned)gstat.vram_fb / 1024, (unsigned)(2048 - gstat.vram_fb / 1024));
    loading_begin();
    audio_init("data");
    loading_step(0.25f);
    game_init();
    loading_end();
    mem_query(&dbg_mem);
    dlog_boot_done();
    dlog_mem("arranque");
    dlog("Arranque completo en %.2f s | monton %u/%u KB usado | libre fuera del monton %u KB",
         (sceKernelGetSystemTimeLow() - tl) / 1000000.0f, dbg_mem.heap_used / 1024, dbg_mem.heap_total / 1024,
         dbg_mem.sys_free / 1024);
    dlog_flush();

    unsigned prev = 0;
    int stick = 0, show_perf = 0;
    unsigned last = sceKernelGetSystemTimeLow();
    float fps = 60, cpu_ms = 0, wait_ms = 0;
    while (running) {
        unsigned t0 = sceKernelGetSystemTimeLow();
        float dt = (t0 - last) / 1000000.0f;
        last = t0;
        if (dt <= 0 || dt > 0.25f) dt = 1.0f / 60.0f;
        fps = fps * 0.95f + (1.0f / dt) * 0.05f;

        DBG_PHASE(main, "leyendo los botones");
        SceCtrlData pad;
        sceCtrlPeekBufferPositive(&pad, 1);
        unsigned b = pad.Buttons;
#if ANALOG
        /* el stick hace de cruceta */
        int ax = pad.Lx - 128, ay = pad.Ly - 128, sdir = 0;
        if (ax < -70) sdir = PSP_CTRL_LEFT;
        else if (ax > 70) sdir = PSP_CTRL_RIGHT;
        else if (ay < -70) sdir = PSP_CTRL_UP;
        else if (ay > 70) sdir = PSP_CTRL_DOWN;
        if (abs(ax) < 40 && abs(ay) < 40) stick = 0;
        else if (sdir) stick = sdir;
        b |= stick;
#else
        (void)stick;
#endif
        unsigned pr = b & ~prev;
        prev = b;
        if (pr & PSP_CTRL_SELECT) show_perf = (show_perf + 1) & 3;
        unsigned in = 0, held = 0;
        static const struct { unsigned psp, g; } map[] = {
            {PSP_CTRL_UP, IN_UP}, {PSP_CTRL_DOWN, IN_DOWN}, {PSP_CTRL_LEFT, IN_LEFT}, {PSP_CTRL_RIGHT, IN_RIGHT},
            {PSP_CTRL_CROSS, IN_CROSS}, {PSP_CTRL_CIRCLE, IN_CIRCLE}, {PSP_CTRL_SQUARE, IN_SQUARE},
            {PSP_CTRL_TRIANGLE, IN_TRIANGLE}, {PSP_CTRL_LTRIGGER, IN_L}, {PSP_CTRL_RTRIGGER, IN_R},
            {PSP_CTRL_START, IN_START}};
        for (unsigned k = 0; k < sizeof(map) / sizeof(map[0]); k++) {
            if (pr & map[k].psp) in |= map[k].g;
            if (b & map[k].psp) held |= map[k].g;
        }
        /* repeticion en los menus al mantener la cruceta */
        static float rep;
        if (held & (IN_UP | IN_DOWN | IN_LEFT | IN_RIGHT)) {
            rep += dt;
            if (rep > 0.4f && gstate != GS_PLAY && gstate != GS_CLASSIC) {
                in |= held & (IN_UP | IN_DOWN | IN_LEFT | IN_RIGHT);
                rep = 0.32f;
            }
        } else rep = 0;
        DBG_PHASE(main, "game_input");
        game_input(in, held);
        unsigned tu0 = sceKernelGetSystemTimeLow();
        DBG_PHASE(main, "game_update");
        game_update(dt);
        unsigned tu1 = sceKernelGetSystemTimeLow();
        DBG_PHASE(main, "game_draw");
        gfx_begin(RGBA(40, 60, 90, 255));
        game_draw();
        if (show_perf) perf_draw(show_perf, cpu_ms, wait_ms, fps);
        unsigned t1 = sceKernelGetSystemTimeLow();
        float w = gfx_end();
        cpu_ms = cpu_ms * 0.9f + ((t1 - t0) / 1000.0f) * 0.1f;
        wait_ms = wait_ms * 0.9f + w * 0.1f;
        dbg_upd_ms = (tu1 - tu0) / 1000.0f;
        dbg_draw_ms = (t1 - tu1) / 1000.0f;
        dbg_frame(dt * 1000.0f, (t1 - t0) / 1000.0f, w);
    }
    dlog("Salida desde el menu HOME");
    game_save();
    save_flush();
    dlog_close();
    audio_shutdown();
    gfx_shutdown();
    sceKernelExitGame();
    return 0;
}
