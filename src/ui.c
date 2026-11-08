/* ui.c - pantallas: inicio, niveles, pausa, superado, perdido, tienda, recompensa diaria, ajustes y premios
 *
 * Todo con los botones de la PSP: cruceta o stick para elegir, X aceptar, O volver, L/R cambiar de pagina o pestana.
 * Los dibujos son los del juego (Logo, Btn_*, Level_Bas, Slot_*, Popup_*, Banner_*, iconos de la tienda...).
 */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "game.h"
#include "lang.h"
#include "audio.h"
#include "devlog.h"
#include "ui_ids.h"
#include "skin.h"

#define TXT_DARK RGBA(105, 58, 28, 255)   /* texto sobre los paneles claros (base_popup) */

int ui_return_state = GS_HOME;
static int sel;                    /* opcion elegida en la pantalla actual */
static int page;
static int shop_tab;               /* 0 aspectos, 1 fondos, 2 packs (la tienda de pago del original, con fichas) */
static char msg[128];
static float msg_t;
static int reward_q[8][2], reward_n;
static int confirm_reset;

/* orden de la tienda: ShopRegionUI.sortIndex de screens/shop (meta.bin); si el meta.bin es antiguo, el de la 2.3.4 */
static const uint8_t SKIN_ORDER_OLD[] = {0, 20, 2, 15, 21, 27, 23, 28, 16, 10, 17, 3, 4, 22, 6, 11, 13, 7, 1, 18, 8, 9, 19,
                                         12, 5, 14, 24, 25, 26};
static const uint8_t BG_ORDER_OLD[] = {0, 1, 11, 8, 9, 12, 2, 13, 7, 6, 10, 4, 5, 3};
static int shop_list[MAX_SKINS], shop_count;

static int skin_icon(int id) { return id >= 0 && id < MAX_SKINS ? meta.skin_icon[id] : -1; }
static int bg_icon(int id) { return id >= 0 && id < MAX_BGS ? meta.bg_icon[id] : -1; }

void ui_reset_sel(void) { sel = 0; }

void ui_message(const char *text, float secs)
{
    snprintf(msg, sizeof(msg), "%s", text);
    msg_t = secs;
}

void reward_popup(int type, int id)
{
    if (reward_n < 8) {
        reward_q[reward_n][0] = type;
        reward_q[reward_n][1] = id;
        reward_n++;
    }
    if (gstate != GS_REWARD) {
        ui_return_state = gstate == GS_LOADING ? GS_HOME : gstate;
        set_state(GS_REWARD);
        play_sfx(SFX_UNLOCK);
    }
}

/* ---------------------------------------------------------------- piezas */
void draw_panel(float x, float y, float w, float h)
{
    int id = WSPR(UI_BASE_POPUP);
    if (id >= 0) gfx_sprite_rect(&world, id, x, y, w, h, WHITE);
    else gfx_fill(x, y, w, h, RGBA(40, 30, 60, 230));
}

void draw_button(float x, float y, float w, float h, const char *label, int selected, int enabled)
{
    int id = WSPR(enabled ? (selected ? UI_BTN_GREEN : UI_BTN_BLUE) : UI_BTN_GRAY);
    float grow = selected ? 1.0f + 0.04f * sinf(gtime * 6) : 1.0f;
    float cw = w * grow, ch = h * grow;
    if (id >= 0) gfx_sprite_rect(&world, id, x + (w - cw) / 2, y + (h - ch) / 2, cw, ch, WHITE);
    else gfx_fill(x, y, w, h, selected ? RGBA(80, 200, 80, 255) : RGBA(60, 90, 200, 255));
    text_draw(x + w / 2, y + h / 2 - text_height(FONT_M) / 2 + 1, FONT_M, label, WHITE, 1);
}

static void counters(float y)
{
    char b[32];
    gfx_sprite_fit(&world, WSPR(UI_ICON_HINT), 14, y + 9, 20, 20, WHITE);
    snprintf(b, sizeof(b), "%d", sv.hints);
    text_draw(28, y, FONT_M, b, WHITE, 0);
    gfx_sprite_fit(&world, WSPR(UI_ICON_SKIPLEVEL), 64, y + 9, 20, 20, WHITE);
    snprintf(b, sizeof(b), "%d", sv.skips);
    text_draw(78, y, FONT_M, b, WHITE, 0);
    gfx_sprite_fit(&world, WSPR(UI_STAR), 114, y + 9, 20, 20, WHITE);
    snprintf(b, sizeof(b), T("%d fichas"), sv.tokens);
    text_draw(128, y, FONT_M, b, WHITE, 0);
}

static void help(const char *t) { text_draw(SCR_W / 2, SCR_H - 16, FONT_S, t, RGBA(255, 255, 255, 220), 1); }

static int daily_ready(void) { return meta.ndaily > 0 && (unsigned)today_ymd() != sv.daily_last; }

/* ---------------------------------------------------------------- piezas de las pantallas de la 14.2.9
 * Las pantallas del original son de 1920x1080: en la PSP todo va a 1/4 (480x270), con las mismas posiciones. */
static void wrect(int ui, float cx, float cy, float w, float h, uint32_t c)
{
    int id = WSPR(ui);
    if (id >= 0) gfx_sprite_rect(&world, id, cx - w / 2, cy - h / 2, w, h, c);
}
static void wfit(int ui, float cx, float cy, float w, float h, uint32_t c)
{
    int id = WSPR(ui);
    if (id >= 0) gfx_sprite_fit(&world, id, cx, cy, w, h, c);
}
static int sui(int k) { return k >= 0 && k < 32 ? meta.shop_ui[k] : -1; }
static void srect(int k, float cx, float cy, float w, float h, uint32_t c)
{
    int id = sui(k);
    if (id >= 0) gfx_sprite_rect(&shop_sheet, id, cx - w / 2, cy - h / 2, w, h, c);
}
static void sfit(int k, float cx, float cy, float w, float h, uint32_t c)
{
    int id = sui(k);
    if (id >= 0) gfx_sprite_fit(&shop_sheet, id, cx, cy, w, h, c);
}
static float pulse(int on) { return on ? 1.08f + 0.04f * sinf(gtime * 6) : 1.0f; }
static int secs_to_midnight(void) { return 86400 - plat_secs_of_day(); }
static void hms(char *b, int n, int s)
{
    if (s < 0) s = 0;
    snprintf(b, n, "%02d:%02d:%02d", s / 3600, s / 60 % 60, s % 60);
}

/* cruceta entre botones colocados libremente: el mas cercano en esa direccion */
typedef struct { float x, y; int on; } NavPt;
static int nav_move(const NavPt *p, int n, int cur, unsigned in)
{
    int dx = in & IN_LEFT ? -1 : in & IN_RIGHT ? 1 : 0, dy = in & IN_UP ? -1 : in & IN_DOWN ? 1 : 0;
    if (!dx && !dy) return cur;
    int best = cur;
    float bs = 1e9f;
    for (int i = 0; i < n; i++) {
        if (!p[i].on || i == cur) continue;
        float ex = p[i].x - p[cur].x, ey = p[i].y - p[cur].y;
        float along = dx ? ex * dx : ey * dy, perp = dx ? fabsf(ey) : fabsf(ex);
        if (along <= 4) continue;
        float sc = along + perp * 2;
        if (sc < bs) { bs = sc; best = i; }
    }
    return best;
}

static int pack_valid(int k);
static int pack_index(int id)
{
    for (int k = 0; k < meta.npack; k++)
        if (meta.pack[k].id == id) return k;
    return -1;
}
static int limited_pack(void)
{
    for (int k = 0; k < meta.npack; k++)
        if (meta.pack[k].timer) return k;
    return -1;
}
static void open_packs(int k);

/* ---------------------------------------------------------------- inicio (HomeController, screens/home de la 14.2.9)
 * Logo_v12 arriba; abajo el banner del paquete de temporada (BundleScrollController, va pasando solo), el boton de
 * jugar con "Lv.N" y "Snake Retro" con su cuenta atras; arriba a la derecha la oferta limitada, aspectos y ajustes, y
 * debajo la tienda y el diario. NIVELES (el selector de manzanas) va como boton extra debajo del diario. */
enum { HM_BUNDLE, HM_PLAY, HM_RETRO, HM_OFFER, HM_SKIN, HM_SETTINGS, HM_SHOP, HM_DAILY, HM_LEVELS, HM_N };
static const float HM_XY[HM_N][2] = {{86, 170}, {241, 178}, {393, 178}, {345, 22}, {417, 22},
                                     {455, 22}, {455, 63}, {455, 100}, {24, 24}};
static const char *HM_NAME[HM_N] = {"Paquete de temporada", "Jugar", "Snake Retro (modo clasico)", "Oferta limitada",
                                    "Aspectos y fondos", "Ajustes", "Tienda", "Recompensa diaria", "Niveles"};
static int home_sel = HM_PLAY;
static float bundle_t;
static int bundle_k;
static const int SEASON_ID[4] = {11, 13, 14, 15};
static const int SEASON_BANNER[4] = {UI_BANNER_LUNAR, UI_BANNER_SUMMER, UI_BANNER_PRIDE, UI_BANNER_WORLDCUP};

static int seasons(int *ks)
{
    int n = 0;
    for (int i = 0; i < 4; i++)
        if (pack_index(SEASON_ID[i]) >= 0) ks[n++] = i;
    return n;
}

static void home_nav(NavPt *p)
{
    int ks[4];
    for (int i = 0; i < HM_N; i++) p[i] = (NavPt){HM_XY[i][0], HM_XY[i][1], 1};
    p[HM_BUNDLE].on = seasons(ks) > 0 && WSPR(UI_BANNER_LUNAR) >= 0;
    p[HM_OFFER].on = limited_pack() >= 0 && pack_valid(limited_pack());
}

static void home_draw(void)
{
    gfx_bg(home_tex.pixels ? &home_tex : &bg_tex, WHITE);
    NavPt p[HM_N];
    home_nav(p);
    if (!p[home_sel].on) home_sel = HM_PLAY;
    char b[48];
    if (WSPR(UI_LOGO) >= 0) wfit(UI_LOGO, 241, 44, 150, 66, WHITE);
    else text_draw(241, 30, FONT_XL, "Snake Puzzle", WHITE, 1);
    /* paquete de temporada */
    int ks[4], ns = seasons(ks);
    if (p[HM_BUNDLE].on) {
        int i = ks[bundle_k % ns], k = pack_index(SEASON_ID[i]);
        float s = pulse(home_sel == HM_BUNDLE);
        wrect(SEASON_BANNER[i], 86, 170, 116 * s, 77 * s, WHITE);
        if (!pack_valid(k)) wrect(UI_TAG_SOLDOUT, 86, 167, 77, 34, WHITE);
        else {
            wrect(UI_TAG_SALEOFF, 142, 139, 22, 22, WHITE);
            text_draw(142, 139 - text_height(FONT_S) / 2, FONT_S, T("NUEVO"), WHITE, 1);
        }
        for (int j = 0; j < ns; j++)
            gfx_fill(86 - (ns - 1) * 5 + j * 10 - 2, 214, 5, 5, j == bundle_k % ns ? WHITE : RGBA(255, 255, 255, 100));
    }
    /* jugar */
    float s = pulse(home_sel == HM_PLAY);
    wrect(UI_BTN_PLAY, 241, 178, 120 * s, 75 * s, WHITE);
    snprintf(b, sizeof(b), "Lv.%d", sv.cur_level + 1);
    text_draw(241, 132, FONT_L, b, WHITE, 1);
    /* Snake Retro */
    s = pulse(home_sel == HM_RETRO);
    wrect(UI_BTN_MINIGAME, 393, 178, 116 * s, 69 * s, WHITE);
    text_draw(393, 132, FONT_L, "Snake Retro", WHITE, 1);
    wrect(UI_BASE_TIME, 393, 209, 58, 22, WHITE);
    hms(b, sizeof(b), secs_to_midnight());
    text_draw(393, 209 - text_height(FONT_S) / 2, FONT_S, b, WHITE, 1);
    /* botones redondos */
    if (p[HM_OFFER].on) {
        s = pulse(home_sel == HM_OFFER);
        wrect(UI_ICON_LIMITEDOFFER, 345, 22, 32 * s, 26 * s, WHITE);
        wrect(UI_BASE_TIME, 344, 36, 29, 9, WHITE);
        text_draw(344, 36 - text_height(FONT_S) / 2, FONT_S, T("OFERTA"), WHITE, 1);
    }
    s = pulse(home_sel == HM_SKIN);
    wrect(UI_BTN_SKIN, 417, 22, 31 * s, 31 * s, WHITE);
    s = pulse(home_sel == HM_SETTINGS);
    wrect(UI_BTN_SETTINGS, 455, 22, 31 * s, 31 * s, WHITE);
    s = pulse(home_sel == HM_SHOP);
    wrect(UI_ICON_SHOP, 455, 63, 31 * s, 31 * s, WHITE);
    s = pulse(home_sel == HM_DAILY);
    wrect(UI_ICON_DAILY, 455, 100, 31 * s, 31 * s, WHITE);
    if (daily_ready()) wrect(UI_ICON_NOTI, 465, 90, 9, 9, WHITE);
    else {
        wrect(UI_BG_MASK, 455, 114, 30, 9, WHITE);
        hms(b, sizeof(b), secs_to_midnight());
        text_draw(455, 114 - text_height(FONT_S) / 2, FONT_S, b, WHITE, 1);
    }
    s = pulse(home_sel == HM_LEVELS);
    if (WSPR(UI_APPLE_GREEN) >= 0) wfit(UI_APPLE_GREEN, 24, 22, 28 * s, 30 * s, WHITE);
    text_draw(24, 37, FONT_S, T("NIVELES"), WHITE, 1);
    counters(SCR_H - 40);
    snprintf(b, sizeof(b), "%s   " BTN_X " %s", T(HM_NAME[home_sel]), T("Aceptar"));
    help(b);
#if DEVLOG
    {
        /* para saber sin buscar archivos si el devlog se escribe y si el audio arranco */
        char d[200];
        snprintf(d, sizeof(d), "PRUEBAS | devlog: %s (%s)", dlog_path(), dlog_ok() ? "OK" : "SIN ESCRIBIR");
        text_draw(SCR_W - 4, SCR_H - 44, FONT_S, d, RGBA(255, 230, 120, 255), 2);
        text_draw(SCR_W - 4, SCR_H - 30, FONT_S, audio_status(), RGBA(255, 230, 120, 255), 2);
    }
#endif
}

static void home_input(unsigned in)
{
    NavPt p[HM_N];
    home_nav(p);
    if (!p[home_sel].on) home_sel = HM_PLAY;
    home_sel = nav_move(p, HM_N, home_sel, in);
    if (!(in & IN_CROSS)) return;
    play_sfx(SFX_CLICK);
    switch (home_sel) {
    case HM_BUNDLE: {
        int ks[4], ns = seasons(ks);
        if (ns) open_packs(pack_index(SEASON_ID[ks[bundle_k % ns]]));
        break;
    }
    case HM_PLAY: play_start(sv.cur_level, 0); break;
    case HM_RETRO: classic_start(); break;
    case HM_OFFER: open_packs(limited_pack()); break;
    case HM_SKIN: shop_tab = 0; sel = 0; set_state(GS_SHOP); break;
    case HM_SETTINGS: sel = 0; confirm_reset = 0; set_state(GS_SETTINGS); break;
    case HM_SHOP: open_packs(-1); break;
    case HM_DAILY: sel = 0; set_state(GS_DAILY); break;
    case HM_LEVELS: {
        int c = sv.cur_level < levels.count ? sv.cur_level : levels.count - 1;
        page = c / 15; sel = c % 15; set_state(GS_SELECT); break;
    }
    }
}

/* ---------------------------------------------------------------- niveles (SelectLevelController: 15 por pagina)
 * El de la 14.2.9: una manzana por nivel (LevelSelectElement.Init): verde superado (sprPass), roja saltado (sprSkip),
 * amarilla el actual (sprCurrent) y gris bloqueado (sprLock, sin numero). Numeros con dos cifras ("00"). */
static int level_open(int i) { return i <= sv.cur_level || level_won(i); }

static void select_draw(void)
{
    gfx_bg(home_tex.pixels ? &home_tex : &bg_tex, WHITE);   /* Background_Home */
    int pages = (levels.count + 14) / 15;
    char b[48];
    gfx_fill(0, 0, SCR_W, SCR_H, RGBA(0, 0, 0, 40));
    text_draw(SCR_W / 2, 4, FONT_L, T("NIVELES"), WHITE, 1);
    int cur = sv.cur_level;
    int apple_ok = WSPR(UI_APPLE_GREEN) >= 0;
    for (int k = 0; k < 15; k++) {
        int i = page * 15 + k;
        if (i >= levels.count) break;
        float x = 72 + (k % 5) * 84, y = 66 + (k / 5) * 66;
        float s = k == sel ? 1.12f + 0.04f * sinf(gtime * 6) : 1.0f;
        int lock = i > cur && !level_won(i);
        int spr = lock ? UI_APPLE_GRAY : i == cur ? UI_APPLE_YELLOW : level_skipped(i) ? UI_APPLE_RED : UI_APPLE_GREEN;
        if (apple_ok) gfx_sprite_fit(&world, WSPR(spr), x, y, 54 * s, 58 * s, WHITE);
        else {
            int id = WSPR(lock ? UI_BTN_GRAY : i == cur ? UI_BTN_BLUE : UI_BTN_GREEN);
            if (id >= 0) gfx_sprite_rect(&world, id, x - 30 * s, y - 26 * s, 60 * s, 52 * s, WHITE);
        }
        if (!lock) {
            snprintf(b, sizeof(b), "%02d", i + 1);
            /* fontUnlock (superado) y fontOther (actual o saltado) */
            uint32_t col = (i == cur || level_skipped(i)) ? RGBA(120, 60, 20, 255) : WHITE;
            text_draw(x, y - 4 * s - text_height(FONT_M) / 2, FONT_M, b, col, 1);
        }
    }
    /* flechas (Btn_NextLevel; la de la izquierda es la misma girada) */
    int arrow = WSPR(UI_BTN_NEXTLEVEL);
    if (arrow >= 0 && spr_h(&world, arrow) > 0) {
        float sc = 64 / spr_h(&world, arrow);
        if (page > 0) gfx_sprite(&world, arrow, 22, 132, sc, 0, FLIP_X, WHITE);
        if (page < pages - 1) gfx_sprite(&world, arrow, SCR_W - 22, 132, sc, 0, 0, WHITE);
    }
    /* Page_Base con "pagina/paginas" */
    int pb = WSPR(UI_PAGE_BASE);
    if (pb >= 0) gfx_sprite_rect(&world, pb, SCR_W / 2 - 46, 236 - 15, 92, 30, WHITE);   /* 178x58 en el original */
    snprintf(b, sizeof(b), "%d/%d", page + 1, pages);
    text_draw(SCR_W / 2, 236 - text_height(FONT_M) / 2, FONT_M, b, pb >= 0 ? TXT_DARK : WHITE, 1);
    help(T(BTN_X " Jugar   " BTN_TRI " Tienda   " BTN_O " Volver   L/R Pagina"));
}

static void select_input(unsigned in)
{
    int pages = (levels.count + 14) / 15;
    if (in & IN_LEFT) {
        if (sel % 5) sel--;
        else if (page > 0) { page--; sel += 4; }
    }
    if (in & IN_RIGHT) {
        if (sel % 5 < 4 && page * 15 + sel + 1 < levels.count) sel++;
        else if (page + 1 < pages) { page++; sel -= sel % 5; }
    }
    if (in & IN_UP && sel >= 5) sel -= 5;
    if (in & IN_DOWN && sel + 5 < 15 && page * 15 + sel + 5 < levels.count) sel += 5;
    if (in & IN_L && page > 0) page--;
    if (in & IN_R && page + 1 < pages) page++;
    while (page * 15 + sel >= levels.count && sel > 0) sel--;
    if (in & IN_CIRCLE) { play_sfx(SFX_CLICK); home_sel = HM_LEVELS; set_state(GS_HOME); }
    if (in & IN_CROSS) {
        int i = page * 15 + sel;
        if (level_open(i)) { play_sfx(SFX_CLICK); play_start(i, 0); }
        else { play_sfx(SFX_STOP); ui_message(T("Supera los niveles anteriores para abrir este"), 1.5f); }
    }
    if (in & IN_TRIANGLE) {   /* boton de aspectos del selector: la tienda */
        play_sfx(SFX_CLICK);
        ui_open_shop();
    }
}

/* ---------------------------------------------------------------- pausa, superado y perdido */
/* ---------------------------------------------------------------- ajustes (SettingsController, screens/settings)
 * En la 14.2.9 no hay menu de pausa: el engranaje abre esta ventana (Popup_Setting_Gameplay) con Musica, Sonido
 * (y Vibrar, que la PSP no tiene) y el boton Home. En el juego se abre con START; en el inicio, con el engranaje (alli
 * el boton de abajo es BORRAR PARTIDA, del port). */
static void settings_popup(int ingame)
{
    gfx_fill(0, 0, SCR_W, SCR_H, RGBA(0, 0, 0, 120));
    if (WSPR(UI_POPUP_SETTING) >= 0) wrect(UI_POPUP_SETTING, 240, 122, 256, 175, WHITE);
    else draw_panel(112, 35, 256, 175);
    wrect(UI_TITLE_RATING, 242, 40, 219, 42, WHITE);
    text_draw(242, 40 - text_height(FONT_L) / 2 - 2, FONT_L, T("AJUSTES"), WHITE, 1);
    wrect(UI_BTN_CANCEL, 360, 40, 22, 22, WHITE);
    text_draw(360, 50, FONT_S, BTN_O, WHITE, 1);
    float s = pulse(sel == 0);
    wfit(sv.music ? UI_MUSIC_ON : UI_MUSIC_OFF, 200, 104, 49 * s, 51 * s, WHITE);
    text_draw(200, 132, FONT_M, T("Musica"), TXT_DARK, 1);
    s = pulse(sel == 1);
    wfit(sv.sound ? UI_SOUND_ON : UI_SOUND_OFF, 280, 104, 49 * s, 51 * s, WHITE);
    text_draw(280, 132, FONT_M, T("Sonido"), TXT_DARK, 1);
    if (ingame) draw_button(170, 166, 140, 40, T("INICIO"), sel == 2, 1);
    else {
        draw_button(170, 166, 140, 40, T(confirm_reset ? "PULSA OTRA VEZ" : "BORRAR PARTIDA"), sel == 2, 1);
        char b[80];
        unsigned m = sv.play_seconds / 60;
        snprintf(b, sizeof(b), T("%d niveles superados | %uh %02um jugadas"), levels_won_count(), m / 60, m % 60);
        text_draw(SCR_W / 2, 150, FONT_S, b, TXT_DARK, 1);
    }
    help(T(BTN_X " Cambiar   " BTN_O " Cerrar"));
}

/* devuelve 1 si hay que cerrar la ventana */
static int settings_nav(unsigned in)
{
    if (in & (IN_LEFT | IN_RIGHT) && sel < 2) sel ^= 1;
    if (in & IN_DOWN) { sel = 2; }
    if (in & IN_UP && sel == 2) { sel = 0; confirm_reset = 0; }
    if (in & IN_CIRCLE) return 1;
    if (in & IN_CROSS) {
        play_sfx(SFX_CLICK);
        if (sel == 0) sv.music ^= 1;
        if (sel == 1) sv.sound ^= 1;
        if (sel < 2) { audio_set_enabled(sv.sound, sv.music); game_save(); }
    }
    return 0;
}

static void pause_draw(void) { settings_popup(1); }

static void pause_input(unsigned in)
{
    if (settings_nav(in) || in & IN_START) { sel = 0; set_state(GS_PLAY); return; }
    if (in & IN_CROSS && sel == 2) { sel = 0; set_state(GS_HOME); }
}

static void progress_bar(float x, float y, float w)
{
    int st, en, id, type;
    if (progress_next(&st, &en, &id, &type) < 0) return;
    int won = levels_won_count();
    float f = (won - (st - 1)) / (float)(en - st + 1);
    if (f < 0) f = 0;
    if (f > 1) f = 1;
    gfx_fill(x, y, w, 12, RGBA(0, 0, 0, 120));
    gfx_fill(x + 2, y + 2, (w - 4) * f, 8, RGBA(250, 200, 40, 255));
    char b[64];
    snprintf(b, sizeof(b), T(type ? "Fondo nuevo: %d%%" : "Aspecto nuevo: %d%%"), (int)(f * 100));
    text_draw(x + w / 2, y + 14, FONT_S, b, TXT_DARK, 1);
    int icon = type ? bg_icon(id) : skin_icon(id);
    if (icon >= 0) gfx_sprite_fit(&shop_sheet, icon, x + w + 26, y + 8, 40, 40, f >= 1 ? WHITE : RGBA(120, 120, 120, 255));
}

/* ---------------------------------------------------------------- superado (RewardProgressPopup + WinPopup)
 * Si el nivel es nuevo y hay un aspecto o fondo en camino (SkinProgress), primero sale RewardProgressPopup: el titulo
 * New Skin/New BG sobre Title_Base, rayos de luz y la silueta (progress_N) llenandose (full_N) hasta el nuevo %. Luego
 * WinPopup: Banner_Win con 3 estrellas, "Nivel 07 COMPLETADO!", el top de jugadores y Siguiente, con las banderas. */
static int win_phase;          /* 0 progreso, 1 ventana de superado */
static float win_t, win_from, win_to;
static int win_tier = -1;

void ui_win_begin(int first)
{
    win_phase = 1;
    win_t = 0;
    win_tier = -1;
    if (!first) return;
    int won = levels_won_count();
    for (int i = 0; i < meta.nprog; i++)
        if (won >= meta.prog[i].start && won <= meta.prog[i].end) {
            float n = meta.prog[i].end - meta.prog[i].start + 1;
            win_tier = i;
            win_from = (won - meta.prog[i].start) / n;
            win_to = (won - meta.prog[i].start + 1) / n;
            win_phase = 0;
            break;
        }
}

static void progress_popup_draw(void)
{
    gfx_fill(0, 0, SCR_W, SCR_H, RGBA(0, 0, 0, 150));
    const ProgressDef *d = &meta.prog[win_tier];
    for (int k = 0; k < 12; k++) {   /* LightRay */
        float a = gtime * 0.6f + k * 0.5236f;
        gfx_fill(240 + cosf(a) * 70 - 3, 146 + sinf(a) * 70 - 3, 6, 6, RGBA(255, 230, 120, 180));
    }
    wrect(UI_TITLE_BASE, 240, 63, 177, 51, WHITE);
    if (WSPR(UI_NEWSKIN) >= 0) wfit(d->type ? UI_NEWBG : UI_NEWSKIN, 240, 57, 81, 17, WHITE);
    else wfit(d->type ? UI_NEWBGUNLOCKED : UI_NEWSKINUNLOCKED, 240, 60, 110, 26, WHITE);
    float f = win_from + (win_to - win_from) * (win_t < 1.2f ? win_t / 1.2f : 1);
    int base = (d->type ? 22 : 0) + d->id * 2;
    int a = d->id < 11 ? meta.prog_icon[base] : -1, b = d->id < 11 ? meta.prog_icon[base + 1] : -1;
    /* ContentSkin 443x177 / ContentBg 504x314 del original, a 1/4 y con la proporcion del dibujo */
    float bw = d->type ? 126 : 111, bh = d->type ? 79 : 44, w = bw, h = bh;
    if (a >= 0 && shop_sheet.spr[a].w && shop_sheet.spr[a].h) {
        float k = bw / shop_sheet.spr[a].w < bh / shop_sheet.spr[a].h ? bw / shop_sheet.spr[a].w : bh / shop_sheet.spr[a].h;
        w = shop_sheet.spr[a].w * k;
        h = shop_sheet.spr[a].h * k;
    }
    float x = 238 - w / 2, y = 146 - h / 2;
    if (a >= 0) gfx_sprite_rect(&shop_sheet, a, x, y, w, h, WHITE);
    if (b >= 0) gfx_sprite_rect_bottom(&shop_sheet, b, x, y, w, h, f, WHITE);
    char t[16];
    snprintf(t, sizeof(t), "%d%%", (int)(f * 100 + 0.5f));
    text_draw(238, 148 - text_height(FONT_XL) / 2, FONT_XL, t, WHITE, 1);
    /* boton de aspectos (Btn_Skin_Unlocked) a la derecha, como en el original */
    wrect(UI_BTN_SKIN, 430, 37, 26, 26, WHITE);
    if (f >= 1) wrect(UI_ICON_NOTI, 440, 28, 8, 8, WHITE);
    help(T(BTN_X " Seguir"));
}

static void win_draw(void)
{
    if (win_phase == 0 && win_tier >= 0) {
        progress_popup_draw();
        return;
    }
    gfx_fill(0, 0, SCR_W, SCR_H, RGBA(0, 0, 0, 110));
    wrect(UI_FLAG, 86, 31, 119, 58, WHITE);
    wrect(UI_FLAG, 394, 31, 119, 58, WHITE);
    if (WSPR(UI_POPUP_WIN) >= 0) wrect(UI_POPUP_WIN, 241, 124, 256, 182, WHITE);
    else draw_panel(113, 33, 256, 182);
    wrect(UI_BANNER_WIN, 242, 37, 216, 70, WHITE);
    /* tres estrellas (Star_banner_win_on en el centro) */
    float st = win_t < 0.6f ? win_t / 0.6f : 1;
    wfit(UI_STAR, 199, 41, 34 * st, 33 * st, WHITE);
    wfit(UI_STAR_BANNER_WIN_ON, 241, 31, 41 * st, 40 * st, WHITE);
    wfit(UI_STAR, 280, 41, 34 * st, 33 * st, WHITE);
    char b[96];
    snprintf(b, sizeof(b), T("Nivel %02d"), play_pos + 1);
    text_draw(241, 78, FONT_XL, b, TXT_DARK, 1);
    text_draw(241, 108, FONT_L, T("COMPLETADO!"), TXT_DARK, 1);
    /* top_player.txt: porcentaje de jugadores (LbTopPlayer) */
    int pct = play_pos < meta.ntop ? meta.top[play_pos] : 0;
    if (pct) {
        snprintf(b, sizeof(b), T("Estas en el top %d,%02d%% de jugadores"), pct / 100, pct % 100);
        text_draw(241, 134, FONT_S, b, RGBA(200, 90, 30, 255), 1);
    }
    progress_bar(165, 147, 110);
    draw_button(171, 168, 140, 40, T("SIGUIENTE"), 1, 1);
    help(T(BTN_X " Siguiente   " BTN_O " Inicio"));
}

static void win_input(unsigned in)
{
    if (win_phase == 0 && win_tier >= 0) {
        if (in & (IN_CROSS | IN_CIRCLE) && win_t > 0.3f) { win_phase = 1; win_t = 0; }
        return;
    }
    if (in & IN_CIRCLE) { play_sfx(SFX_CLICK); set_state(GS_HOME); return; }
    if (!(in & IN_CROSS)) return;
    play_sfx(SFX_CLICK);
    play_start(play_pos + 1, 0);   /* tras el ultimo nivel sigue el bucle (UserLevelInfo) */
}

/* ---------------------------------------------------------------- perdido (LoseController, screens/lose)
 * Popup_Lose con Banner_Lose "Failed!", Icon_Lose y dos botones: Repetir (Btn_Green, Icon_Replay) y Saltar nivel
 * (Btn_Blue, Icon_SkipLevel; en el original con anuncio, aqui gasta un salto). */
static void lose_draw(void)
{
    gfx_fill(0, 0, SCR_W, SCR_H, RGBA(0, 0, 0, 110));
    if (WSPR(UI_POPUP_LOSE) >= 0) wrect(UI_POPUP_LOSE, 240, 121, 256, 182, WHITE);
    else draw_panel(112, 30, 256, 182);
    wrect(UI_BANNER_LOSE, 242, 32, 217, 60, WHITE);
    text_draw(244, 30 - text_height(FONT_L) / 2, FONT_L, T("FALLASTE!"), WHITE, 1);
    wfit(UI_ICON_LOSE, 238, 110, 133, 46, WHITE);
    float s = pulse(sel == 0);
    wrect(UI_BTN_GREEN, 183, 182, 106 * s, 44 * s, WHITE);
    wfit(UI_ICON_REPLAY, 150, 182, 22, 20, WHITE);
    text_draw(193, 182 - text_height(FONT_M) / 2, FONT_M, T("Repetir"), WHITE, 1);
    s = pulse(sel == 1);
    wrect(UI_BTN_BLUE, 297, 182, 106 * s, 44 * s, sv.skips > 0 ? WHITE : RGBA(150, 150, 150, 255));
    wfit(UI_ICON_SKIPLEVEL, 263, 182, 25, 19, WHITE);
    char b[32];
    snprintf(b, sizeof(b), T("Saltar (%d)"), sv.skips);
    text_draw(306, 182 - text_height(FONT_M) / 2, FONT_M, b, WHITE, 1);
    help(T(BTN_X " Aceptar   " BTN_O " Repetir   Cruceta Elegir"));
}

static void lose_input(unsigned in)
{
    if (in & (IN_LEFT | IN_RIGHT)) sel = !sel;
    if (in & IN_CIRCLE) { sel = 0; play_start(play_pos, 0); return; }
    if (!(in & IN_CROSS)) return;
    play_sfx(SFX_CLICK);
    if (sel == 0) { play_start(play_pos, 0); return; }
    if (sv.skips > 0) {
        sv.skips--;
        sel = 0;
        play_skip();
        play_start(play_pos + 1, 0);
    } else {
        play_sfx(SFX_STOP);
        ui_message(T("No te quedan saltos: consiguelos en la tienda con fichas"), 2.0f);
    }
}

/* ---------------------------------------------------------------- tienda */
void ui_open_shop(void)
{
    shop_tab = 0;
    home_sel = HM_SKIN;
    sel = 0;
    set_state(GS_SHOP);
}
static const ShopItemDef *item_def(int type, int id)
{
    for (int i = 0; i < meta.nshop; i++)
        if (meta.shop[i].type == type && meta.shop[i].id == id) return &meta.shop[i];
    return NULL;
}

static int pack_has(int k, int type, int id);
static int pack_of_item(int type, int id);

/* lo que se ve en la tienda: el sortIndex de la 14.2.9 (sin los que no tienen graficos, como el aspecto 29); los que
 * la 14.2.9 ya no ensena (14, 24, 25 y 26) solo salen si ya los tienes, para no perderlos */
static void shop_build(void)
{
    shop_count = 0;
    const uint8_t *ord = shop_tab ? meta.bg_order : meta.skin_order;
    int nord = shop_tab ? meta.nbg_order : meta.nskin_order;
    if (!nord) {
        ord = shop_tab ? BG_ORDER_OLD : SKIN_ORDER_OLD;
        nord = shop_tab ? (int)sizeof(BG_ORDER_OLD) : (int)sizeof(SKIN_ORDER_OLD);
    }
    int max = shop_tab ? MAX_BGS : MAX_SKINS;
    uint8_t used[MAX_SKINS] = {0};
    for (int i = 0; i < nord; i++) {
        int id = ord[i];
        if (id >= max || used[id] || !(shop_tab ? bg_exists(id) : skin_exists(id))) continue;
        shop_list[shop_count++] = id;
        used[id] = 1;
    }
    for (int i = 0; i < max; i++)
        if (!used[i] && (shop_tab ? bg_available(i) : skin_available(i))) shop_list[shop_count++] = i;
}

/* icono de un articulo de la tienda */
static void item_icon(int tab, int id, float x, float y, float w, float h, uint32_t col)
{
    int icon = tab ? bg_icon(id) : skin_icon(id);
    if (icon >= 0) gfx_sprite_fit(&shop_sheet, icon, x, y, w, h, col);
}

/* lo que hace falta para un articulo bloqueado: fichas (en vez de anuncios), progreso o evento */
static int item_need(int type, int id, const char **why)
{
    const ShopItemDef *d = item_def(type, id);
    for (int i = 0; i < meta.nprog; i++)
        if (meta.prog[i].type == type && meta.prog[i].id == id) {
            static char b[48];
            snprintf(b, sizeof(b), T("Supera %d niveles"), meta.prog[i].end);
            *why = b;
            return -1;
        }
    if (d && d->state == 6) {
        *why = T(d->event == 1 ? "Evento de Halloween" : d->event == 2 ? "Evento de Navidad" : "Evento especial");
        /* durante su evento se consigue con fichas, como los demas; fuera de el, bloqueado */
        if (event_now() == d->event) return d->need ? d->need : 3;
        return -1;
    }
    *why = NULL;
    if (!d) {
        /* sin ShopItem: en el original se consigue con su paquete de la tienda de pago (aqui en PACKS, con fichas) */
        if (pack_of_item(type, id) >= 0) {
            *why = T("En PACKS (con fichas)");
            return -1;
        }
        return 5;
    }
    if (d->state == 4) {
        *why = T("Diario o fichas");
        return d->need ? d->need : 3;
    }
    return d->need ? d->need : 1;
}

/* ---- packs (ShopIAPController, BundleEvent / SummerBundle / PrideBundle / WorldCupBundle, LimitedOffer, SpecialBundle,
 * NoAdsBundle): en la PSP se pagan con fichas. En la 14.2.9 los de temporada salen siempre (seasonal_pack_show =
 * "1,2,3,4") hasta comprarlos (HideXBundle); la oferta limitada (paquete con temporizador) tambien se ofrece. ---- */

static int pack_has(int k, int type, int id)
{
    for (int j = 0; j < meta.pack[k].n; j++)
        if (meta.pack[k].type[j] == (type ? RW_BG : RW_SKIN) && meta.pack[k].qty[j] == id) return 1;
    return 0;
}

static int pack_of_item(int type, int id)
{
    for (int k = 0; k < meta.npack; k++)
        if (pack_has(k, type, id)) return k;
    return -1;
}

static int pack_valid(int k)
{
    int useful = 0;
    for (int j = 0; j < meta.pack[k].n; j++) {
        int t = meta.pack[k].type[j], q = meta.pack[k].qty[j];
        if (t == RW_HINT || t == RW_SKIP) useful = 1;
        /* aspectos y fondos: solo si existen y aun no los tienes (comprado = agotado, como HideXBundle) */
        if (t == RW_SKIN && skin_exists(q) && !skin_available(q)) useful = 1;
        if (t == RW_BG && bg_exists(q) && !bg_available(q)) useful = 1;
    }
    return useful;
}

/* precio en fichas: 1 por pista o salto (el original da 1 pista por anuncio visto) y 5 por aspecto o fondo, con
 * descuento en los paquetes grandes como en la tienda original */
static int pack_price(int k)
{
    int c = 0;
    for (int j = 0; j < meta.pack[k].n; j++) {
        int t = meta.pack[k].type[j], q = meta.pack[k].qty[j];
        if (t == RW_HINT || t == RW_SKIP) c += q;
        else if ((t == RW_SKIN && skin_exists(q)) || (t == RW_BG && bg_exists(q))) c += 5;
    }
    if (c >= 10) c = c * 3 / 4;
    return c < 1 ? 1 : c;
}

/* nombre de los paquetes de la 14.2.9 (BundleType: LunarNewYear, UnderTheSea, RainbowParade, GoldenGoal) */
static const char *pack_name(int k)
{
    if (meta.pack[k].timer) return T("Oferta limitada");
    switch (meta.pack[k].id) {
    case 11: return T("Ano Nuevo Lunar");
    case 13: return T("Bajo el mar");
    case 14: return T("Desfile arcoiris");
    case 15: return T("Gol de oro");
    }
    return NULL;
}

static void pack_text(int k, char *b, int n)
{
    b[0] = 0;
    const char *nm = pack_name(k);
    if (nm) snprintf(b, n, "%s: ", nm);
    int first = 1;
    for (int j = 0; j < meta.pack[k].n; j++) {
        int t = meta.pack[k].type[j], q = meta.pack[k].qty[j];
        char p[32] = "";
        if (t == RW_HINT) snprintf(p, sizeof(p), T("%d pistas"), q);
        else if (t == RW_SKIP) snprintf(p, sizeof(p), T("%d saltos"), q);
        else if (t == RW_SKIN && skin_exists(q)) snprintf(p, sizeof(p), "%s", T("aspecto"));
        else if (t == RW_BG && bg_exists(q)) snprintf(p, sizeof(p), "%s", T("fondo"));
        if (!p[0]) continue;
        if (!first) strncat(b, " + ", n - strlen(b) - 1);
        strncat(b, p, n - strlen(b) - 1);
        first = 0;
    }
}

/* ---- tienda de pago (ShopIAPController, screens/shopiap): tarjetas en fila; aqui se paga con fichas ----
 * Orden del original: paquete especial, los de temporada (Bajo el mar, Desfile arcoiris, Gol de oro, Ano Nuevo
 * Lunar), la oferta limitada y los de pistas (con la pista "gratis" por anuncio, que aqui cuesta 1 ficha). Los de
 * temporada comprados desaparecen (HideXBundle); los anuncios (No Ads) no existen en la PSP. */
typedef struct { int pack, card; } Card;
#define FREE_HINT -2
static Card cards[24];
static int ncards;

static void cards_build(void)
{
    static const int order[] = {2, 13, 14, 15, 11};
    static const int art[] = {SU_CARD_SPECIAL, SU_CARD_SUMMER, SU_CARD_PRIDE, SU_CARD_WORLDCUP, SU_CARD_LUNAR};
    ncards = 0;
    for (int i = 0; i < 5; i++) {
        int k = pack_index(order[i]);
        if (k >= 0 && pack_valid(k)) cards[ncards++] = (Card){k, art[i]};
    }
    int lk = limited_pack();
    if (lk >= 0 && pack_valid(lk)) cards[ncards++] = (Card){lk, SU_CARD_DUCK};
    cards[ncards++] = (Card){FREE_HINT, SU_BASEPACK};
    for (int id = 3; id <= 7; id++) {
        int k = pack_index(id);
        if (k >= 0 && pack_valid(k) && ncards < 24) cards[ncards++] = (Card){k, SU_BASEPACK};
    }
}

static int card_price(const Card *c) { return c->pack == FREE_HINT ? 1 : pack_price(c->pack); }

static int pack_hints(int k)
{
    for (int j = 0; j < meta.pack[k].n; j++)
        if (meta.pack[k].type[j] == RW_HINT) return meta.pack[k].qty[j];
    return 0;
}

static void open_packs(int k)
{
    shop_tab = 2;
    sel = 0;
    cards_build();
    for (int i = 0; i < ncards; i++)
        if (cards[i].pack == k) sel = i;
    set_state(GS_SHOP);
}

static void card_draw(const Card *c, float cx, float cy, int on)
{
    float s = on ? 1.0f : 0.8f;
    uint32_t col = on ? WHITE : RGBA(170, 170, 170, 255);
    char b[48];
    if (c->card == SU_BASEPACK) {
        /* BasePack: titulo, icono de pista y cantidad */
        float w = 92 * s, h = 90 * s;
        srect(SU_BASEPACK, cx, cy, w, h, col);
        int q = c->pack == FREE_HINT ? 1 : pack_hints(c->pack);
        static const struct { int q; const char *n; } names[] = {{1, "Pista"}, {3, "Pistas mini"}, {6, "Pistas pequeno"},
                                                                 {10, "Pistas medio"}, {15, "Pistas grande"}, {25, "Pistas extremo"}};
        const char *nm = "Pistas";
        for (unsigned i = 0; i < sizeof(names) / sizeof(names[0]); i++)
            if (names[i].q == q) nm = names[i].n;
        text_draw(cx, cy - 37 * s, FONT_S, T(nm), WHITE, 1);
        sfit(SU_ICON_HINT, cx - 9 * s, cy - 3 * s, 29 * s, 29 * s, col);
        snprintf(b, sizeof(b), "x%d", q);
        text_draw(cx + 16 * s, cy - 9 * s, FONT_M, b, WHITE, 1);
        /* boton verde con el precio (btn_green) */
        int g = WSPR(UI_BTN_GREEN);
        if (g >= 0) gfx_sprite_rect(&world, g, cx - 28 * s, cy + 22 * s, 56 * s, 20 * s, col);
        snprintf(b, sizeof(b), T("%d fichas"), card_price(c));
        text_draw(cx, cy + 32 * s - text_height(FONT_S) / 2, FONT_S, b, WHITE, 1);
        return;
    }
    int id = sui(c->card);
    float iw = id >= 0 ? shop_sheet.spr[id].w : 100, ih = id >= 0 ? shop_sheet.spr[id].h : 100;
    float k = 150 * s / ih;
    if (iw * k > 190 * s) k = 190 * s / iw;
    srect(c->card, cx, cy, iw * k, ih * k, col);
    if (c->card == SU_CARD_SPECIAL) {   /* el texto de la tarjeta especial lo pone el juego (TMP) */
        text_draw(cx, cy - 52 * s, FONT_S, T("Paquete especial"), WHITE, 1);
        int sk = 0, hn = 0;
        for (int j = 0; j < meta.pack[c->pack].n; j++) {
            if (meta.pack[c->pack].type[j] == RW_SKIP) sk = meta.pack[c->pack].qty[j];
            if (meta.pack[c->pack].type[j] == RW_HINT) hn = meta.pack[c->pack].qty[j];
        }
        snprintf(b, sizeof(b), "x%d", sk);
        text_draw(cx - 21 * s, cy + 18 * s, FONT_M, b, WHITE, 1);
        snprintf(b, sizeof(b), "x%d", hn);
        text_draw(cx + 22 * s, cy + 18 * s, FONT_M, b, WHITE, 1);
    }
    if (c->card == SU_CARD_DUCK) text_draw(cx + 10 * s, cy - 42 * s, FONT_S, T("OFERTA"), WHITE, 1);
    /* el precio va en el boton verde de la tarjeta (Price Text: -216 en la especial, unos -270 en las demas) */
    snprintf(b, sizeof(b), T("%d fichas"), card_price(c));
    float py = cy + (c->card == SU_CARD_SPECIAL ? 216 : 270) / 4.0f * k;   /* original/4 a la escala de la tarjeta */
    text_draw(cx, py - text_height(FONT_M) / 2, FONT_M, b, sv.tokens >= card_price(c) ? WHITE : RGBA(255, 170, 170, 255), 1);
}

static void packs_draw(void)
{
    cards_build();
    if (sel >= ncards) sel = ncards - 1;
    if (sel < 0) sel = 0;
    text_draw(SCR_W / 2, 4, FONT_L, T("TIENDA"), WHITE, 1);
    /* la elegida en el centro y las demas a los lados */
    float x = 240;
    for (int i = sel; i >= 0 && x > -120; i--) {
        float w = cards[i].card == SU_BASEPACK ? 92 : 170;
        if (i != sel) x -= w * 0.8f / 2 + 10;
        card_draw(&cards[i], x, 112, i == sel);
        x -= w * (i == sel ? 1.0f : 0.8f) / 2;
    }
    x = 240;
    for (int i = sel + 1; i < ncards && x < 600; i++) {
        float wp = cards[i - 1].card == SU_BASEPACK ? 92 : 170, w = cards[i].card == SU_BASEPACK ? 92 : 170;
        x += wp * (i - 1 == sel ? 1.0f : 0.8f) / 2 + 10 + w * 0.8f / 2;
        card_draw(&cards[i], x, 112, 0);
    }
    counters(SCR_H - 40);
    help(T(BTN_X " Comprar con fichas   " BTN_O " Volver   L/R Pestana"));
}

static void packs_buy(void)
{
    cards_build();
    if (sel >= ncards || sel < 0) return;
    Card c = cards[sel];
    int price = card_price(&c);
    if (sv.tokens < price) {
        play_sfx(SFX_STOP);
        ui_message(T("Faltan fichas: ganas 1 por cada nivel nuevo superado"), 2.5f);
        return;
    }
    sv.tokens -= price;
    if (c.pack == FREE_HINT) {
        sv.hints++;
        play_sfx(SFX_REWARD);
        ui_message(T("+1 pista"), 1.5f);
        dlog("TIENDA pista suelta por 1 ficha");
        game_save();
        return;
    }
    char b[100], m[128];
    pack_text(c.pack, b, sizeof(b));
    int k = c.pack;
    for (int j = 0; j < meta.pack[k].n; j++) {
        int t = meta.pack[k].type[j], q = meta.pack[k].qty[j];
        if (t == RW_HINT) sv.hints += q;
        else if (t == RW_SKIP) sv.skips += q;
        else if (t == RW_SKIN && skin_exists(q) && !skin_available(q)) { own_skin(q); reward_popup(0, q); }
        else if (t == RW_BG && bg_exists(q) && !bg_available(q)) { own_bg(q); reward_popup(1, q); }
    }
    play_sfx(SFX_REWARD);
    snprintf(m, sizeof(m), T("Comprado: %s"), b);
    ui_message(m, 2.0f);
    dlog("TIENDA paquete %d por %d fichas", meta.pack[k].id, price);
    game_save();
}

/* ---- aspectos y fondos (ShopController, screens/shop): la serpiente sobre la plataforma a la izquierda y el panel de
 * casillas (ShopItemUI) a la derecha, 3 por fila. Cada casilla: Slot_Unlocked (Slot_Highlight la equipada, con
 * TickIcon); si esta bloqueada, Base_Lock con candado y su texto: "Lv N" (progreso), "Special" (evento o paquete),
 * "Limited" (oferta limitada), "Day N" (recompensa diaria) o las fichas puestas (los anuncios del original). Los
 * adornos: calabaza (aspecto 15, fondo 8), Navidad (21, 9), farolillo (27, 11), New (30-32, 14-16), VIP (33, 17). */
static void item_label(int type, int id, char *b, int n)
{
    b[0] = 0;
    for (int i = 0; i < meta.nprog; i++)
        if (meta.prog[i].type == type && meta.prog[i].id == id) { snprintf(b, n, "Lv %d", meta.prog[i].end); return; }
    const ShopItemDef *d = item_def(type, id);
    if (!d) {
        int k = pack_of_item(type, id);
        snprintf(b, n, "%s", T(k >= 0 && meta.pack[k].timer ? "Limitado" : "Especial"));
        return;
    }
    if (d->state == 6) { snprintf(b, n, "%s", T("Especial")); return; }
    if (d->state == 4)
        for (int i = 0; i < meta.ndaily; i++)
            if (meta.daily[i].kind == 1 && meta.daily[i].value == id) { snprintf(b, n, T("Dia %d"), i + 1); return; }
    const char *why;
    int need = item_need(type, id, &why);
    if (need > 0) snprintf(b, n, "%d/%d", type ? sv.bg_paid[id] : sv.skin_paid[id], need);
}

static int item_tag(int type, int id)
{
    if ((type == 0 && id == 15) || (type && id == 8)) return SU_TAG_HALLOWEEN;
    if ((type == 0 && id == 21) || (type && id == 9)) return SU_TAG_XMAS;
    if ((type == 0 && id == 27) || (type && id == 11)) return SU_TAG_LUNAR;
    if ((type == 0 && id == 33) || (type && id == 17)) return SU_TAG_VIP;
    if ((type == 0 && id >= 30 && id <= 32) || (type && id >= 14 && id <= 16)) return SU_TAG_NEW;
    return -1;
}

static void shop_draw(void)
{
    gfx_bg(&bg_tex, WHITE);
    if (shop_tab == 2) {
        srect(SU_BACK, 22, 19, 23, 23, WHITE);
        packs_draw();
        return;
    }
    shop_build();
    if (sel >= shop_count) sel = shop_count - 1;
    if (sel < 0) sel = 0;
    srect(SU_BACK, 22, 19, 23, 23, WHITE);
    text_draw(22, 31, FONT_S, BTN_O, WHITE, 1);
    /* plataforma con la serpiente equipada y su manzana (SnakeShop, AppleShop) */
    srect(SU_GROUND, 125, 138, 124, 41, WHITE);
    draw_snake_preview(104, 113, 24, 3);
    if (SK_APPLE < skin_sheet.nspr) gfx_sprite(&skin_sheet, SK_APPLE, 166, 114, 26, 0, 0, WHITE);
    /* panel con las pestanas */
    srect(SU_POPUP, 339, 130, 228, 163, WHITE);
    int t0 = shop_tab == 0;
    srect(SU_TAB_UNSEL, t0 ? 383 : 288, 38, 91, 32, WHITE);
    sfit(t0 ? SU_ICON_BG : SU_ICON_SKIN, t0 ? 383 : 288, 38, 26, 23, WHITE);
    srect(SU_TAB_SEL, t0 ? 288 : 383, 34, 94, 40, WHITE);
    sfit(t0 ? SU_ICON_SKIN : SU_ICON_BG, t0 ? 288 : 383, 32, 33, 28, WHITE);
    text_draw(t0 ? 383 : 288, 54, FONT_S, "L/R", RGBA(110, 70, 40, 255), 1);
    /* casillas: 3 por fila, 2 filas a la vista */
    int first_row = sel / 3 > 1 ? sel / 3 - 1 : 0;
    int rows = (shop_count + 2) / 3;
    for (int k = first_row * 3; k < shop_count && k < first_row * 3 + 6; k++) {
        int id = shop_list[k];
        float x = 271 + (k % 3) * 65.5f, y = 97 + (k / 3 - first_row) * 70;
        int own = shop_tab ? bg_available(id) : skin_available(id);
        int eq = (shop_tab ? sv.bg : sv.skin) == id;
        float s = pulse(k == sel);
        srect(eq ? SU_SLOT_HL : SU_SLOT, x, y, 56 * s, 64 * s, WHITE);
        item_icon(shop_tab, id, x, y - 2, 46 * s, 54 * s, WHITE);
        if (!own) {
            char b[24];
            srect(SU_BASE_LOCK, x, y, 56 * s, 64 * s, WHITE);
            sfit(SU_LOCK, x - 21 * s, y - 26 * s, 10, 12, WHITE);
            item_label(shop_tab, id, b, sizeof(b));
            int lim = !strcmp(b, T("Limitado"));
            text_draw(x, y + 17 * s, FONT_S, b, lim ? RGBA(255, 220, 60, 255) : WHITE, 1);
        }
        int tg = item_tag(shop_tab, id);
        if (tg >= 0) sfit(tg, x - 20 * s, y - 28 * s, tg == SU_TAG_NEW || tg == SU_TAG_VIP ? 24 : 15, 14, WHITE);
        if (eq) wfit(UI_TICKICON, x + 23 * s, y - 27 * s, 15, 15, WHITE);
        if (k == sel) gfx_fill(x - 28 * s, y + 31 * s, 56 * s, 2, RGBA(250, 200, 40, 255));
    }
    /* barra de desplazamiento (Scroller / Scroller2) */
    srect(SU_SCROLLER, 441, 130, 5, 134, WHITE);
    if (rows > 2) {
        float hh = 134 * 2.0f / rows, yy = 63 + (134 - hh) * first_row / (float)(rows - 2);
        srect(SU_SCROLLER2, 441, yy + hh / 2, 6, hh, WHITE);
    }
    /* lo que hace falta para el elegido */
    int id = shop_list[sel];
    int owned = shop_tab ? bg_available(id) : skin_available(id);
    int equipped = shop_tab ? sv.bg == id : sv.skin == id;
    const char *why = NULL;
    int need = owned ? 0 : item_need(shop_tab, id, &why);
    char b[64];
    if (equipped) snprintf(b, sizeof(b), "%s", T("EQUIPADO"));
    else if (owned) snprintf(b, sizeof(b), "%s", T(BTN_X " Equipar"));
    else if (need > 0) snprintf(b, sizeof(b), T(BTN_X " Pagar ficha (%d/%d)"), shop_tab ? sv.bg_paid[id] : sv.skin_paid[id], need);
    else snprintf(b, sizeof(b), "%s", T("Bloqueado"));
    text_draw(125, 172, FONT_M, b, WHITE, 1);
    if (!owned && why) text_draw(125, 192, FONT_S, why, RGBA(255, 220, 120, 255), 1);
    counters(SCR_H - 40);
    help(T(BTN_X " Elegir/pagar   " BTN_O " Volver   L/R Pestana"));
}

static void shop_input(unsigned in)
{
    if (in & IN_L) { shop_tab = (shop_tab + 2) % 3; sel = 0; }
    if (in & IN_R) { shop_tab = (shop_tab + 1) % 3; sel = 0; }
    if (in & IN_CIRCLE) { play_sfx(SFX_CLICK); home_sel = shop_tab == 2 ? HM_SHOP : HM_SKIN; set_state(GS_HOME); return; }
    if (shop_tab == 2) {
        cards_build();
        if (sel >= ncards) sel = ncards ? ncards - 1 : 0;   /* la lista encoge al agotarse un paquete */
        if (in & IN_LEFT && sel > 0) sel--;
        if (in & IN_RIGHT && sel + 1 < ncards) sel++;
        if (in & IN_CROSS) packs_buy();
        return;
    }
    shop_build();
    if (in & IN_LEFT && sel > 0) sel--;
    if (in & IN_RIGHT && sel + 1 < shop_count) sel++;
    if (in & IN_UP && sel >= 3) sel -= 3;
    if (in & IN_DOWN && sel + 3 < shop_count) sel += 3;
    else if (in & IN_DOWN && sel / 3 < (shop_count - 1) / 3) sel = shop_count - 1;
    if (!(in & IN_CROSS) || !shop_count) return;
    int id = shop_list[sel];
    int owned = shop_tab ? bg_available(id) : skin_available(id);
    if (owned) {
        play_sfx(SFX_CLICK);
        if (shop_tab) { sv.bg = id; load_bg(id); }
        else { sv.skin = id; load_skin(id); }
        game_save();
        return;
    }
    const char *why;
    int need = item_need(shop_tab, id, &why);
    if (need <= 0) {
        int k = pack_of_item(shop_tab, id);
        if (k >= 0 && pack_valid(k)) { play_sfx(SFX_CLICK); open_packs(k); return; }   /* su paquete */
        play_sfx(SFX_STOP);
        ui_message(why ? why : T("Bloqueado"), 1.5f);
        return;
    }
    if (!sv.tokens) { play_sfx(SFX_STOP); ui_message(T("Sin fichas: gana 1 ficha por cada nivel nuevo superado"), 2.5f); return; }
    sv.tokens--;
    uint8_t *paid = shop_tab ? &sv.bg_paid[id] : &sv.skin_paid[id];
    (*paid)++;
    play_sfx(SFX_CLICK8);
    if (*paid >= need) {
        if (shop_tab) own_bg(id);
        else own_skin(id);
        dlog("TIENDA desbloqueado %s %d", shop_tab ? "fondo" : "aspecto", id);
        reward_popup(shop_tab, id);
    }
    game_save();
}

/* ---------------------------------------------------------------- recompensa diaria (DailyRewardController) */
static void daily_draw(void)
{
    gfx_bg(&bg_tex, WHITE);
    gfx_fill(0, 0, SCR_W, SCR_H, RGBA(0, 0, 0, 80));
    text_draw(SCR_W / 2, 8, FONT_L, T("RECOMPENSA DIARIA"), WHITE, 1);
    int ready = daily_ready();
    int day = sv.daily_day % (meta.ndaily ? meta.ndaily : 7);
    for (int i = 0; i < meta.ndaily; i++) {
        float x = i < 6 ? 60 + (i % 3) * 110 : 400, y = i < 6 ? 80 + (i / 3) * 90 : 125;
        int claimed = i < day;
        int base = WSPR(i == 6 ? (claimed ? UI_BASE_ITEM_DAY7_CLAIMED : UI_BASE_ITEM_DAY7)
                               : (claimed ? UI_BASE_ITEM_CLAIMED : UI_BASE_ITEM_DAILY));
        float w = i == 6 ? 90 : 96, h = i == 6 ? 150 : 80;
        float s = (i == day && ready) ? 1.06f + 0.04f * sinf(gtime * 6) : 1.0f;
        if (base >= 0) gfx_sprite_rect(&world, base, x - w * s / 2, y - h * s / 2, w * s, h * s, WHITE);
        else gfx_fill(x - w / 2, y - h / 2, w, h, claimed ? RGBA(80, 80, 80, 255) : RGBA(220, 160, 40, 255));
        char b[32];
        snprintf(b, sizeof(b), T("Dia %d"), i + 1);
        text_draw(x, y - h / 2 + 4, FONT_S, b, WHITE, 1);
        const DailyDef *d = &meta.daily[i];
        if (d->kind == 1) {
            int icon = skin_icon(d->value);
            if (icon >= 0) gfx_sprite_fit(&shop_sheet, icon, x, y + 6, w * 0.6f, h * 0.55f, WHITE);
        } else {
            gfx_sprite_fit(&world, WSPR(UI_ICON_HINT), x, y + 4, 36, 36, WHITE);
            snprintf(b, sizeof(b), "x%d", d->qty);
            text_draw(x + 16, y + 10, FONT_M, b, WHITE, 0);
        }
        if (claimed) gfx_sprite_fit(&world, WSPR(UI_ICON_TICK), x + w / 2 - 12, y + h / 2 - 12, 24, 24, WHITE);
    }
    help(T(ready ? BTN_X " Cobrar   " BTN_O " Volver" : "Vuelve manana a por mas   " BTN_O " Volver"));
}

static void daily_input(unsigned in)
{
    if (in & IN_CIRCLE) { play_sfx(SFX_CLICK); home_sel = HM_DAILY; set_state(GS_HOME); return; }
    if (!(in & IN_CROSS) || !daily_ready()) return;
    int n = meta.ndaily ? meta.ndaily : 7;
    int day = sv.daily_day % n;
    const DailyDef *d = &meta.daily[day];
    sv.daily_day = (day + 1) % n;   /* al terminar los 7 dias se vuelve a empezar */
    sv.daily_last = today_ymd();
    if (d->kind == 1) {
        if (!skin_available(d->value)) {
            own_skin(d->value);
            reward_popup(0, d->value);
        } else sv.hints += 2;   /* ya lo tenia: pistas en su lugar */
    } else {
        sv.hints += d->qty;
        play_sfx(SFX_REWARD);
        char b[48];
        snprintf(b, sizeof(b), T(d->qty > 1 ? "+%d pistas" : "+%d pista"), d->qty);
        ui_message(b, 1.5f);
    }
    dlog("DIARIO dia %d cobrado", day + 1);
    game_save();
}

/* ---------------------------------------------------------------- ajustes */
static void settings_draw(void)
{
    home_draw();
    settings_popup(0);
}

extern void save_defaults(void);

static void settings_input(unsigned in)
{
    if (settings_nav(in)) { play_sfx(SFX_CLICK); sel = 0; confirm_reset = 0; home_sel = HM_SETTINGS; set_state(GS_HOME); return; }
    if (!(in & IN_CROSS) || sel != 2) return;
    if (!confirm_reset) confirm_reset = 1;
    else {
        save_defaults();
        load_skin(0);
        load_bg(0);
        confirm_reset = 0;
        ui_message(T("Partida borrada"), 1.5f);
        dlog("AJUSTES partida borrada");
        game_save();
    }
}

/* ---------------------------------------------------------------- premio (NewSkinUnlocked / NewBGUnlocked) */
static void reward_draw(void)
{
    gfx_fill(0, 0, SCR_W, SCR_H, RGBA(0, 0, 0, 140));
    if (!reward_n) return;
    int type = reward_q[0][0], id = reward_q[0][1];
    for (int k = 0; k < 12; k++) {   /* rayos de luz (lightRay) */
        float a = gtime * 0.6f + k * 0.5236f;
        gfx_fill(SCR_W / 2 + cosf(a) * 80 - 3, 130 + sinf(a) * 80 - 3, 6, 6, RGBA(255, 230, 120, 200));
    }
    int ban = WSPR(type ? UI_NEWBGUNLOCKED : UI_NEWSKINUNLOCKED);
    if (ban >= 0) gfx_sprite_fit(&world, ban, SCR_W / 2, 36, 200, 50, WHITE);
    text_draw(SCR_W / 2, 64, FONT_L, T(type ? "FONDO NUEVO!" : "ASPECTO NUEVO!"), WHITE, 1);
    int icon = type ? bg_icon(id) : skin_icon(id);
    float s = 1.0f + 0.06f * sinf(gtime * 5);
    if (icon >= 0) gfx_sprite_fit(&shop_sheet, icon, SCR_W / 2, 150, 140 * s, 120 * s, WHITE);
    help(T(BTN_X " Usar ahora   " BTN_O " Seguir"));
}

static void reward_input(unsigned in)
{
    if (!(in & (IN_CROSS | IN_CIRCLE))) return;
    if (reward_n && (in & IN_CROSS)) {
        int type = reward_q[0][0], id = reward_q[0][1];
        if (type) { sv.bg = id; load_bg(id); }
        else { sv.skin = id; load_skin(id); }
        game_save();
    }
    play_sfx(SFX_CLICK);
    if (reward_n) {
        memmove(reward_q, reward_q + 1, sizeof(reward_q[0]) * (reward_n - 1));
        reward_n--;
    }
    if (!reward_n) set_state(ui_return_state);
}

/* ---------------------------------------------------------------- reparto */
void ui_input(unsigned in)
{
    unsigned nav = in & (IN_UP | IN_DOWN | IN_LEFT | IN_RIGHT | IN_L | IN_R);
    if (nav && gstate != GS_REWARD) play_sfx(SFX_CLICK8);
    switch (gstate) {
    case GS_HOME: home_input(in); break;
    case GS_SELECT: select_input(in); break;
    case GS_PAUSE: pause_input(in); break;
    case GS_WIN: win_input(in); break;
    case GS_LOSE: lose_input(in); break;
    case GS_SHOP: shop_input(in); break;
    case GS_DAILY: daily_input(in); break;
    case GS_SETTINGS: settings_input(in); break;
    case GS_REWARD: reward_input(in); break;
    }
}

void ui_update(float dt)
{
    if (msg_t > 0) msg_t -= dt;
    win_t += dt;
    bundle_t += dt;
    if (bundle_t > 3) { bundle_t = 0; bundle_k++; }   /* BundleScrollController: pasa solo */
}

void ui_draw(void)
{
    unsigned t0 = plat_us();
    switch (gstate) {
    case GS_HOME: home_draw(); break;
    case GS_SELECT: select_draw(); break;
    case GS_PAUSE: pause_draw(); break;
    case GS_WIN: win_draw(); break;
    case GS_LOSE: lose_draw(); break;
    case GS_SHOP: shop_draw(); break;
    case GS_DAILY: daily_draw(); break;
    case GS_SETTINGS: settings_draw(); break;
    case GS_REWARD:
        if (ui_return_state == GS_WIN || ui_return_state == GS_PLAY) play_draw();
        reward_draw();
        break;
    }
    if (msg_t > 0) {
        float w = text_width(FONT_M, msg) + 24;
        gfx_fill((SCR_W - w) / 2, 220, w, 26, RGBA(0, 0, 0, 180));
        text_draw(SCR_W / 2, 224, FONT_M, msg, WHITE, 1);
    }
    dbg_sect_ms[5] = (plat_us() - t0) / 1000.0f;
}
