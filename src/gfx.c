/* gfx.c - lectura de hojas de sprites y fondos, y el calculo de vertices (comun a la PSP y al PC) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <malloc.h>
#include "gfx.h"
#include "devlog.h"

GfxStats gstat;

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static int rd16(const uint8_t *p) { return p[0] | p[1] << 8; }

static void *load_file(const char *path, uint32_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    /* alineado a 64: la paleta y las texturas los lee el GE directamente */
    void *m = memalign(64, n + 64);
    if (m && fread(m, 1, n, f) != (size_t)n) {
        free(m);
        m = NULL;
    }
    fclose(f);
    if (size) *size = n;
    return m;
}

int sheet_load(Sheet *s, const char *path)
{
    memset(s, 0, sizeof(*s));
    uint32_t size;
    uint8_t *m = load_file(path, &size);
    if (!m) return -1;
    if (memcmp(m, "SPR1", 4)) {
        free(m);
        return -2;
    }
    s->mem = m;
    s->bytes = size;
    s->npages = rd16(m + 4);
    s->nspr = rd16(m + 6);
    if (s->npages > 4) s->npages = 4;
    const uint8_t *p = m + 8;
    uint32_t sizes[4];
    for (int i = 0; i < s->npages; i++, p += 12) {
        Tex *t = &s->pages[i];
        t->w = rd16(p);
        t->h = rd16(p + 2);
        t->fmt = p[4] & 0x7f;
        t->swizzled = (p[4] & 0x80) != 0;
        sizes[i] = rd32(p + 8);
    }
    s->spr = malloc(sizeof(Spr) * (s->nspr ? s->nspr : 1));
    for (int i = 0; i < s->nspr; i++, p += 18) {
        Spr *r = &s->spr[i];
        r->page = rd16(p);
        r->x = rd16(p + 2);
        r->y = rd16(p + 4);
        r->w = rd16(p + 6);
        r->h = rd16(p + 8);
        r->ox = (int16_t)rd16(p + 10);
        r->oy = (int16_t)rd16(p + 12);
        r->uw = (int16_t)rd16(p + 14);
        r->uh = (int16_t)rd16(p + 16);
    }
    uint32_t off = (uint32_t)(p - m);
    off = (off + 15) & ~15u;
    for (int i = 0; i < s->npages; i++) {
        Tex *t = &s->pages[i];
        if (t->fmt == TF_T8) {
            t->clut = (uint32_t *)(m + off);
            t->pixels = m + off + 1024;
        } else
            t->pixels = m + off;
        t->bytes = sizes[i];
        off += (sizes[i] + 15) & ~15u;
        be_tex_upload(t);
    }
    return 0;
}

int gfx_ram_only;

void sheet_free(Sheet *s)
{
    for (int i = 0; i < s->npages; i++) tex_free(&s->pages[i]);
    free(s->spr);
    free(s->mem);
    memset(s, 0, sizeof(*s));
}

int bg_load(Tex *t, const char *path)
{
    memset(t, 0, sizeof(*t));
    uint32_t size;
    uint8_t *m = load_file(path, &size);
    if (!m) return -1;
    if (memcmp(m, "BG16", 4)) {
        free(m);
        return -2;
    }
    t->w = rd16(m + 4);
    t->h = rd16(m + 6);
    t->fmt = m[8] & 0x7f;
    t->swizzled = (m[8] & 0x80) != 0;
    /* los pixeles empiezan en el byte 12: se mueven al principio del bloque (alineado) */
    memmove(m, m + 12, size - 12);
    t->pixels = m;
    t->bytes = size - 12;
    /* si se copia a la VRAM, el bloque ya no hace falta; si no, la textura lo usa y tex_free lo libera */
    if (be_tex_upload(t)) free(m);
    else t->own = m;
    return 0;
}

/* ---------------------------------------------------------------- vertices */
static void quad(const Sheet *s, int id, float cx, float cy, float c, float sn, float scale, int flip, uint32_t color)
{
    if (!s || id < 0 || id >= s->nspr) return;
    const Spr *r = &s->spr[id];
    const Tex *t = &s->pages[r->page];
    float x0 = r->ox / 256.0f * scale, y0 = r->oy / 256.0f * scale;
    float x1 = x0 + r->uw / 256.0f * scale, y1 = y0 + r->uh / 256.0f * scale;
    int flipx = flip & 1, flipy = flip & 2;
    if (flipx) {
        float a = -x1;
        x1 = -x0;
        x0 = a;
    }
    if (flipy) {
        float a = -y1;
        y1 = -y0;
        y0 = a;
    }
    float u0 = r->x, v0 = r->y, u1 = r->x + r->w, v1 = r->y + r->h;
    if (flipx) {
        float a = u0;
        u0 = u1;
        u1 = a;
    }
    if (flipy) {
        float a = v0;
        v0 = v1;
        v1 = a;
    }
    /* tira: arriba-izq, abajo-izq, arriba-der, abajo-der; giro con y de pantalla hacia abajo */
    float lx[4] = {x0, x0, x1, x1}, ly[4] = {y0, y1, y0, y1};
    float uu[4] = {u0, u0, u1, u1}, vv[4] = {v0, v1, v0, v1};
    float v[16];
    for (int k = 0; k < 4; k++) {
        v[k * 4 + 0] = cx + lx[k] * c + ly[k] * sn;
        v[k * 4 + 1] = cy - lx[k] * sn + ly[k] * c;
        v[k * 4 + 2] = uu[k];
        v[k * 4 + 3] = vv[k];
    }
    be_bind(t);
    be_quad(v, color);
}

void gfx_sprite(const Sheet *s, int id, float x, float y, float scale, int rot, int flip, uint32_t color)
{
    static const float C[4] = {1, 0, -1, 0}, S[4] = {0, 1, 0, -1};
    quad(s, id, x, y, C[rot & 3], S[rot & 3], scale, flip, color);
}

void gfx_sprite_angle(const Sheet *s, int id, float x, float y, float scale, float ang, uint32_t color)
{
    quad(s, id, x, y, cosf(ang), sinf(ang), scale, 0, color);
}

void gfx_sprite_rect(const Sheet *s, int id, float x, float y, float w, float h, uint32_t color)
{
    if (!s || id < 0 || id >= s->nspr) return;
    const Spr *r = &s->spr[id];
    float v[16] = {x, y, r->x, r->y, x, y + h, r->x, r->y + r->h,
                   x + w, y, r->x + r->w, r->y, x + w, y + h, r->x + r->w, r->y + r->h};
    be_bind(&s->pages[r->page]);
    be_quad(v, color);
}

void gfx_sprite_rect_bottom(const Sheet *s, int id, float x, float y, float w, float h, float frac, uint32_t color)
{
    if (!s || id < 0 || id >= s->nspr || frac <= 0) return;
    if (frac > 1) frac = 1;
    const Spr *r = &s->spr[id];
    float y0 = y + h * (1 - frac), ty0 = r->y + r->h * (1 - frac);
    float v[16] = {x, y0, r->x, ty0, x, y + h, r->x, r->y + r->h,
                   x + w, y0, r->x + r->w, ty0, x + w, y + h, r->x + r->w, r->y + r->h};
    be_bind(&s->pages[r->page]);
    be_quad(v, color);
}

void gfx_sprite_rect_left(const Sheet *s, int id, float x, float y, float w, float h, float frac, uint32_t color)
{
    if (!s || id < 0 || id >= s->nspr || frac <= 0) return;
    if (frac > 1) frac = 1;
    const Spr *r = &s->spr[id];
    float x1 = x + w * frac, tx1 = r->x + r->w * frac;
    float v[16] = {x, y, r->x, r->y, x, y + h, r->x, r->y + r->h,
                   x1, y, tx1, r->y, x1, y + h, tx1, r->y + r->h};
    be_bind(&s->pages[r->page]);
    be_quad(v, color);
}

static void quad_uv(const Tex *t, float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, uint32_t c)
{
    float v[16] = {x0, y0, u0, v0, x0, y1, u0, v1, x1, y0, u1, v0, x1, y1, u1, v1};
    be_bind(t);
    be_quad(v, c);
}

void gfx_sprite_hslice(const Sheet *s, int id, float x, float y, float w, float h, float bf, uint32_t color)
{
    if (!s || id < 0 || id >= s->nspr) return;
    const Spr *r = &s->spr[id];
    const Tex *t = &s->pages[r->page];
    float cap = r->h ? h * bf * r->w / r->h : 0;   /* los extremos con su proporcion */
    if (cap * 2 > w) cap = w / 2;
    float ub = r->w * bf;
    quad_uv(t, x, y, x + cap, y + h, r->x, r->y, r->x + ub, r->y + r->h, color);
    quad_uv(t, x + cap, y, x + w - cap, y + h, r->x + ub, r->y, r->x + r->w - ub, r->y + r->h, color);
    quad_uv(t, x + w - cap, y, x + w, y + h, r->x + r->w - ub, r->y, r->x + r->w, r->y + r->h, color);
}

void gfx_sprite_fit(const Sheet *s, int id, float x, float y, float w, float h, uint32_t color)
{
    if (!s || id < 0 || id >= s->nspr) return;
    const Spr *r = &s->spr[id];
    float sw = r->w ? r->w : 1, sh = r->h ? r->h : 1;
    float k = w / sw < h / sh ? w / sw : h / sh;
    gfx_sprite_rect(s, id, x - sw * k / 2, y - sh * k / 2, sw * k, sh * k, color);
}

float spr_w(const Sheet *s, int id) { return (s && id >= 0 && id < s->nspr) ? s->spr[id].uw / 256.0f : 0; }
float spr_h(const Sheet *s, int id) { return (s && id >= 0 && id < s->nspr) ? s->spr[id].uh / 256.0f : 0; }

void gfx_fill(float x, float y, float w, float h, uint32_t color) { be_fill(x, y, w, h, color); }

void gfx_bg(const Tex *t, uint32_t color)
{
    if (!t || !t->pixels) return;
    /* en tiras de 64 texeles de ancho: el GE lee la textura mucho mas rapido que con un solo rectangulo */
    be_bind(t);
    for (int x = 0; x < SCR_W; x += 64) {
        int w = x + 64 > SCR_W ? SCR_W - x : 64;
        float v[16] = {x, 0, x, 0, x, SCR_H, x, SCR_H, x + w, 0, x + w, 0, x + w, SCR_H, x + w, SCR_H};
        be_quad(v, color);
    }
}

/* ---------------------------------------------------------------- texto */
static Sheet font_sheet;
static int font_nsizes, font_nchars;
static uint16_t font_sizes[8], font_chars[160];
static uint8_t font_adv[8 * 160];

int font_load(const char *spr_path, const char *idx_path)
{
    if (sheet_load(&font_sheet, spr_path)) return -1;
    FILE *f = fopen(idx_path, "rb");
    if (!f) return -2;
    uint8_t h[4];
    if (fread(h, 1, 4, f) != 4) {
        fclose(f);
        return -3;
    }
    font_nsizes = rd16(h);
    font_nchars = rd16(h + 2);
    if (font_nsizes > 8 || font_nchars > 160) {
        fclose(f);
        return -4;
    }
    uint8_t b[2];
    for (int i = 0; i < font_nsizes; i++) font_sizes[i] = fread(b, 1, 2, f) == 2 ? rd16(b) : 16;
    for (int i = 0; i < font_nchars; i++) font_chars[i] = fread(b, 1, 2, f) == 2 ? rd16(b) : '?';
    if (fread(font_adv, 1, font_nsizes * font_nchars, f) != (size_t)(font_nsizes * font_nchars)) {}
    fclose(f);
    return 0;
}

void font_free(void) { sheet_free(&font_sheet); }

static int utf8_next(const char **p)
{
    const unsigned char *s = (const unsigned char *)*p;
    int c = *s++;
    if (c >= 0xc0 && c < 0xe0 && *s) c = ((c & 0x1f) << 6) | (*s++ & 0x3f);
    else if (c >= 0xe0 && s[0] && s[1]) {
        c = ((c & 0x0f) << 12) | ((s[0] & 0x3f) << 6) | (s[1] & 0x3f);
        s += 2;
    }
    *p = (const char *)s;
    return c;
}

static int glyph(int c)
{
    for (int i = 0; i < font_nchars; i++)
        if (font_chars[i] == c) return i;
    return c == ' ' ? -1 : glyph('?');
}

int text_height(int size) { return size < font_nsizes ? font_sizes[size] + 4 : 16; }

float text_width(int size, const char *t)
{
    float w = 0;
    if (size >= font_nsizes) return 0;
    while (*t) {
        int c = utf8_next(&t);
        int g = glyph(c);
        w += g >= 0 ? font_adv[size * font_nchars + g] : font_sizes[size] / 3;
    }
    return w;
}

float text_draw(float x, float y, int size, const char *t, uint32_t color, int align)
{
    if (size >= font_nsizes || !font_sheet.nspr) return 0;
    float w = text_width(size, t);
    if (align == 1) x -= w * 0.5f;
    else if (align == 2) x -= w;
    x = (float)(int)x;
    y = (float)(int)y;
    while (*t) {
        int c = utf8_next(&t);
        int g = glyph(c);
        if (g < 0) {
            x += font_sizes[size] / 3;
            continue;
        }
        int id = size * font_nchars + g;
        const Spr *r = &font_sheet.spr[id];
        float v[16] = {x - 1, y - 2, r->x, r->y, x - 1, y - 2 + r->h, r->x, r->y + r->h,
                       x - 1 + r->w, y - 2, r->x + r->w, r->y, x - 1 + r->w, y - 2 + r->h, r->x + r->w, r->y + r->h};
        be_bind(&font_sheet.pages[r->page]);
        be_quad(v, color);
        x += font_adv[size * font_nchars + g];
    }
    return w;
}
