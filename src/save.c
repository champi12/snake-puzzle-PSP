/* save.c - partida guardada en la carpeta del juego (save.dat) y datos de meta.bin */
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include "game.h"
#include "devlog.h"

#define SAVE_MAGIC 0x4b4e5350u   /* "PSNK" */
#define SAVE_VERSION 2

Save sv;
Meta meta;

/* partida del port de la 2.3.4 (version 1): se lee y se pasa a la 2 sin perder nada */
typedef struct {
    uint32_t magic, version;
    uint16_t cur_level;
    uint8_t won[MAX_LEVELS / 8 + 1];
    uint16_t hints, skips;
    uint16_t tokens;
    uint8_t skin, bg;
    uint32_t skins_owned, bgs_owned;
    uint8_t skin_paid[32], bg_paid[32];
    uint8_t sound, music;
    uint8_t daily_day;
    uint32_t daily_last;
    uint16_t classic_best;
    uint8_t progress_done;
    uint8_t event_done;
    uint32_t total_wins;
    uint32_t play_seconds;
} SaveV1;

static int rd16(const uint8_t *p) { return p[0] | p[1] << 8; }
static int rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | p[3] << 24; }

void save_defaults(void)
{
    memset(&sv, 0, sizeof(sv));
    sv.magic = SAVE_MAGIC;
    sv.version = SAVE_VERSION;
    sv.hints = 3;
    sv.skips = 3;
    sv.skins_owned = 1;            /* aspecto 0 (ShopItem Id 0, State 2) */
    sv.bgs_owned = 1;
    sv.sound = sv.music = 1;
    for (int i = 0; i <= LOOP_AHEAD; i++) sv.loop_real[i] = 0xffff;
}

static void from_v1(const SaveV1 *o)
{
    save_defaults();
    sv.cur_level = o->cur_level;
    memcpy(sv.won, o->won, sizeof(sv.won));
    sv.hints = o->hints;
    sv.skips = o->skips;
    sv.tokens = o->tokens;
    sv.skin = o->skin;
    sv.bg = o->bg;
    sv.skins_owned = o->skins_owned;
    sv.bgs_owned = o->bgs_owned;
    memcpy(sv.skin_paid, o->skin_paid, sizeof(o->skin_paid));
    memcpy(sv.bg_paid, o->bg_paid, sizeof(o->bg_paid));
    sv.sound = o->sound;
    sv.music = o->music;
    sv.daily_day = o->daily_day;
    sv.daily_last = o->daily_last;
    sv.classic_best = o->classic_best;
    sv.progress_done = o->progress_done;
    sv.event_done = o->event_done;
    sv.total_wins = o->total_wins;
    sv.play_seconds = o->play_seconds;
}

static char save_path[200], tmp_path[200];
static void save_paths(void)
{
    snprintf(save_path, sizeof(save_path), "%s/save.dat", plat_write_dir());
    snprintf(tmp_path, sizeof(tmp_path), "%s/save.tmp", plat_write_dir());
}

int save_load(void)
{
    save_defaults();
    save_paths();
    FILE *f = fopen(save_path, "rb");
    if (!f) return -1;
    static uint8_t buf[sizeof(Save) + 512];
    memset(buf, 0, sizeof(buf));
    size_t n = fread(buf, 1, sizeof(buf), f);
    fclose(f);
    uint32_t magic, version;
    memcpy(&magic, buf, 4);
    memcpy(&version, buf + 4, 4);
    if (n >= 8 && magic == SAVE_MAGIC && version == SAVE_VERSION && n == sizeof(Save)) {
        memcpy(&sv, buf, sizeof(Save));
        return 0;
    }
    /* version 1 (port de la 2.3.4); con el apartado de Apple Worm era mas larga: se lee lo comun */
    if (n >= sizeof(SaveV1) && magic == SAVE_MAGIC && version == 1) {
        SaveV1 o;
        memcpy(&o, buf, sizeof(o));
        from_v1(&o);
        dlog("PARTIDA convertida de la version 1 (2.3.4) a la 2 (14.2.9): nivel %d, %d fichas", sv.cur_level + 1,
             sv.tokens);
        game_save();
        return 0;
    }
    save_defaults();
    dlog("AVISO save.dat no valido: se empieza de cero");
    return -2;
}

static void write_save(const Save *d)
{
    /* primero a un archivo aparte: si se apaga la consola a medias, el anterior sigue bien */
    FILE *f = fopen(tmp_path, "wb");
    if (!f) {
        dlog_error("no se pudo guardar la partida");
        return;
    }
    fwrite(d, 1, sizeof(*d), f);
    fclose(f);
    remove(save_path);
    rename(tmp_path, save_path);
}

#ifdef __psp__
/* En la PSP escribir en la Memory Stick tarda ~70 ms (los TIRON de 83 ms del devlog al cobrar, comprar o superar un
 * nivel): se guarda en un hilo aparte con una copia de la partida. */
#include <pspkernel.h>
static Save pending;
static volatile int pending_n, busy;
static SceUID save_sema = -1, save_lock = -1;

static int save_thread(SceSize a, void *p)
{
    (void)a;
    (void)p;
    for (;;) {
        sceKernelWaitSema(save_sema, 1, 0);
        busy = 1;
        while (pending_n) {
            static Save copy;
            sceKernelWaitSema(save_lock, 1, 0);
            copy = pending;
            pending_n = 0;
            sceKernelSignalSema(save_lock, 1);
            write_save(&copy);
        }
        busy = 0;
    }
    return 0;
}

void game_save(void)
{
    if (!save_path[0]) save_paths();
    if (save_sema < 0) {
        save_sema = sceKernelCreateSema("save", 0, 0, 1000, 0);
        save_lock = sceKernelCreateSema("savelock", 0, 1, 1, 0);
        SceUID th = sceKernelCreateThread("save", save_thread, 0x30, 0x2000, 0, 0);
        if (save_sema < 0 || save_lock < 0 || th < 0 || sceKernelStartThread(th, 0, 0) < 0) {
            save_sema = -1;
            write_save(&sv);
            return;
        }
    }
    sceKernelWaitSema(save_lock, 1, 0);
    pending = sv;
    pending_n++;
    sceKernelSignalSema(save_lock, 1);
    sceKernelSignalSema(save_sema, 1);
}

/* al salir: que termine de escribir */
void save_flush(void)
{
    for (int i = 0; i < 200 && (pending_n || busy); i++) sceKernelDelayThread(10000);
}
#else
void game_save(void)
{
    if (!save_path[0]) save_paths();
    write_save(&sv);
}
void save_flush(void) {}
#endif

int meta_load(const char *path)
{
    memset(&meta, 0, sizeof(meta));
    for (int i = 0; i < MAX_SKINS; i++) meta.skin_icon[i] = -1;
    for (int i = 0; i < MAX_BGS; i++) meta.bg_icon[i] = -1;
    for (int i = 0; i < 32; i++) meta.shop_ui[i] = -1;
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    static uint8_t b[16384];
    int n = fread(b, 1, sizeof(b), f);
    fclose(f);
    /* META: el de la 2.3.4 (32 iconos); MET2: el de la 14.2.9 (iconos, paquetes con oferta, orden de la tienda, bucle) */
    int v2 = n >= 8 && !memcmp(b, "MET2", 4);
    if (n < 8 || (!v2 && memcmp(b, "META", 4))) return -2;
    const uint8_t *p = b + 4, *end = b + n;
#define NEED(k) do { if (p + (k) > end) return -3; } while (0)
    NEED(2);
    meta.nshop = rd16(p);
    p += 2;
    NEED(meta.nshop * 5);
    for (int i = 0; i < meta.nshop; i++, p += 5) {
        if (i >= 96) continue;
        meta.shop[i] = (ShopItemDef){p[0], p[1], p[2], p[3], p[4]};
    }
    if (meta.nshop > 96) meta.nshop = 96;
    NEED(2);
    meta.nprog = rd16(p);
    p += 2;
    NEED(meta.nprog * 6);
    for (int i = 0; i < meta.nprog; i++, p += 6)
        if (i < 32) meta.prog[i] = (ProgressDef){rd16(p), rd16(p + 2), p[4], p[5]};
    if (meta.nprog > 32) meta.nprog = 32;
    NEED(2);
    meta.ndaily = rd16(p);
    p += 2;
    NEED(meta.ndaily * 3);
    for (int i = 0; i < meta.ndaily; i++, p += 3)
        if (i < 7) meta.daily[i] = (DailyDef){p[0], p[1], p[2]};
    if (meta.ndaily > 7) meta.ndaily = 7;
    NEED(2);
    meta.nnames = rd16(p);
    p += 2;
    for (int i = 0; i < meta.nnames; i++) {
        NEED(1);
        int l = *p++;
        NEED(l);
        if (i < 16) {
            memcpy(meta.names[i], p, l < 15 ? l : 15);
            meta.names[i][l < 15 ? l : 15] = 0;
        }
        p += l;
    }
    if (meta.nnames > 16) meta.nnames = 16;
    NEED(26);
    meta.lb_min = rd32(p);
    meta.lb_max = rd32(p + 4);
    meta.lb_diff = rd32(p + 8);
    meta.lb_first = rd32(p + 12);
    meta.lb_top10 = rd32(p + 16);
    meta.lb_value = rd32(p + 20);
    p += 24;
    meta.ntop = rd16(p);
    p += 2;
    NEED(meta.ntop * 2);
    for (int i = 0; i < meta.ntop; i++, p += 2)
        if (i < 400) meta.top[i] = rd16(p);
    if (meta.ntop > 400) meta.ntop = 400;
    if (v2) {
        for (int t = 0; t < 2; t++) {
            NEED(2);
            int k = rd16(p);
            p += 2;
            NEED(k * 2);
            for (int i = 0; i < k; i++)
                if (i < MAX_SKINS) (t ? meta.bg_icon : meta.skin_icon)[i] = (int16_t)rd16(p + i * 2);
            p += k * 2;
        }
    } else {
        NEED(128);
        for (int i = 0; i < 32; i++) meta.skin_icon[i] = (int16_t)rd16(p + i * 2);
        for (int i = 0; i < 32; i++) meta.bg_icon[i] = (int16_t)rd16(p + 64 + i * 2);
        p += 128;
    }
    for (int i = 0; i < 44; i++) meta.prog_icon[i] = -1;
    if (v2) {
        NEED(2);
        int k = rd16(p);
        p += 2;
        NEED(k * 2);
        for (int i = 0; i < k; i++)
            if (i < 44) meta.prog_icon[i] = (int16_t)rd16(p + i * 2);
        p += k * 2;
    } else {
        NEED(44);
        for (int i = 0; i < 22; i++) meta.prog_icon[i] = (int16_t)rd16(p + i * 2);
        p += 44;
    }
    if (p + 2 <= end) {
        meta.npack = rd16(p);
        p += 2;
        for (int i = 0; i < meta.npack && p + 2 <= end; i++) {
            int id = p[0], timer = 0;
            if (v2) timer = *++p;
            int k = p[1];
            p += 2;
            if (p + k * 2 > end) break;
            if (i < 24) {
                meta.pack[i].id = id;
                meta.pack[i].timer = timer;
                meta.pack[i].n = k > 8 ? 8 : k;
                for (int j = 0; j < k && j < 8; j++) {
                    meta.pack[i].type[j] = p[j * 2];
                    meta.pack[i].qty[j] = p[j * 2 + 1];
                }
            }
            p += k * 2;
        }
        if (meta.npack > 24) meta.npack = 24;
    }
    if (v2 && p + 1 <= end) {
        int k = *p++;
        for (int i = 0; i < k && p < end; i++, p++)
            if (meta.nskin_order < MAX_SKINS) meta.skin_order[meta.nskin_order++] = *p;
        if (p < end) {
            k = *p++;
            for (int i = 0; i < k && p < end; i++, p++)
                if (meta.nbg_order < MAX_BGS) meta.bg_order[meta.nbg_order++] = *p;
        }
        if (p < end) {
            k = *p++;
            for (int i = 0; i < k && p + 2 <= end; i++, p += 2)
                if (meta.nignore < 64) meta.ignore[meta.nignore++] = rd16(p);
        }
        if (p < end) {
            k = *p++;
            for (int i = 0; i < k && p + 2 <= end; i++, p += 2)
                if (i < 32) meta.shop_ui[i] = (int16_t)rd16(p);
        }
    }
#undef NEED
    return 0;
}
