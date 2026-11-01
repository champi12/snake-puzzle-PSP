/* game.c - recursos, progreso (niveles, aspectos, fondos, eventos) y reparto entre pantallas
 *
 * Lo que en el original depende de internet se sustituye por algo local:
 *   - "ver un anuncio" (ShopItem.NeededAdWatchCount, pistas extra) -> fichas que se ganan al superar niveles;
 *   - el ranking online del modo clasico -> los 10 rivales de ClassicLeaderboardData del propio APK;
 *   - la configuracion remota (Firebase) -> los valores por defecto que trae el APK.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game.h"
#include "audio.h"
#include "devlog.h"
#include "ui_ids.h"

Sheet world, skin_sheet, shop_sheet;
int16_t widx[160];
int nwidx;
Tex bg_tex, home_tex;
LevelPack levels;
int nskins, nbgs;
int gstate = GS_LOADING;
float gtime;
static int loaded_skin = -1, loaded_bg = -1;

extern int save_load(void);
extern int meta_load(const char *path);

static const char *state_names[GS_COUNT] = {"CARGA", "INICIO", "NIVELES", "JUGANDO", "PAUSA", "SUPERADO", "PERDIDO",
                                             "TIENDA", "DIARIO", "AJUSTES", "CLASICO", "CLASICO_FIN", "PREMIO"};
const char *dbg_state_name(int s) { return s >= 0 && s < GS_COUNT ? state_names[s] : "?"; }

void set_state(int s)
{
    /* las ventanas que salen encima del juego empiezan siempre por su primer boton */
    if (s != gstate && (s == GS_WIN || s == GS_LOSE || s == GS_PAUSE)) ui_reset_sel();
    int prev = gstate;
    gstate = s;
    dbg_game.state = s;
    /* puntos de control de memoria: deben dar lo mismo cada vez que se pasa por ellos */
    if (s != prev && (s == GS_HOME || s == GS_SHOP || s == GS_SELECT))
        dlog_mem(dbg_state_name(s));
}

void play_sfx(int id) { audio_play(id, 1.0f); }

/* ---------------------------------------------------------------- progreso */
int level_won(int i) { return i >= 0 && i < MAX_LEVELS && (sv.won[i >> 3] >> (i & 7)) & 1; }

void level_set_won(int i)
{
    if (i < 0 || i >= MAX_LEVELS) return;
    sv.won[i >> 3] |= 1 << (i & 7);
}

int levels_won_count(void)
{
    int n = 0;
    for (int i = 0; i < levels.count; i++) n += level_won(i);
    return n;
}

int today_ymd(void)
{
    int y, m, d;
    plat_date(&y, &m, &d);
    return y * 10000 + m * 100 + d;
}

/* fechas de los eventos (los iconos y articulos de Halloween, Navidad y Ano Nuevo Lunar del original) */
int event_now(void)
{
    int y, m, d;
    plat_date(&y, &m, &d);
    int md = m * 100 + d;
    if (md >= 1015 && md <= 1107) return 1;
    if (md >= 1210 || md <= 107) return 2;
    if (md >= 120 && md <= 220) return 3;
    return 0;
}

static const ShopItemDef *shop_def(int type, int id)
{
    for (int i = 0; i < meta.nshop; i++)
        if (meta.shop[i].type == type && meta.shop[i].id == id) return &meta.shop[i];
    return NULL;
}

int skin_available(int id) { return id >= 0 && id < 32 && (sv.skins_owned >> id) & 1; }
int bg_available(int id) { return id >= 0 && id < 32 && (sv.bgs_owned >> id) & 1; }

/* SkinProgress: cada tramo de niveles va llenando la barra de un aspecto (type 0) o un fondo (type 1) */
int progress_next(int *start, int *end, int *id, int *type)
{
    for (int i = 0; i < meta.nprog; i++) {
        if (i < sv.progress_done) continue;
        *start = meta.prog[i].start;
        *end = meta.prog[i].end;
        *id = meta.prog[i].id;
        *type = meta.prog[i].type;
        return i;
    }
    return -1;
}

void shop_unlock_check(void)
{
    int won = levels_won_count();
    for (int i = sv.progress_done; i < meta.nprog; i++) {
        if (won < meta.prog[i].end) break;
        int id = meta.prog[i].id, type = meta.prog[i].type;
        int had = type ? bg_available(id) : skin_available(id);
        if (type == 0) sv.skins_owned |= 1u << id;
        else sv.bgs_owned |= 1u << id;
        sv.progress_done = i + 1;
        /* solo se anuncia si de verdad es nuevo (p. ej. un aspecto de evento ya conseguido no se vuelve a anunciar) */
        if (!had) reward_popup(type, id);
        dlog("PREMIO por progreso: %s %d%s", type ? "fondo" : "aspecto", id, had ? " (ya lo tenia: sin aviso)" : "");
    }
    /* los articulos de evento ya no se regalan al abrir el juego: durante su evento se consiguen en la tienda con
     * fichas (ui.c, item_need) */
    (void)shop_def;
}

/* ---------------------------------------------------------------- recursos */
static unsigned load_t0;
static void load_begin(void) { load_t0 = plat_ms(); }
static void load_end(const char *what, uint32_t bytes)
{
    dbg_loads_frame++;
    mem_query(&dbg_mem);
    dlog("CARGA %-14s %6u KB en %4u ms | monton %u KB", what, (unsigned)(bytes / 1024), plat_ms() - load_t0,
         dbg_mem.heap_used / 1024);
}

extern uint32_t gfx_vram_mark(void);
extern void gfx_vram_reset(uint32_t mark);
static uint32_t vram_dyn;   /* a partir de aqui: aspecto y fondo (se recargan al cambiar) */
static uint32_t vram_bg;    /* a partir de aqui: el fondo */

void load_skin(int id)
{
    if (id == loaded_skin) return;
    char p[64];
    snprintf(p, sizeof(p), "data/skin%02d.spr", id);
    /* el aspecto y el fondo comparten el hueco del final de la VRAM: se recargan los dos */
    sheet_free(&skin_sheet);
    tex_free(&bg_tex);
    gfx_vram_reset(vram_dyn);
    load_begin();
    if (sheet_load(&skin_sheet, p)) {
        dlog_error("no se pudo cargar %s", p);
        if (id) {
            loaded_skin = -1;
            load_skin(0);
            return;
        }
    } else
        load_end(p, skin_sheet.bytes);
    vram_bg = gfx_vram_mark();
    loaded_skin = id;
    int b = loaded_bg;
    loaded_bg = -1;
    if (b >= 0) load_bg(b);
}

void load_bg(int id)
{
    if (id == loaded_bg) return;
    char p[64];
    snprintf(p, sizeof(p), "data/bg%02d.tex", id);
    tex_free(&bg_tex);
    if (loaded_skin >= 0) gfx_vram_reset(vram_bg);
    load_begin();
    if (bg_load(&bg_tex, p) == 0) load_end(p, bg_tex.bytes);
    else dlog_error("no se pudo cargar %s", p);
    loaded_bg = id;
}

static int count_files(const char *fmt, int max)
{
    int n = 0;
    for (int i = 0; i < max; i++) {
        char p[64];
        snprintf(p, sizeof(p), fmt, i);
        FILE *f = fopen(p, "rb");
        if (!f) continue;
        fclose(f);
        n = i + 1;
    }
    return n;
}

extern void fatal_error(const char *msg);

void game_init(void)
{
    load_begin();
    if (levels_open(&levels, "data/levels.bin")) fatal_error("Falta data/levels.bin");
    load_end("levels.bin", levels.size);
    if (meta_load("data/meta.bin")) dlog_error("falta data/meta.bin: sin tienda ni progreso");
    load_begin();
    if (sheet_load(&world, "data/world.spr")) fatal_error("Falta data/world.spr");
    load_end("world.spr", world.bytes);
    FILE *f = fopen("data/world.idx", "rb");
    if (f) {
        uint8_t b[2];
        if (fread(b, 1, 2, f) == 2) nwidx = b[0] | b[1] << 8;
        if (nwidx > 160) nwidx = 160;
        for (int i = 0; i < nwidx; i++) {
            if (fread(b, 1, 2, f) != 2) break;
            widx[i] = (int16_t)(b[0] | b[1] << 8);
        }
        fclose(f);
    }
    load_begin();
    if (font_load("data/font.spr", "data/font.idx")) dlog_error("falta la fuente (data/font.spr)");
    else load_end("font.spr", 0);
    load_begin();
    if (sheet_load(&shop_sheet, "data/shop.spr") == 0) load_end("shop.spr", shop_sheet.bytes);
    load_begin();
    if (bg_load(&home_tex, "data/home.tex") == 0) load_end("home.tex", home_tex.bytes);
    nskins = count_files("data/skin%02d.spr", 32);
    nbgs = count_files("data/bg%02d.tex", 32);
    save_load();
    if (sv.cur_level >= levels.count) sv.cur_level = levels.count - 1;
    if (!skin_available(sv.skin)) sv.skin = 0;
    if (!bg_available(sv.bg)) sv.bg = 0;
    vram_dyn = gfx_vram_mark();
    load_skin(sv.skin);
    load_bg(sv.bg);
    audio_set_enabled(sv.sound, sv.music);
    audio_music(event_now() == 3);
    dlog("Juego: %d niveles, %d aspectos, %d fondos, %d articulos de tienda | partida: nivel %d, %d superados, %d pistas, "
         "%d saltos, %d fichas", levels.count, nskins, nbgs, meta.nshop, sv.cur_level + 1, levels_won_count(), sv.hints,
         sv.skips, sv.tokens);
    shop_unlock_check();
    set_state(GS_HOME);
}

/* ---------------------------------------------------------------- bucle */
void game_input(unsigned in, unsigned held)
{
    switch (gstate) {
    case GS_PLAY: play_input(in, held); break;
    case GS_CLASSIC: classic_input(in); break;
    case GS_CLASSIC_END: classic_end_input(in); break;
    default: ui_input(in); break;
    }
}

void game_update(float dt)
{
    gtime += dt;
    static float secs;
    secs += dt;
    if (secs >= 1) {
        secs -= 1;
        sv.play_seconds++;
    }
    if (gstate == GS_PLAY || gstate == GS_PAUSE || gstate == GS_WIN || gstate == GS_LOSE) play_update(gstate == GS_PLAY ? dt : 0);
    if (gstate == GS_CLASSIC) classic_update(dt);
    ui_update(dt);
}

void game_draw(void)
{
    switch (gstate) {
    case GS_PLAY:
    case GS_PAUSE:
    case GS_WIN:
    case GS_LOSE:
        play_draw();
        break;
    case GS_CLASSIC:
        classic_draw();
        break;
    case GS_CLASSIC_END:
        classic_draw();
        classic_end_draw();
        break;
    default:
        break;
    }
    ui_draw();
}
