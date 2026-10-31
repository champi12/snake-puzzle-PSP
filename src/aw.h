/* aw.h - apartado de Apple Worm: sus 135 niveles con su interfaz (seleccion de nivel y de skins)
 *
 * Los datos los genera tools/appleworm.py en data/aw/ a partir del APK de Apple Worm del usuario; si no estan, el
 * apartado no aparece en el inicio.
 */
#ifndef AW_H
#define AW_H
#include "gfx.h"
#include "level.h"

/* orden fijo de data/aw/aw.idx (tools/appleworm.py) */
enum {
    AW_BLOCK0 = 0,                 /* + mascara de vecinos: arriba 8, derecha 4, abajo 2, izquierda 1 */
    AW_APPLE = 16, AW_SAW, AW_SPIKES_F, AW_SPIKES_B, AW_STONE, AW_PORTAL_BG, AW_PORTAL_SW,
    AWU_WINDOW_LEV, AWU_BTN_LEV, AWU_BTN_LEV_PRESS, AWU_PLACE_LEVEL, AWU_PLACE_LEVEL_SKIN, AWU_STAR_L, AWU_STAR_C,
    AWU_STAR_R, AWU_STAR_L_G, AWU_STAR_C_G, AWU_STAR_R_G, AWU_LOCK, AWU_ARROW_L, AWU_ARROW_R, AWU_BACK,
    AWU_SHOP_WINDOW, AWU_SKINS, AWU_CORNER_SKIN, AWU_CHECK, AWU_COIN, AWU_BTN_GREEN, AWU_BTN_GR_BIG,
    AWU_PREVIEW0
};

extern int aw_mode;                /* 1 mientras se juega un nivel de Apple Worm */
extern LevelPack aw_levels;
extern Sheet aw_sheet;
extern Tex aw_bg;

int aw_available(void);            /* hay data/aw */
int aw_open(void);                 /* carga lo que falte; 0 si bien */
int aw_spr(int i);
int aw_level_won(int i);
void aw_level_set_won(int i);
int aw_levels_won_count(void);
void aw_on_win(int idx, int first); /* monedas del nivel y skins que se desbloquean */
void aw_select_draw(void);
void aw_select_input(unsigned in);
void aw_skins_draw(void);
void aw_skins_input(unsigned in);
void aw_enter(void);               /* desde el inicio: a la seleccion de nivel */
void aw_enter_pack(int pack);      /* seleccion de nivel con la interfaz de Apple Worm: 0 Snake Puzzle, 1 Apple Worm */
int aw_ui_ok(void);                /* hay graficos de Apple Worm para la seleccion de nivel */

#endif
