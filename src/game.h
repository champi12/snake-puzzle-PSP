/* game.h - estado del juego, pantallas y progreso (lo comun a play.c, ui.c y classic.c) */
#ifndef GAME_H
#define GAME_H
#include <stdint.h>
#include "gfx.h"
#include "level.h"
#include "logic.h"

/* pantallas (los nombres van al devlog: dbg_state_name) */
enum {
    GS_LOADING, GS_HOME, GS_SELECT, GS_PLAY, GS_PAUSE, GS_WIN, GS_LOSE, GS_SHOP, GS_DAILY, GS_SETTINGS, GS_CLASSIC,
    GS_CLASSIC_END, GS_REWARD, GS_COUNT
};

/* botones (los traduce main.c desde la PSP) */
enum {
    IN_UP = 1, IN_DOWN = 2, IN_LEFT = 4, IN_RIGHT = 8, IN_CROSS = 16, IN_CIRCLE = 32, IN_SQUARE = 64,
    IN_TRIANGLE = 128, IN_L = 256, IN_R = 512, IN_START = 1024, IN_SELECT = 2048
};

/* botones de la PSP en el texto (glifos de data/font.spr) */
#define BTN_X "\xe2\x9c\x95"
#define BTN_O "\xe2\x97\x8b"
#define BTN_SQ "\xe2\x96\xa1"
#define BTN_TRI "\xe2\x96\xb3"

#define MAX_LEVELS 300
#define MAX_SKINS 64               /* 14.2.9: aspectos 0..33 (29 sin graficos) y fondos 0..17 */
#define MAX_BGS 64
#define LOOP_AHEAD 10              /* UserLevelInfo.PreGenerateFutureLevels: 10 niveles del bucle por adelantado */
#define LOOP_RECENT 30             /* UserLevelInfo.AddToRecentLevels: se recuerdan los ultimos 30 */
#define TOTAL_LEVEL 270            /* GamePlayController.TOTAL_LEVEL */

/* partida guardada (save.c). La version 1 (port de la 2.3.4) se convierte al cargarla */
typedef struct {
    uint32_t magic, version;
    uint16_t cur_level;            /* posicion en curso: 0..niveles-1 los del juego, a partir de ahi el bucle */
    uint8_t won[MAX_LEVELS / 8 + 1];
    uint16_t hints, skips;
    uint16_t tokens;               /* fichas: sustituyen a "ver un anuncio" y a las compras del original */
    uint8_t skin, bg;
    uint64_t skins_owned, bgs_owned;
    uint8_t skin_paid[MAX_SKINS], bg_paid[MAX_BGS];   /* fichas puestas en cada articulo (AdWatchCount) */
    uint8_t sound, music;
    uint8_t daily_day;             /* dias ya cobrados (0..7) */
    uint32_t daily_last;           /* fecha del ultimo cobro (aaaammdd) */
    uint16_t classic_best;
    uint8_t progress_done;         /* tramos de progreso de aspectos ya entregados */
    uint8_t event_done;
    uint32_t total_wins;
    uint32_t play_seconds;
    /* 14.2.9 */
    uint8_t skipped[MAX_LEVELS / 8 + 1];   /* UserInfo.LEVEL_SKIP_DATA: manzana roja en el selector */
    uint16_t loop_base;                    /* bucle (UserLevelInfo): posicion de loop_real[0] */
    uint16_t loop_real[LOOP_AHEAD + 1];    /* nivel real de cada posicion del bucle (0xffff sin elegir) */
    uint8_t loop_used[MAX_LEVELS / 8 + 1]; /* niveles ya salidos en el bucle (LoopLevelHistory) */
    uint16_t recent[LOOP_RECENT];          /* RecentPlayedLevels */
    uint8_t nrecent;
    uint8_t pad[3];
} Save;
extern Save sv;

/* datos de meta.bin: tienda, progreso, recompensa diaria, ranking clasico y top de jugadores */
typedef struct { uint8_t id, type, state, need, event; } ShopItemDef;
typedef struct { uint16_t start, end; uint8_t id, type; } ProgressDef;
typedef struct { uint8_t kind, qty, value; } DailyDef;
typedef struct {
    int nshop;
    ShopItemDef shop[96];
    int nprog;
    ProgressDef prog[32];
    int ndaily;
    DailyDef daily[7];
    int nnames;
    char names[16][16];
    int lb_min, lb_max, lb_diff, lb_first, lb_top10, lb_value;
    int ntop;
    uint16_t top[400];             /* % de jugadores superados por nivel (x100) */
    int16_t skin_icon[MAX_SKINS], bg_icon[MAX_BGS];
    int16_t prog_icon[44];         /* siluetas del progreso: [id*2] vacia, [id*2+1] llena; +22 los fondos */
    int npack;                     /* paquetes de la tienda de pago (BasePackageItem), aqui con fichas */
    struct { uint8_t id, n, timer; uint8_t type[8], qty[8]; } pack[24];
    int nskin_order, nbg_order;    /* ShopRegionUI.sortIndex (lo que se ve en la tienda y en que orden) */
    uint8_t skin_order[MAX_SKINS], bg_order[MAX_BGS];
    int nignore;                   /* LevelConfig.ignoreLoopLevels: niveles que no salen en el bucle */
    uint16_t ignore[64];
    int16_t shop_ui[32];           /* graficos de la tienda de la 14.2.9 en shop.spr (SU_*) */
} Meta;
enum { RW_NOADS = 0, RW_HINT, RW_SKIP, RW_SKIN, RW_BG };
extern Meta meta;

/* recursos cargados */
extern Sheet world, skin_sheet, shop_sheet;
extern int16_t widx[160];
extern int nwidx;
extern Tex bg_tex, home_tex;
extern LevelPack levels;
extern int nskins, nbgs;
#define WSPR(i) ((i) < nwidx ? widx[i] : -1)

/* juego */
extern int gstate;
extern float gtime;
void game_init(void);
void game_input(unsigned in, unsigned held);
void game_update(float dt);
void game_draw(void);
void game_save(void);
void set_state(int s);

/* progreso (game.c) */
int level_won(int idx);
void level_set_won(int idx);
int levels_won_count(void);
int today_ymd(void);
int event_now(void);               /* 0 nada, 1 Halloween, 2 Navidad, 3 Ano Nuevo Lunar */
int skin_available(int id);        /* 1 se puede elegir */
int bg_available(int id);
int skin_exists(int id);           /* 1 si hay data/skinNN.spr (el 29 de la 14.2.9 no tiene graficos) */
int bg_exists(int id);
void own_skin(int id);
void own_bg(int id);
int level_skipped(int idx);
void level_set_skipped(int idx, int on);
/* bucle de niveles (UserLevelInfo de la 14.2.9): tras el ultimo nivel se sigue con niveles al azar */
int pos_level(int pos);            /* indice del nivel que se juega en la posicion pos */
int pos_is_loop(int pos);
int pos_number(int pos);           /* numero del nivel que se ensena (Level_N; en el bucle 271, 272...) */
void pos_advance(int pos);         /* pos superado o saltado: sv.cur_level pasa a la siguiente */
void load_skin(int id);
void load_bg(int id);
void shop_unlock_check(void);      /* desbloqueos por progreso (SkinProgress) */
int progress_next(int *start, int *end, int *id, int *type);
void play_sfx(int id);

/* nivel (play.c) */
void play_start(int pos, int hint_mode);   /* pos: posicion (ver pos_level) */
void play_input(unsigned in, unsigned held);
void play_skip(void);              /* salta el nivel en curso (gasta el salto quien llama) */
void play_update(float dt);
void play_draw(void);
void draw_snake_preview(float x, float y, float cs, int len);
void draw_snake_screen(const GState *st, const float *xs, const float *ys, float cell);
extern int snake_phase;
extern int play_idx;               /* indice del nivel (en levels.bin) */
extern int play_pos;               /* posicion (igual que play_idx salvo en el bucle) */
extern int play_hint;
extern Level cur_level;

/* pantallas (ui.c) */
void ui_input(unsigned in);
void ui_update(float dt);
void ui_draw(void);
void ui_message(const char *text, float secs);
void ui_reset_sel(void);
void draw_button(float x, float y, float w, float h, const char *label, int selected, int enabled);
void draw_panel(float x, float y, float w, float h);
void reward_popup(int type, int id);   /* GS_REWARD: aspecto o fondo nuevo */
void ui_open_shop(void);
void ui_win_begin(int first);         /* al superar un nivel: progreso de aspecto (si toca) y ventana de superado */              /* tienda unica, pestana de aspectos */
extern int ui_return_state;

/* modo clasico (classic.c) */
void classic_start(void);
void classic_input(unsigned in);
void classic_update(float dt);
void classic_draw(void);
void classic_end_draw(void);
void classic_end_input(unsigned in);

/* plataforma (main.c / tests) */
unsigned plat_ms(void);
unsigned plat_us(void);
void plat_date(int *y, int *m, int *d);
int plat_secs_of_day(void);         /* segundos desde las 00:00 (reloj de la consola) */
void plat_exit(void);
const char *plat_write_dir(void);   /* carpeta donde se puede escribir (partida, devlog) */

#endif
