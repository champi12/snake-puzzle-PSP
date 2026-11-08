/* gfx.h - dibujo 2D: hojas de sprites (*.spr de tools/extract.py), fondos, texto y estadisticas para el devlog
 *
 * Las mismas funciones se usan en la PSP (gfx_psp.c, con el GE) y en el PC (tests/gfx_host.c, por software, para
 * sacar capturas sin consola).
 */
#ifndef GFX_H
#define GFX_H
#include <stdint.h>

#define SCR_W 480
#define SCR_H 272

enum { TF_T8 = 0, TF_5650 = 1, TF_4444 = 2, TF_8888 = 3 };

typedef struct {
    int w, h, fmt, swizzled;
    void *pixels;           /* indices o colores */
    uint32_t *clut;         /* paleta (T8) */
    int in_vram;
    uint32_t bytes;
    void *own;              /* bloque de memoria propio (fondos): se libera con tex_free */
} Tex;

typedef struct {
    uint16_t page, x, y, w, h;   /* rectangulo en la pagina */
    int16_t ox, oy, uw, uh;      /* posicion y tamano respecto al punto de anclaje, en 1/256 de unidad (y hacia abajo) */
} Spr;

typedef struct {
    int npages;
    Tex pages[4];
    int nspr;
    Spr *spr;
    void *mem;
    uint32_t bytes;
} Sheet;

/* estadisticas del fotograma (devlog y medidor de SELECT) */
typedef struct {
    int draws, verts, binds;
    uint32_t vram_fb, vram_tex, ram_tex;
    int vram_tex_n, ram_tex_n;
    int dlist_used, dlist_peak, dlist_max;
} GfxStats;
extern GfxStats gstat;

int sheet_load(Sheet *s, const char *path);
void sheet_free(Sheet *s);
int bg_load(Tex *t, const char *path);
void tex_free(Tex *t);

void gfx_init(void);
void gfx_begin(uint32_t clear);
float gfx_end(void);           /* devuelve la espera al GE + VBlank (ms) */
void gfx_shutdown(void);

/* color: 0xAABBGGRR (como el GE) */
#define RGBA(r, g, b, a) ((uint32_t)(r) | ((uint32_t)(g) << 8) | ((uint32_t)(b) << 16) | ((uint32_t)(a) << 24))
#define WHITE 0xffffffffu

/* sprite con su anclaje en (x, y); scale = pixeles por unidad; rot = giros de 90 grados a la izquierda;
 * flip: FLIP_X y/o FLIP_Y (espejo antes del giro, como SpriteRenderer.flipX/flipY) */
#define FLIP_X 1
#define FLIP_Y 2
void gfx_sprite(const Sheet *s, int id, float x, float y, float scale, int rot, int flip, uint32_t color);
/* igual pero con un angulo cualquiera (radianes, a la izquierda) */
void gfx_sprite_angle(const Sheet *s, int id, float x, float y, float scale, float ang, uint32_t color);
/* sprite estirado a un rectangulo de pantalla */
void gfx_sprite_rect(const Sheet *s, int id, float x, float y, float w, float h, uint32_t color);
/* solo la parte de abajo (frac de 0 a 1) de un sprite estirado a un rectangulo: Image.fillAmount vertical */
void gfx_sprite_rect_bottom(const Sheet *s, int id, float x, float y, float w, float h, float frac, uint32_t color);
/* sprite centrado en (x, y) y escalado para caber en w x h sin deformarse */
void gfx_sprite_fit(const Sheet *s, int id, float x, float y, float w, float h, uint32_t color);
/* ancho y alto del sprite en unidades (para colocarlo) */
float spr_w(const Sheet *s, int id);
float spr_h(const Sheet *s, int id);
void gfx_fill(float x, float y, float w, float h, uint32_t color);
void gfx_bg(const Tex *t, uint32_t color);

/* texto (fuente del juego a 4 tamanos) */
enum { FONT_S = 0, FONT_M, FONT_L, FONT_XL };
int font_load(const char *spr_path, const char *idx_path);
void font_free(void);
/* align: 0 izquierda, 1 centro, 2 derecha */
float text_draw(float x, float y, int size, const char *utf8, uint32_t color, int align);
float text_width(int size, const char *utf8);
int text_height(int size);

/* ---- lo que pone cada plataforma ---- */
void be_bind(const Tex *t);
/* cuadrilatero texturizado: 4 vertices (x, y, u, v en texeles) en orden de tira de triangulos */
void be_quad(const float *xyuv, uint32_t color);
void be_fill(float x, float y, float w, float h, uint32_t color);
int be_tex_upload(Tex *t);     /* la PSP copia a la VRAM si cabe */

#endif
