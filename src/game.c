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
#include "lang.h"

Sheet world, skin_sheet, shop_sheet;
int16_t widx[160];
int nwidx;
Tex bg_tex, home_tex;
LevelPack levels;
int nskins, nbgs;
static uint64_t skin_files, bg_files;   /* que data/skinNN.spr y data/bgNN.tex hay (puede haber huecos) */
int gstate = GS_LOADING;
float gtime;
static int loaded_skin = -1, loaded_bg = -1;
static int font_ready;              /* la fuente ya cargada por la pantalla de carga */

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
    if (s == GS_SHOP || s == GS_DAILY || s == GS_REWARD || s == GS_WIN) shop_assets();
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

int skin_exists(int id) { return id >= 0 && id < MAX_SKINS && (skin_files >> id) & 1; }
int bg_exists(int id) { return id >= 0 && id < MAX_BGS && (bg_files >> id) & 1; }
int skin_available(int id) { return skin_exists(id) && (sv.skins_owned >> id) & 1; }
int bg_available(int id) { return bg_exists(id) && (sv.bgs_owned >> id) & 1; }
void own_skin(int id) { if (id >= 0 && id < MAX_SKINS) sv.skins_owned |= (uint64_t)1 << id; }
void own_bg(int id) { if (id >= 0 && id < MAX_BGS) sv.bgs_owned |= (uint64_t)1 << id; }
int level_skipped(int i) { return i >= 0 && i < MAX_LEVELS && (sv.skipped[i >> 3] >> (i & 7)) & 1; }
void level_set_skipped(int i, int on)
{
    if (i < 0 || i >= MAX_LEVELS) return;
    if (on) sv.skipped[i >> 3] |= 1 << (i & 7);
    else sv.skipped[i >> 3] &= ~(1 << (i & 7));
}

/* ---------------------------------------------------------------- bucle de niveles (UserLevelInfo, 14.2.9)
 * Con new_loop_logic (por defecto true), al pasar el ultimo nivel se sigue: "Nivel 267, 268..." juegan un nivel real
 * elegido al azar (SelectLevelFromPool) entre los que no estan en LevelConfig.ignoreLoopLevels, sin repetir los
 * ultimos 30 jugados (RecentPlayedLevels) ni los ya salidos en el bucle (LoopLevelHistory); si ya no queda ninguno,
 * vale cualquiera no ignorado (AddCandidateLevels sin comprobar), y si tampoco, GetFallbackRealLevel. Se eligen 10
 * por adelantado (PreGenerateFutureLevels). */
static int bit(const uint8_t *b, int i) { return i >= 0 && i < MAX_LEVELS && (b[i >> 3] >> (i & 7)) & 1; }

static int loop_ignored(int idx)
{
    if (idx < 0 || idx >= levels.count) return 1;
    int num = levels_num(&levels, idx);
    for (int i = 0; i < meta.nignore; i++)
        if (meta.ignore[i] == num) return 1;
    return 0;
}

static int is_recent(int idx)
{
    for (int i = 0; i < sv.nrecent; i++)
        if (sv.recent[i] == idx) return 1;
    return 0;
}

static void add_recent(int idx)
{
    if (idx < 0) return;
    if (sv.nrecent >= LOOP_RECENT) {
        memmove(sv.recent, sv.recent + 1, sizeof(sv.recent[0]) * (LOOP_RECENT - 1));
        sv.nrecent = LOOP_RECENT - 1;
    }
    sv.recent[sv.nrecent++] = idx;
}

static int loop_pick(void)
{
    static uint16_t cand[MAX_LEVELS];
    int n = 0;
    for (int pass = 0; pass < 2 && !n; pass++)
        for (int i = 0; i < levels.count; i++) {
            if (loop_ignored(i)) continue;
            if (!pass && (bit(sv.loop_used, i) || is_recent(i))) continue;
            cand[n++] = i;
        }
    if (!n) {   /* GetFallbackRealLevel */
        for (int i = 0; i < levels.count; i++)
            if (!loop_ignored(i)) return i;
        return levels.count - 1;
    }
    return cand[rand() % n];
}

int pos_is_loop(int pos) { return pos >= levels.count; }

/* numero que se ensena (Level_N del original): los niveles vacios del APK (101, 147, 167, 175) no se juegan pero no
 * corren la numeracion; el bucle sigue a partir de TOTAL_LEVEL (271, 272...) */
int pos_number(int pos)
{
    if (pos < 0) return 1;
    if (pos_is_loop(pos)) {
        int last = levels.count ? levels_num(&levels, levels.count - 1) : 0;
        return (last > TOTAL_LEVEL ? last : TOTAL_LEVEL) + pos - levels.count + 1;
    }
    return levels_num(&levels, pos);
}

static void loop_window(int pos)
{
    if (pos >= sv.loop_base && pos <= sv.loop_base + LOOP_AHEAD) return;
    if (pos > sv.loop_base && pos - sv.loop_base <= LOOP_AHEAD) {
        int k = pos - sv.loop_base;
        memmove(sv.loop_real, sv.loop_real + k, sizeof(sv.loop_real[0]) * (LOOP_AHEAD + 1 - k));
        for (int i = LOOP_AHEAD + 1 - k; i <= LOOP_AHEAD; i++) sv.loop_real[i] = 0xffff;
    } else
        for (int i = 0; i <= LOOP_AHEAD; i++) sv.loop_real[i] = 0xffff;
    sv.loop_base = pos;
}

static int loop_slot(int pos)
{
    uint16_t *r = &sv.loop_real[pos - sv.loop_base];
    if (*r >= levels.count || loop_ignored(*r)) {   /* TryGetValidLoopHistory / IsValidLoopRealLevel */
        *r = loop_pick();
        sv.loop_used[*r >> 3] |= 1 << (*r & 7);
        add_recent(*r);
        dlog("BUCLE nivel %d -> nivel real %d", pos_number(pos), levels_num(&levels, *r));
    }
    return *r;
}

int pos_level(int pos)
{
    if (pos < 0) return 0;
    if (!pos_is_loop(pos)) return pos;
    loop_window(pos);
    int r = loop_slot(pos);
    for (int k = 1; k <= LOOP_AHEAD && pos + k <= sv.loop_base + LOOP_AHEAD; k++) loop_slot(pos + k);
    return r;
}

void pos_advance(int pos)
{
    if (sv.cur_level > pos) return;
    sv.cur_level = pos + 1;
    /* UpdateLastLevel: un nivel normal entra en los recientes; en el bucle se eligen los siguientes */
    if (!pos_is_loop(sv.cur_level)) add_recent(sv.cur_level);
    else pos_level(sv.cur_level);
}

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
        if (type == 0) own_skin(id);
        else own_bg(id);
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

static int count_files(const char *fmt, int max, uint64_t *have)
{
    int n = 0;
    *have = 0;
    for (int i = 0; i < max; i++) {
        char p[64];
        snprintf(p, sizeof(p), fmt, i);
        FILE *f = fopen(p, "rb");
        if (!f) continue;
        fclose(f);
        *have |= (uint64_t)1 << i;
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
    loading_step(0.35f);
    if (meta_load("data/meta.bin")) dlog_error("falta data/meta.bin: sin tienda ni progreso");
    load_begin();
    /* world.idx: indices de los sprites y, al final, la primera pagina de los menus (van a la RAM) */
    int ui_page = -1;
    FILE *f = fopen("data/world.idx", "rb");
    if (f) {
        uint8_t b[2];
        if (fread(b, 1, 2, f) == 2) nwidx = b[0] | b[1] << 8;
        int n = nwidx;
        if (nwidx > 160) nwidx = 160;
        for (int i = 0; i < n; i++) {
            if (fread(b, 1, 2, f) != 2) break;
            if (i < 160) widx[i] = (int16_t)(b[0] | b[1] << 8);
        }
        if (fread(b, 1, 2, f) == 2) ui_page = (int16_t)(b[0] | b[1] << 8);
        fclose(f);
    }
    gfx_ram_from_page = ui_page;
    if (sheet_load(&world, "data/world.spr")) fatal_error("Falta data/world.spr");
    gfx_ram_from_page = -1;
    load_end("world.spr", world.bytes);
    dlog("world.spr: casillas y objetos en la VRAM, menus (desde la pagina %d) en la RAM", ui_page);
    loading_step(0.6f);
    if (!font_ready) {
        load_begin();
        if (font_load("data/font.spr", "data/font.idx")) dlog_error("falta la fuente (data/font.spr)");
        else { load_end("font.spr", 0); font_ready = 1; }
    }
    loading_step(0.7f);
    /* la pantalla de inicio y la tienda son de los menus: en la RAM, para dejar la VRAM al juego (atlas, fuente,
     * aspecto y fondo del nivel). La tienda se carga la primera vez que hace falta (shop_assets) */
    load_begin();
    gfx_ram_only = 1;
    if (bg_load(&home_tex, "data/home.tex") == 0) load_end("home.tex (RAM)", home_tex.bytes);
    gfx_ram_only = 0;
    loading_step(0.8f);
    nskins = count_files("data/skin%02d.spr", MAX_SKINS, &skin_files);
    nbgs = count_files("data/bg%02d.tex", MAX_BGS, &bg_files);
    srand(plat_us() ^ (unsigned)today_ymd());
    save_load();
    if (sv.cur_level > levels.count + 60000) sv.cur_level = levels.count;
    if (pos_is_loop(sv.cur_level)) pos_level(sv.cur_level);
    if (!skin_available(sv.skin)) sv.skin = 0;
    if (!bg_available(sv.bg)) sv.bg = 0;
    vram_dyn = gfx_vram_mark();
    load_skin(sv.skin);
    load_bg(sv.bg);
    loading_step(0.95f);
    audio_set_enabled(sv.sound, sv.music);
    audio_music(event_now() == 3);
    dlog("Juego: %d niveles, %d aspectos, %d fondos, %d articulos de tienda | partida: nivel %d, %d superados, %d pistas, "
         "%d saltos, %d fichas", levels.count, nskins, nbgs, meta.nshop, sv.cur_level + 1, levels_won_count(), sv.hints,
         sv.skips, sv.tokens);
    shop_unlock_check();
    set_state(GS_HOME);
}

/* ---------------------------------------------------------------- pantalla de carga (screens/loadatascene)
 * Background_Loading, Logo_v12, la barra (Progress2, Progress1 que se llena, Outline) y "LOADING..." con puntos
 * (LoadingDotsAnimation). Todo a 1/4 y en la RAM; se libera al terminar de cargar. */
static Sheet load_sheet;
static Tex load_tex;
static int load_steps;

void loading_step(float f)
{
    if (!load_tex.pixels && !load_sheet.nspr && !font_ready) return;
    gfx_begin(RGBA(170, 220, 235, 255));
    if (load_tex.pixels) gfx_bg(&load_tex, WHITE);
    gfx_sprite_fit(&load_sheet, 0, 240, 71, 196, 86, WHITE);
    float x = 240 - 237 / 2.0f, y = 181 - 13.5f / 2;
    /* Progress2 y Outline son 9-slice de 54x54 con bordes de 26 */
    gfx_sprite_hslice(&load_sheet, 2, x, y, 237, 13.5f, 26 / 54.0f, WHITE);
    gfx_sprite_rect_left(&load_sheet, 1, x + 1.5f, y + 1, 234, 11.5f, f, WHITE);
    gfx_sprite_hslice(&load_sheet, 3, x, y, 237, 13.5f, 26 / 54.0f, WHITE);
    if (font_ready) {
        static const char *dots[4] = {"", ".", "..", "..."};
        char b[32];
        snprintf(b, sizeof(b), "%s%s", T("CARGANDO"), dots[load_steps++ & 3]);
        text_draw(240 - text_width(FONT_L, T("CARGANDO")) / 2, 204 - text_height(FONT_L) / 2, FONT_L, b,
                  RGBA(92, 40, 20, 255), 0);
    }
    gfx_end();
}

void loading_begin(void)
{
    gfx_ram_only = 1;
    bg_load(&load_tex, "data/loading.tex");
    sheet_load(&load_sheet, "data/loading.spr");
    gfx_ram_only = 0;
    loading_step(0.02f);
    if (font_load("data/font.spr", "data/font.idx") == 0) font_ready = 1;
    loading_step(0.1f);
}

void loading_end(void)
{
    loading_step(1.0f);
    sheet_free(&load_sheet);
    tex_free(&load_tex);
    dlog_mem_reset();
}

/* iconos y graficos de la tienda (shop.spr): solo cuando se entra en una pantalla que los usa, y en la RAM */
void shop_assets(void)
{
    static int tried;
    if (tried) return;
    tried = 1;
    load_begin();
    gfx_ram_only = 1;
    if (sheet_load(&shop_sheet, "data/shop.spr") == 0) load_end("shop.spr (RAM)", shop_sheet.bytes);
    else dlog_error("no se pudo cargar data/shop.spr");
    gfx_ram_only = 0;
    dlog_mem_reset();
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
