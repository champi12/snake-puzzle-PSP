/* gfx_host.c - el dibujo de gfx.h por software en el PC (capturas para comprobar el port sin consola)
 * Lee las texturas tal cual van a la PSP (paleta de 256 colores y orden "swizzled"), con filtro al vecino. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../src/gfx.h"

static uint32_t fb[SCR_W * SCR_H];
static const Tex *bound;

void gfx_init(void) { gstat.vram_fb = 2 * 512 * SCR_H * 2; gstat.dlist_max = 256 * 1024; }
void gfx_shutdown(void) {}
uint32_t gfx_vram_mark(void) { return 0; }
void gfx_vram_reset(uint32_t m) { (void)m; }
int gfx_vram_off;
int be_tex_upload(Tex *t) { gstat.ram_tex += t->bytes; gstat.ram_tex_n++; return 0; }
void tex_free(Tex *t) { memset(t, 0, sizeof(*t)); }

void gfx_begin(uint32_t clear)
{
    for (int i = 0; i < SCR_W * SCR_H; i++) fb[i] = clear;
    gstat.draws = gstat.verts = gstat.binds = 0;
}

float gfx_end(void) { return 0; }

void be_bind(const Tex *t) { bound = t; gstat.binds++; }

static uint32_t texel(const Tex *t, int x, int y)
{
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= t->w) x = t->w - 1;
    if (y >= t->h) y = t->h - 1;
    int bpp = t->fmt == TF_T8 ? 1 : t->fmt == TF_8888 ? 4 : 2;
    int wb = t->w * bpp, xb = x * bpp, off;
    if (t->swizzled) {
        int bx = xb / 16, by = y / 8;
        off = (by * (wb / 16) + bx) * 128 + (y % 8) * 16 + xb % 16;
    } else off = y * wb + xb;
    const uint8_t *p = (const uint8_t *)t->pixels + off;
    if (t->fmt == TF_T8) return t->clut[p[0]];
    if (t->fmt == TF_5650) {
        int v = p[0] | p[1] << 8;
        int r = (v & 31) << 3, g = ((v >> 5) & 63) << 2, b = ((v >> 11) & 31) << 3;
        return RGBA(r, g, b, 255);
    }
    if (t->fmt == TF_4444) {
        int v = p[0] | p[1] << 8;
        return RGBA((v & 15) * 17, ((v >> 4) & 15) * 17, ((v >> 8) & 15) * 17, ((v >> 12) & 15) * 17);
    }
    return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24;
}

static void blend(int x, int y, uint32_t c)
{
    if (x < 0 || y < 0 || x >= SCR_W || y >= SCR_H) return;
    uint32_t *d = &fb[y * SCR_W + x];
    int a = c >> 24;
    if (!a) return;
    int r = ((c & 255) * a + (*d & 255) * (255 - a)) / 255;
    int g = (((c >> 8) & 255) * a + ((*d >> 8) & 255) * (255 - a)) / 255;
    int b = (((c >> 16) & 255) * a + ((*d >> 16) & 255) * (255 - a)) / 255;
    *d = RGBA(r, g, b, 255);
}

static uint32_t modulate(uint32_t a, uint32_t b)
{
    return RGBA((a & 255) * (b & 255) / 255, ((a >> 8) & 255) * ((b >> 8) & 255) / 255,
                ((a >> 16) & 255) * ((b >> 16) & 255) / 255, (a >> 24) * (b >> 24) / 255);
}

static void tri(const float *a, const float *b, const float *c, uint32_t color)
{
    float minx = fminf(a[0], fminf(b[0], c[0])), maxx = fmaxf(a[0], fmaxf(b[0], c[0]));
    float miny = fminf(a[1], fminf(b[1], c[1])), maxy = fmaxf(a[1], fmaxf(b[1], c[1]));
    float den = (b[1] - c[1]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[1] - c[1]);
    if (fabsf(den) < 1e-6f) return;
    int x0 = (int)floorf(minx), x1 = (int)ceilf(maxx), y0 = (int)floorf(miny), y1 = (int)ceilf(maxy);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > SCR_W) x1 = SCR_W;
    if (y1 > SCR_H) y1 = SCR_H;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            float px = x + 0.5f, py = y + 0.5f;
            float w0 = ((b[1] - c[1]) * (px - c[0]) + (c[0] - b[0]) * (py - c[1])) / den;
            float w1 = ((c[1] - a[1]) * (px - c[0]) + (a[0] - c[0]) * (py - c[1])) / den;
            float w2 = 1 - w0 - w1;
            if (w0 < -1e-4f || w1 < -1e-4f || w2 < -1e-4f) continue;
            float u = w0 * a[2] + w1 * b[2] + w2 * c[2], v = w0 * a[3] + w1 * b[3] + w2 * c[3];
            blend(x, y, modulate(texel(bound, (int)floorf(u), (int)floorf(v)), color));
        }
}

void be_quad(const float *q, uint32_t color)
{
    if (!bound || !bound->pixels) return;
    tri(q, q + 4, q + 8, color);
    tri(q + 8, q + 4, q + 12, color);
    gstat.draws++;
    gstat.verts += 4;
}

void be_fill(float x, float y, float w, float h, uint32_t color)
{
    for (int yy = (int)y; yy < (int)(y + h); yy++)
        for (int xx = (int)x; xx < (int)(x + w); xx++) blend(xx, yy, color);
    gstat.draws++;
}

/* captura en PPM (tests/host_shot.py la pasa a PNG) */
void host_shot(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", SCR_W, SCR_H);
    for (int i = 0; i < SCR_W * SCR_H; i++) {
        uint8_t c[3] = {fb[i] & 255, (fb[i] >> 8) & 255, (fb[i] >> 16) & 255};
        fwrite(c, 1, 3, f);
    }
    fclose(f);
}
