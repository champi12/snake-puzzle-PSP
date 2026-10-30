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
#define MAX_SKINS 32
#define MAX_BGS 32

/* partida guardada (save.c) */
typedef struct {
    uint32_t magic, version;
    uint16_t cur_level;            /* indice del nivel en curso (el siguiente sin superar) */
    uint8_t won[MAX_LEVELS / 8 + 1];
    uint16_t hints, skips;
    uint16_t tokens;               /* fichas: sustituyen a "ver un anuncio" del original */
    uint8_t skin, bg;
    uint32_t skins_owned, bgs_owned;
    uint8_t skin_paid[MAX_SKINS], bg_paid[MAX_BGS];   /* fichas puestas en cada articulo (AdWatchCount) */
    uint8_t sound, music;
    uint8_t daily_day;             /* dias ya cobrados (0..7) */
    uint32_t daily_last;           /* fecha del ultimo cobro (aaaammdd) */
    uint16_t classic_best;
    uint8_t progress_done;         /* tramos de progreso de aspectos ya entregados */
    uint8_t event_done;
    uint32_t total_wins;
    uint32_t play_seconds;
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
    int16_t skin_icon[32], bg_icon[32], prog_icon[22];
} Meta;
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
void load_skin(int id);
void load_bg(int id);
void shop_unlock_check(void);      /* desbloqueos por progreso (SkinProgress) */
int progress_next(int *start, int *end, int *id, int *type);
void play_sfx(int id);

/* nivel (play.c) */
void play_start(int idx, int hint_mode);
void play_input(unsigned in, unsigned held);
void play_update(float dt);
void play_draw(void);
void draw_snake_preview(float x, float y, float cs, int len);
extern int play_idx;
extern int play_hint;
extern Level cur_level;

/* pantallas (ui.c) */
void ui_input(unsigned in);
void ui_update(float dt);
void ui_draw(void);
void ui_message(const char *text, float secs);
void draw_button(float x, float y, float w, float h, const char *label, int selected, int enabled);
void draw_panel(float x, float y, float w, float h);
void reward_popup(int type, int id);   /* GS_REWARD: aspecto o fondo nuevo */
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
void plat_exit(void);

#endif
