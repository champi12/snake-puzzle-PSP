/* logic.h - reglas de Snake Puzzle traducidas del C# original (SnakeController, Stone, Trap, TeleportGate,
 * Barrie, Switch, GameLogicController; ver docs/DESCOMPILACION.md)
 *
 * El original mueve los objetos con DOTween y comprueba con Physics2D (rayos y circulos); aqui todo es por casillas,
 * que es lo que esos rayos miden: un rayo de 1 casilla desde la cabeza, un circulo de radio 0,2 bajo cada parte.
 */
#ifndef LOGIC_H
#define LOGIC_H
#include <stdint.h>
#include "level.h"

#define MAX_SNAKE 160
#define MAX_STONE 32
#define MAX_APPLE 48
#define MAX_GATE 16
#define MAX_BAR 8

typedef struct { int8_t x, y; } Cell;

/* todo lo que cambia durante la partida (cabe en una copia: deshacer = volver a una copia) */
typedef struct {
    int n;                     /* largo de la serpiente */
    Cell s[MAX_SNAKE];         /* s[0] es la cabeza */
    int dir;                   /* hacia donde mira la cabeza */
    int nstone;
    Cell st[MAX_STONE];
    uint8_t st_alive[MAX_STONE];
    uint8_t apple_alive[MAX_APPLE];
    uint8_t bar_len[MAX_BAR];  /* largo actual del haz de cada barrera */
    uint8_t dead;              /* 0, DEAD_TRAP, DEAD_FALL */
    uint8_t win;
} GState;

enum { DEAD_TRAP = 1, DEAD_FALL };

/* lo que no cambia: el nivel ya convertido a cuadricula */
typedef struct {
    int w, h;
    uint8_t solid[LV_MAXW * LV_MAXH];
    uint8_t kill[LV_MAXW * LV_MAXH];      /* pinchos y sierras */
    int gx, gy;
    int napple;
    Cell apple[MAX_APPLE];
    uint8_t apple_double[MAX_APPLE];
    int ngate;
    Cell gate[MAX_GATE];
    uint8_t gate_dir[MAX_GATE];           /* direccion en la que se entra (TeleportGate.gateDirection) */
    int8_t gate_partner[MAX_GATE];
    int nswitch;
    Cell sw[MAX_BAR];
    int8_t sw_bar[MAX_BAR];
    int nbar;
    Cell bar[MAX_BAR];                    /* agujero del que sale el haz */
    uint8_t bar_dir[MAX_BAR];
    uint8_t bar_max[MAX_BAR];             /* hasta la pared */
} GLevel;

/* resultado de un movimiento */
enum { MV_NONE = 0, MV_BLOCKED, MV_MOVED, MV_EAT, MV_PUSH, MV_TELE, MV_WIN, MV_DIE };

/* pasos para la animacion: la logica es instantanea, el dibujo recorre estas fotos */
#define MAX_STEPS 48
typedef struct {
    int kind;                  /* STEP_MOVE, STEP_FALL */
    GState st;
} GStep;
enum { STEP_MOVE = 0, STEP_FALL };

typedef struct {
    int n;
    GStep step[MAX_STEPS];
    int stone_landed;          /* alguna piedra cayo (sonido stone_drop) */
    int snake_fell;            /* la serpiente cayo (sonido drop) */
} GSteps;

void logic_load(GLevel *gl, GState *gs, const Level *L);
/* aplica la gravedad inicial (isFallWhenCreate) */
void logic_start(const GLevel *gl, GState *gs, GSteps *out);
int logic_move(const GLevel *gl, GState *gs, int dir, GSteps *out);
/* la serpiente esta en la casilla de entrada de un portal y el siguiente paso la saca por el otro */
int logic_in_gate(const GLevel *gl, const GState *gs);
int logic_gate_at(const GLevel *gl, int x, int y);
int logic_stone_at(const GState *gs, int x, int y);
int logic_apple_at(const GLevel *gl, const GState *gs, int x, int y);
int logic_beam_at(const GLevel *gl, const GState *gs, int x, int y);

#endif
