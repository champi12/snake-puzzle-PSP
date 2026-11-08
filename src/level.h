/* level.h - niveles de data/levels.bin (los genera tools/extract.py a partir de los prefabs level/level_N)
 *
 * Coordenadas: casillas de la cuadricula de Unity con el origen desplazado a (0, 0); la y crece hacia arriba, como
 * en el juego original.
 */
#ifndef LEVEL_H
#define LEVEL_H
#include <stdint.h>

#define LV_MAXW 64
#define LV_MAXH 64
#define LV_MAXOBJ 256   /* el nivel 178 tiene 216 manzanas */
#define LV_MAXTILES 1200
#define LV_MAXHINT 256

/* tipos de objeto (como en tools/levels.py) */
enum { O_APPLE = 1, O_STONE, O_SPIKE, O_SAW, O_GATE, O_SWITCH, O_BARRIER, O_TUT };

/* direcciones del enum del original (ToolHint.AppendMovement): 0 arriba, 1 abajo, 2 izquierda, 3 derecha */
enum { D_UP = 0, D_DOWN, D_LEFT, D_RIGHT };
extern const int8_t DIR_DX[4], DIR_DY[4];
static inline int dir_opposite(int d) { return d ^ 1; }

/* SnakeController: como se coloca la serpiente al crearla (Initialize) */
enum {
    SF_FALL = 1, SF_DOWN = 2, SF_UP = 4, SF_LEFT = 8, SF_SECOND_RIGHT = 16, SF_SECOND_DOWN = 32, SF_SECOND_UP = 64
};

typedef struct {
    uint8_t x, y, layer, flags;   /* flags: 1 espejo X, 2 espejo Y, 4 girado 90, 8 capa delantera */
    uint16_t spr;                 /* sprite de world.spr */
} LvTile;

typedef struct {
    uint8_t type;
    int8_t x, y;
    uint8_t a;          /* manzana: doble | portal: direccion de entrada | barrera: direccion del haz | pinchos: giro/90 */
    int8_t b;           /* portal: pareja | interruptor: barrera que controla */
    uint16_t spr;       /* tutorial: sprite */
    int16_t px, py;     /* tutorial: posicion en 1/256 de casilla */
} LvObj;

typedef struct {
    int num;                      /* numero del nivel en el juego original */
    int x0, y0;                   /* origen en la cuadricula de Unity */
    int w, h;
    int flags;                    /* SF_* */
    int sx, sy, gx, gy;           /* serpiente (cabeza) y meta */
    uint8_t solid[LV_MAXW * LV_MAXH];
    int ntiles;
    LvTile tiles[LV_MAXTILES];
    int nobj;
    LvObj obj[LV_MAXOBJ];
    int nhint;
    char hint[LV_MAXHINT];        /* solucion oficial: '0'..'3' por movimiento */
} Level;

typedef struct {
    int count;
    uint8_t *blob;
    uint32_t size;
} LevelPack;

int levels_open(LevelPack *p, const char *path);   /* 0 si bien */
int levels_num(const LevelPack *p, int index);     /* numero del nivel (Level_N del original) */
int level_get(const LevelPack *p, int index, Level *out);
void levels_close(LevelPack *p);

#endif
