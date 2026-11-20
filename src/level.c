/* level.c - lectura de data/levels.bin
 * Todo se lee byte a byte: en la PSP real leer un entero de una direccion no alineada cuelga la consola. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "level.h"
#include "file.h"

const int8_t DIR_DX[4] = {0, 0, -1, 1};
const int8_t DIR_DY[4] = {1, -1, 0, 0};

static uint32_t u32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static int u16(const uint8_t *p) { return p[0] | p[1] << 8; }
static int s16(const uint8_t *p) { return (int16_t)(p[0] | p[1] << 8); }

int levels_open(LevelPack *p, const char *path)
{
    memset(p, 0, sizeof(*p));
    uint32_t n;
    p->blob = file_load(path, &n);
    if (!p->blob) return -1;
    p->size = n;
    if (n < 8 || memcmp(p->blob, "SNL1", 4)) return -3;
    p->count = u32(p->blob + 4);
    return 0;
}

int levels_num(const LevelPack *pk, int index)
{
    if (index < 0 || index >= pk->count) return 0;
    uint32_t off = u32(pk->blob + 8 + 4 * index);
    if (off + 2 > (uint32_t)pk->size) return 0;
    return u16(pk->blob + off);
}

void levels_close(LevelPack *p)
{
    free(p->blob);
    memset(p, 0, sizeof(*p));
}

int level_get(const LevelPack *pk, int index, Level *L)
{
    if (index < 0 || index >= pk->count) return -1;
    const uint8_t *p = pk->blob + u32(pk->blob + 8 + 4 * index);
    const uint8_t *end = pk->blob + pk->size;
    memset(L, 0, sizeof(*L));
    L->num = u16(p);
    L->x0 = s16(p + 2);
    L->y0 = s16(p + 4);
    L->w = p[6];
    L->h = p[7];
    L->flags = p[8];
    L->sx = (int8_t)p[10];
    L->sy = (int8_t)p[11];
    L->gx = (int8_t)p[12];
    L->gy = (int8_t)p[13];
    p += 14;
    if (L->w > LV_MAXW || L->h > LV_MAXH) return -2;
    for (int y = 0; y < L->h; y++)
        for (int x = 0; x < L->w; x++) L->solid[y * LV_MAXW + x] = p[y * L->w + x];
    p += L->w * L->h;
    int nt = u16(p);
    p += 2;
    for (int i = 0; i < nt && p + 6 <= end; i++, p += 6) {
        if (L->ntiles >= LV_MAXTILES) continue;
        LvTile *t = &L->tiles[L->ntiles++];
        t->x = p[0];
        t->y = p[1];
        t->layer = p[2];
        t->flags = p[3];
        t->spr = u16(p + 4);
    }
    int no = u16(p);
    p += 2;
    for (int i = 0; i < no && p + 12 <= end; i++, p += 12) {
        if (L->nobj >= LV_MAXOBJ) continue;
        LvObj *o = &L->obj[L->nobj++];
        o->type = p[0];
        o->x = (int8_t)p[1];
        o->y = (int8_t)p[2];
        o->a = p[3];
        o->b = (int8_t)p[4];
        o->spr = u16(p + 6);
        o->px = s16(p + 8);
        o->py = s16(p + 10);
    }
    int nh = u16(p);
    p += 2;
    if (nh >= LV_MAXHINT) nh = LV_MAXHINT - 1;
    memcpy(L->hint, p, nh);
    L->nhint = nh;
    return 0;
}
