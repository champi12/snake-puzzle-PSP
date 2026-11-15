/* gfx_psp.c - dibujo con el GE de la PSP
 *
 * Pantalla de 16 bits (5650) con doble buffer y sin buffer Z (todo es 2D y se dibuja en orden): quedan 1,5 MB de
 * VRAM para las texturas. Las texturas que caben se copian a la VRAM (el GE las lee mas rapido); las demas se leen
 * de la RAM. Todas las texturas vienen "swizzled" y con paleta de 256 colores salvo los fondos (16 bits).
 */
#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspgu.h>
#include <pspge.h>
#include <string.h>
#include <malloc.h>
#include "gfx.h"
#include "devlog.h"

#define BUF_W 512
#define FB_BYTES (BUF_W * SCR_H * 2)
#define VRAM_SIZE (2 * 1024 * 1024)
#define DLIST_SIZE (256 * 1024)

static unsigned int __attribute__((aligned(64))) dlist[2][DLIST_SIZE / 4];
static int cur_list;
static uint32_t vram_top;           /* siguiente byte libre de la VRAM (relativo) */
static const Tex *bound;
static int texturing;

typedef struct {
    float u, v;
    uint32_t color;
    float x, y, z;
} Vtx;

void gfx_init(void)
{
    sceGuInit();
    sceGuStart(GU_DIRECT, dlist[0]);
    sceGuDrawBuffer(GU_PSM_5650, (void *)0, BUF_W);
    sceGuDispBuffer(SCR_W, SCR_H, (void *)FB_BYTES, BUF_W);
    sceGuOffset(2048 - SCR_W / 2, 2048 - SCR_H / 2);
    sceGuViewport(2048, 2048, SCR_W, SCR_H);
    sceGuScissor(0, 0, SCR_W, SCR_H);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDisable(GU_CULL_FACE);
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
    sceGuEnable(GU_TEXTURE_2D);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuTexFilter(GU_LINEAR, GU_LINEAR);
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
    sceGuEnable(GU_DITHER);
    sceGuShadeModel(GU_SMOOTH);
    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);
    vram_top = 2 * FB_BYTES;
    gstat.vram_fb = vram_top;
    gstat.dlist_max = DLIST_SIZE;
    texturing = 1;
}

void gfx_shutdown(void) { sceGuTerm(); }

void gfx_begin(uint32_t clear)
{
    sceGuStart(GU_DIRECT, dlist[cur_list]);
    sceGuClearColor(clear);
    sceGuClear(GU_COLOR_BUFFER_BIT);
    bound = NULL;
    gstat.draws = gstat.verts = gstat.binds = 0;
}

float gfx_end(void)
{
    int used = sceGuFinish();
    if (used > gstat.dlist_peak) gstat.dlist_peak = used;
    gstat.dlist_used = used;
    unsigned int t0 = sceKernelGetSystemTimeLow();
    DBG_PHASE(main, "gfx_end: esperando a que el GE termine (sceGuSync)");
    sceGuSync(0, 0);
    DBG_PHASE(main, "gfx_end: esperando al VBlank");
    sceDisplayWaitVblankStart();
    sceGuSwapBuffers();
    cur_list ^= 1;
    return (sceKernelGetSystemTimeLow() - t0) / 1000.0f;
}

static int log2i(int v)
{
    int r = 0;
    while ((1 << r) < v) r++;
    return r;
}

int be_tex_upload(Tex *t)
{
    /* la VRAM se reparte de abajo arriba y no se libera pieza a pieza: los fondos y los aspectos se cargan siempre
     * en el mismo hueco (ver gfx_vram_mark / gfx_vram_reset en main.c) */
    uint32_t need = (t->bytes + 63) & ~63u;
    uint32_t clut_bytes = t->fmt == TF_T8 ? 1024 : 0;
    if (!gfx_ram_only && vram_top + need + clut_bytes <= VRAM_SIZE) {
        uint8_t *dst = (uint8_t *)sceGeEdramGetAddr() + vram_top;
        if (clut_bytes) {
            /* la paleta se queda en la RAM (sceGuClutLoad la copia al GE); se mueven solo los indices */
            memcpy(dst, t->pixels, t->bytes - clut_bytes);
            t->pixels = dst;
            vram_top += (t->bytes - clut_bytes + 63) & ~63u;
        } else {
            memcpy(dst, t->pixels, t->bytes);
            t->pixels = dst;
            vram_top += need;
        }
        sceKernelDcacheWritebackAll();
        t->in_vram = 1;
        gstat.vram_tex = vram_top - gstat.vram_fb;
        gstat.vram_tex_n++;
        return 1;
    }
    sceKernelDcacheWritebackRange(t->pixels, t->bytes);
    if (t->clut) sceKernelDcacheWritebackRange(t->clut, 1024);
    gstat.ram_tex += t->bytes;
    gstat.ram_tex_n++;
    if (!gfx_ram_only)
        dlog("AVISO textura %dx%d sin sitio en VRAM (%u KB libres): se lee de la RAM", t->w, t->h,
             (unsigned)((VRAM_SIZE - vram_top) / 1024));
    return 0;
}

void tex_free(Tex *t)
{
    if (!t->in_vram && t->pixels) {
        gstat.ram_tex -= t->bytes;
        gstat.ram_tex_n--;
    }
    if (t->own) sceGuSync(0, 0);   /* que el GE no siga leyendola */
    free(t->own);
    memset(t, 0, sizeof(*t));
}

/* marcas para reutilizar la VRAM: lo que se carga despues de la marca se libera con el reset */
uint32_t gfx_vram_mark(void) { return vram_top; }
void gfx_vram_reset(uint32_t mark)
{
    sceGuSync(0, 0);
    vram_top = mark;
    gstat.vram_tex = vram_top - gstat.vram_fb;
}

void be_bind(const Tex *t)
{
    if (t == bound) return;
    bound = t;
    gstat.binds++;
    if (!texturing) {
        sceGuEnable(GU_TEXTURE_2D);
        texturing = 1;
    }
    if (t->fmt == TF_T8) {
        sceGuClutMode(GU_PSM_8888, 0, 0xff, 0);
        sceGuClutLoad(256 / 8, t->clut);
        sceGuTexMode(GU_PSM_T8, 0, 0, t->swizzled);
    } else
        sceGuTexMode(t->fmt == TF_5650 ? GU_PSM_5650 : t->fmt == TF_4444 ? GU_PSM_4444 : GU_PSM_8888, 0, 0, t->swizzled);
    sceGuTexImage(0, 1 << log2i(t->w), 1 << log2i(t->h), t->w, t->pixels);
    sceGuTexScale(1.0f, 1.0f);
    sceGuTexOffset(0.0f, 0.0f);
}

void be_quad(const float *q, uint32_t color)
{
    Vtx *v = sceGuGetMemory(4 * sizeof(Vtx));
    for (int k = 0; k < 4; k++) {
        v[k].u = q[k * 4 + 2];
        v[k].v = q[k * 4 + 3];
        v[k].color = color;
        v[k].x = q[k * 4 + 0];
        v[k].y = q[k * 4 + 1];
        v[k].z = 0;
    }
    sceGuDrawArray(GU_TRIANGLE_STRIP, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D, 4, 0, v);
    gstat.draws++;
    gstat.verts += 4;
}

void be_fill(float x, float y, float w, float h, uint32_t color)
{
    if (texturing) {
        sceGuDisable(GU_TEXTURE_2D);
        texturing = 0;
    }
    bound = NULL;
    struct { uint32_t c; float x, y, z; } *v = sceGuGetMemory(2 * 16);
    v[0].c = v[1].c = color;
    v[0].x = x;
    v[0].y = y;
    v[1].x = x + w;
    v[1].y = y + h;
    v[0].z = v[1].z = 0;
    sceGuDrawArray(GU_SPRITES, GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D, 2, 0, v);
    sceGuEnable(GU_TEXTURE_2D);
    texturing = 1;
    gstat.draws++;
    gstat.verts += 2;
}
