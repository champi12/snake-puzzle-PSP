/* save.c - partida guardada en la carpeta del juego (save.dat) y datos de meta.bin */
#include <stdio.h>
#include <string.h>
#include "game.h"
#include "devlog.h"

#define SAVE_MAGIC 0x4b4e5350u   /* "PSNK" */
#define SAVE_VERSION 1

Save sv;
Meta meta;

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
}

int save_load(void)
{
    save_defaults();
    FILE *f = fopen("save.dat", "rb");
    if (!f) return -1;
    Save tmp;
    int ok = fread(&tmp, 1, sizeof(tmp), f) == sizeof(tmp) && tmp.magic == SAVE_MAGIC && tmp.version == SAVE_VERSION;
    fclose(f);
    if (!ok) {
        dlog("AVISO save.dat no valido: se empieza de cero");
        return -2;
    }
    sv = tmp;
    return 0;
}

void game_save(void)
{
    /* primero a un archivo aparte: si se apaga la consola a medias, el anterior sigue bien */
    FILE *f = fopen("save.tmp", "wb");
    if (!f) {
        dlog_error("no se pudo guardar la partida");
        return;
    }
    fwrite(&sv, 1, sizeof(sv), f);
    fclose(f);
    remove("save.dat");
    rename("save.tmp", "save.dat");
}

int meta_load(const char *path)
{
    memset(&meta, 0, sizeof(meta));
    for (int i = 0; i < 32; i++) meta.skin_icon[i] = meta.bg_icon[i] = -1;
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    static uint8_t b[8192];
    int n = fread(b, 1, sizeof(b), f);
    fclose(f);
    if (n < 8 || memcmp(b, "META", 4)) return -2;
    const uint8_t *p = b + 4;
    meta.nshop = rd16(p);
    p += 2;
    for (int i = 0; i < meta.nshop; i++, p += 5) {
        if (i >= 96) continue;
        meta.shop[i] = (ShopItemDef){p[0], p[1], p[2], p[3], p[4]};
    }
    if (meta.nshop > 96) meta.nshop = 96;
    meta.nprog = rd16(p);
    p += 2;
    for (int i = 0; i < meta.nprog; i++, p += 6)
        if (i < 32) meta.prog[i] = (ProgressDef){rd16(p), rd16(p + 2), p[4], p[5]};
    if (meta.nprog > 32) meta.nprog = 32;
    meta.ndaily = rd16(p);
    p += 2;
    for (int i = 0; i < meta.ndaily; i++, p += 3)
        if (i < 7) meta.daily[i] = (DailyDef){p[0], p[1], p[2]};
    if (meta.ndaily > 7) meta.ndaily = 7;
    meta.nnames = rd16(p);
    p += 2;
    for (int i = 0; i < meta.nnames; i++) {
        int l = *p++;
        if (i < 16) {
            memcpy(meta.names[i], p, l < 15 ? l : 15);
            meta.names[i][l < 15 ? l : 15] = 0;
        }
        p += l;
    }
    if (meta.nnames > 16) meta.nnames = 16;
    meta.lb_min = rd32(p);
    meta.lb_max = rd32(p + 4);
    meta.lb_diff = rd32(p + 8);
    meta.lb_first = rd32(p + 12);
    meta.lb_top10 = rd32(p + 16);
    meta.lb_value = rd32(p + 20);
    p += 24;
    meta.ntop = rd16(p);
    p += 2;
    for (int i = 0; i < meta.ntop; i++, p += 2)
        if (i < 400) meta.top[i] = rd16(p);
    if (meta.ntop > 400) meta.ntop = 400;
    for (int i = 0; i < 32; i++) meta.skin_icon[i] = (int16_t)rd16(p + i * 2);
    for (int i = 0; i < 32; i++) meta.bg_icon[i] = (int16_t)rd16(p + 64 + i * 2);
    p += 128;
    for (int i = 0; i < 22; i++) meta.prog_icon[i] = (int16_t)rd16(p + i * 2);
    return 0;
}
