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

#define TXT_DARK RGBA(105, 58, 28, 255)   /* texto sobre los paneles claros (base_popup) */

int ui_return_state = GS_HOME;
static int sel;                    /* opcion elegida en la pantalla actual */
static int page;
static int shop_tab;               /* 0 aspectos, 1 fondos, 2 packs (la tienda de pago del original, con fichas) */
static char msg[128];
static float msg_t;
static int reward_q[8][2], reward_n;
static int confirm_reset;

/* orden de la tienda: ShopRegionUI.sortIndex del prefab screens/shop */
static const int SKIN_ORDER[] = {0, 20, 2, 15, 21, 27, 23, 28, 16, 10, 17, 3, 4, 22, 6, 11, 13, 7, 1, 18, 8, 9, 19, 12, 5,
                                 14, 24, 25, 26};
static const int BG_ORDER[] = {0, 1, 11, 8, 9, 12, 2, 13, 7, 6, 10, 4, 5, 3};
static int shop_list[64], shop_count;

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

/* ---------------------------------------------------------------- inicio */
enum { H_PLAY, H_LEVELS, H_CLASSIC, H_SHOP, H_DAILY, H_SETTINGS, H_COUNT };
static const char *home_labels[H_COUNT] = {"JUGAR", "NIVELES", "CLASICO", "TIENDA", "DIARIO", "AJUSTES"};

static void home_draw(void)
{
    gfx_bg(home_tex.pixels ? &home_tex : &bg_tex, WHITE);
    int logo = WSPR(UI_LOGO);
    if (logo >= 0) {
        float s = 150.0f / (spr_w(&world, logo) > 0 ? spr_w(&world, logo) : 1);
        if (spr_h(&world, logo) * s > 110) s = 110 / spr_h(&world, logo);
        gfx_sprite(&world, logo, 130, 92 + sinf(gtime * 2) * 3, s, 0, 0, WHITE);
    } else
        text_draw(130, 70, FONT_XL, "Snake Puzzle", WHITE, 1);
    draw_snake_preview(130, 178, 22, 5);
    for (int i = 0; i < H_COUNT; i++) {
        char b[48];
        if (i == H_PLAY) snprintf(b, sizeof(b), T("JUGAR  (nivel %d)"), sv.cur_level + 1);
        else snprintf(b, sizeof(b), "%s%s", T(home_labels[i]), i == H_DAILY && daily_ready() ? "  (!)" : "");
        draw_button(270, 26 + i * 37, 190, 32, b, sel == i, 1);
    }
    counters(SCR_H - 40);
    help(T(BTN_X " Aceptar   Cruceta Elegir"));
#if DEVLOG
    {
        /* para saber sin buscar archivos si el devlog se escribe y si el audio arranco */
        char b[200];
        snprintf(b, sizeof(b), "PRUEBAS | devlog: %s (%s)", dlog_path(), dlog_ok() ? "OK" : "SIN ESCRIBIR");
        text_draw(SCR_W - 4, SCR_H - 44, FONT_S, b, RGBA(255, 230, 120, 255), 2);
        text_draw(SCR_W - 4, SCR_H - 30, FONT_S, audio_status(), RGBA(255, 230, 120, 255), 2);
    }
#endif
}

static void home_input(unsigned in)
{
    if (in & IN_UP) sel = (sel + H_COUNT - 1) % H_COUNT;
    if (in & IN_DOWN) sel = (sel + 1) % H_COUNT;
    if (!(in & IN_CROSS)) return;
    play_sfx(SFX_CLICK);
    switch (sel) {
    case H_PLAY: play_start(sv.cur_level, 0); break;
    case H_LEVELS: page = sv.cur_level / 15; sel = sv.cur_level % 15; set_state(GS_SELECT); break;
    case H_CLASSIC: classic_start(); break;
    case H_SHOP: shop_tab = 0; sel = 0; set_state(GS_SHOP); break;
    case H_DAILY: sel = 0; set_state(GS_DAILY); break;
    case H_SETTINGS: sel = 0; confirm_reset = 0; set_state(GS_SETTINGS); break;
    }
}

/* ---------------------------------------------------------------- niveles (SelectLevelController: 15 por pagina) */
static int level_open(int i) { return i <= sv.cur_level || level_won(i); }

/* selector de niveles con los graficos del de Apple Worm (data/lvsel.spr, tools/lvsel.py); si no estan, el propio */
enum { LV_WINDOW, LV_PLACE, LV_STAR_L, LV_STAR_C, LV_STAR_R, LV_STAR_L_G, LV_STAR_C_G, LV_STAR_R_G, LV_LOCK, LV_ARROW_L,
       LV_ARROW_R, LV_SKINS, LV_COUNT };
static Sheet lv_sheet;
static int16_t lv_idx[LV_COUNT];
static int lv_state;   /* 0 sin probar, 1 cargado, -1 no hay */

static int lvsel_ok(void)
{
    if (lv_state) return lv_state > 0;
    lv_state = -1;
    for (int k = 0; k < LV_COUNT; k++) lv_idx[k] = -1;
    FILE *f = fopen("data/lvsel.idx", "rb");
    if (!f) return 0;
    uint8_t b[2];
    int n = 0;
    if (fread(b, 1, 2, f) == 2) n = b[0] | b[1] << 8;
    for (int k = 0; k < n && k < LV_COUNT; k++) {
        if (fread(b, 1, 2, f) != 2) break;
        lv_idx[k] = (int16_t)(b[0] | b[1] << 8);
    }
    fclose(f);
    if (sheet_load(&lv_sheet, "data/lvsel.spr") == 0 && lv_idx[LV_PLACE] >= 0) lv_state = 1;
    return lv_state > 0;
}

static void lv_fit(int k, float x, float y, float w, float h)
{
    if (lv_idx[k] >= 0) gfx_sprite_fit(&lv_sheet, lv_idx[k], x, y, w, h, WHITE);
}

static void select_draw_lvsel(void)
{
    gfx_bg(&bg_tex, WHITE);
    if (lv_idx[LV_WINDOW] >= 0) gfx_sprite_rect(&lv_sheet, lv_idx[LV_WINDOW], 50, 22, 380, 224, WHITE);
    else draw_panel(50, 22, 380, 224);
    char b[48];
    int pages = (levels.count + 14) / 15;
    snprintf(b, sizeof(b), T("NIVELES   %d / %d"), page + 1, pages);
    text_draw(SCR_W / 2, 30, FONT_L, b, WHITE, 1);
    for (int k = 0; k < 15; k++) {
        int i = page * 15 + k;
        if (i >= levels.count) break;
        float x = 106 + (k % 5) * 67, y = 90 + (k / 5) * 54;
        int on = k == sel;
        float s = on ? 1.1f + 0.05f * sinf(gtime * 6) : 1.0f;
        if (level_open(i)) {
            lv_fit(LV_PLACE, x, y, 46 * s, 44 * s);
            snprintf(b, sizeof(b), "%d", i + 1);
            text_draw(x, y - 12 * s, FONT_L, b, RGBA(110, 70, 40, 255), 1);
            int w = level_won(i);   /* tres estrellas al superarlo */
            lv_fit(w ? LV_STAR_L : LV_STAR_L_G, x - 13, y + 19, 13, 13);
            lv_fit(w ? LV_STAR_C : LV_STAR_C_G, x, y + 17, 15, 15);
            lv_fit(w ? LV_STAR_R : LV_STAR_R_G, x + 13, y + 19, 13, 13);
        } else
            lv_fit(LV_LOCK, x, y, 40 * s, 42 * s);
    }
    if (page > 0) lv_fit(LV_ARROW_L, 36, 150, 26, 40);
    if (page < pages - 1) lv_fit(LV_ARROW_R, 444, 150, 26, 40);
    lv_fit(LV_SKINS, 446, 30, 38, 38);
    counters(SCR_H - 40);
    help(T(BTN_X " Jugar   " BTN_TRI " Tienda   " BTN_O " Volver   L/R Pagina"));
}

static void select_draw(void)
{
    if (lvsel_ok()) {
        select_draw_lvsel();
        return;
    }
    gfx_bg(&bg_tex, WHITE);
    gfx_fill(0, 0, SCR_W, SCR_H, RGBA(0, 0, 0, 60));
    int pages = (levels.count + 14) / 15;
    char b[48];
    snprintf(b, sizeof(b), T("NIVELES   %d / %d"), page + 1, pages);
    text_draw(SCR_W / 2, 6, FONT_L, b, WHITE, 1);
    for (int k = 0; k < 15; k++) {
        int i = page * 15 + k;
        if (i >= levels.count) break;
        float x = 52 + (k % 5) * 94, y = 58 + (k / 5) * 66;
        int open = level_open(i);
        float s = (k == sel) ? 1.1f + 0.04f * sinf(gtime * 6) : 1.0f;
        int id = WSPR(open ? (level_won(i) ? UI_BTN_GREEN : UI_BTN_BLUE) : UI_BTN_GRAY);
        if (id >= 0) gfx_sprite_rect(&world, id, x - 34 * s, y - 26 * s, 68 * s, 52 * s, WHITE);
        else gfx_fill(x - 34, y - 26, 68, 52, open ? RGBA(90, 160, 60, 255) : RGBA(90, 90, 90, 255));
        snprintf(b, sizeof(b), "%d", i + 1);
        text_draw(x, y - 14, FONT_L, b, open ? WHITE : RGBA(255, 255, 255, 110), 1);
        if (!open) gfx_sprite_fit(&world, WSPR(UI_SLOT_LOCKED), x + 22, y + 14, 20, 20, WHITE);
        if (level_won(i)) gfx_sprite_fit(&world, WSPR(UI_ICON_TICK), x + 24, y + 16, 22, 22, WHITE);
    }
    help(T(BTN_X " Jugar   " BTN_O " Volver   L/R Cambiar de pagina"));
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
    if (in & IN_CIRCLE) { play_sfx(SFX_CLICK); sel = H_LEVELS; set_state(GS_HOME); }
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
static void pause_draw(void)
{
    gfx_fill(0, 0, SCR_W, SCR_H, RGBA(0, 0, 0, 120));
    draw_panel(140, 30, 200, 215);
    text_draw(SCR_W / 2, 40, FONT_L, T("PAUSA"), TXT_DARK, 1);
    static const char *L[] = {"CONTINUAR", "REINICIAR", "NIVELES", "INICIO"};
    for (int i = 0; i < 4; i++) draw_button(165, 76 + i * 38, 150, 32, T(L[i]), sel == i, 1);
    gfx_sprite_fit(&world, WSPR(sv.sound ? UI_SOUND_ON : UI_SOUND_OFF), 172, 234, 30, 30, WHITE);
    gfx_sprite_fit(&world, WSPR(sv.music ? UI_MUSIC_ON : UI_MUSIC_OFF), 308, 234, 30, 30, WHITE);
    help(T(BTN_X " Aceptar   " BTN_SQ " Sonido   " BTN_TRI " Musica   START Seguir"));
}

static void toggle_audio(unsigned in)
{
    if (in & IN_SQUARE) { sv.sound ^= 1; audio_set_enabled(sv.sound, sv.music); game_save(); }
    if (in & IN_TRIANGLE) { sv.music ^= 1; audio_set_enabled(sv.sound, sv.music); game_save(); }
}

static void pause_input(unsigned in)
{
    if (in & IN_UP) sel = (sel + 3) % 4;
    if (in & IN_DOWN) sel = (sel + 1) % 4;
    toggle_audio(in);
    if (in & (IN_START | IN_CIRCLE)) { set_state(GS_PLAY); return; }
    if (!(in & IN_CROSS)) return;
    play_sfx(SFX_CLICK);
    switch (sel) {
    case 0: set_state(GS_PLAY); break;
    case 1: play_start(play_idx, 0); break;
    case 2:
        page = play_idx / 15; sel = play_idx % 15; set_state(GS_SELECT); break;
    case 3: sel = 0; set_state(GS_HOME); break;
    }
    if (gstate != GS_PAUSE && gstate != GS_SELECT) sel = 0;
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
    int icon = type ? meta.bg_icon[id & 31] : meta.skin_icon[id & 31];
    if (icon >= 0) gfx_sprite_fit(&shop_sheet, icon, x + w + 26, y + 8, 40, 40, f >= 1 ? WHITE : RGBA(120, 120, 120, 255));
}

static void win_draw(void)
{
    gfx_fill(0, 0, SCR_W, SCR_H, RGBA(0, 0, 0, 110));
    draw_panel(110, 40, 260, 200);
    int ban = WSPR(UI_BANNER_WIN);
    if (ban >= 0) gfx_sprite_rect(&world, ban, 140, 18, 200, 50, WHITE);
    text_draw(SCR_W / 2, 30, FONT_L, T("SUPERADO!"), WHITE, 1);
    char b[96];
    snprintf(b, sizeof(b), T("Nivel %d completado"), play_idx + 1);
    text_draw(SCR_W / 2, 74, FONT_M, b, TXT_DARK, 1);
    /* top_player.txt: porcentaje de jugadores que no llegaron a este nivel */
    int pct = play_idx < meta.ntop ? meta.top[play_idx] : 0;
    if (pct) {
        snprintf(b, sizeof(b), T("Mejor que el %d,%02d%% de los jugadores"), pct / 100, pct % 100);
        text_draw(SCR_W / 2, 98, FONT_S, b, RGBA(200, 90, 30, 255), 1);
    }
    progress_bar(140, 124, 150);
    static const char *L[] = {"SIGUIENTE", "REPETIR", "INICIO"};
    for (int i = 0; i < 3; i++) draw_button(130 + i * 75, 186, 70, 30, T(L[i]), sel == i, 1);
    help(T(BTN_X " Aceptar   Cruceta Elegir"));
}

static void win_input(unsigned in)
{
    if (in & IN_LEFT) sel = (sel + 2) % 3;
    if (in & IN_RIGHT) sel = (sel + 1) % 3;
    if (!(in & IN_CROSS)) return;
    play_sfx(SFX_CLICK);
    if (sel == 0) play_start(play_idx + 1 < levels.count ? play_idx + 1 : play_idx, 0);
    else if (sel == 1) play_start(play_idx, 0);
    else set_state(GS_HOME);
    sel = 0;
}

static void lose_draw(void)
{
    gfx_fill(0, 0, SCR_W, SCR_H, RGBA(0, 0, 0, 110));
    draw_panel(110, 50, 260, 180);
    int ban = WSPR(UI_BANNER_LOSE);
    if (ban >= 0) gfx_sprite_rect(&world, ban, 140, 28, 200, 50, WHITE);
    text_draw(SCR_W / 2, 40, FONT_L, T("OH, NO!"), WHITE, 1);
    gfx_sprite_fit(&world, WSPR(UI_ICON_LOSE), SCR_W / 2, 112, 110, 60, WHITE);
    static const char *L[] = {"REINTENTAR", "PISTA", "SALTAR", "INICIO"};
    for (int i = 0; i < 4; i++) {
        char b[32];
        if (i == 1) snprintf(b, sizeof(b), T("PISTA (%d)"), sv.hints);
        else if (i == 2) snprintf(b, sizeof(b), T("SALTAR (%d)"), sv.skips);
        else snprintf(b, sizeof(b), "%s", T(L[i]));
        draw_button(125 + (i % 2) * 118, 150 + (i / 2) * 36, 112, 30, b, sel == i,
                    i == 1 ? sv.hints > 0 : i == 2 ? sv.skips > 0 : 1);
    }
    help(T(BTN_X " Aceptar   Cruceta Elegir"));
}

static void lose_input(unsigned in)
{
    if (in & (IN_LEFT | IN_RIGHT)) sel ^= 1;
    if (in & (IN_UP | IN_DOWN)) sel ^= 2;
    if (in & IN_CIRCLE) { sel = 0; play_start(play_idx, 0); return; }
    if (!(in & IN_CROSS)) return;
    play_sfx(SFX_CLICK);
    int s = sel;
    sel = 0;
    if (s == 0) play_start(play_idx, 0);
    else if (s == 1) {
        if (sv.hints > 0) { sv.hints--; game_save(); play_start(play_idx, 1); }
        else sel = 1;
    } else if (s == 2) {
        if (sv.skips > 0) {
            sv.skips--;
            level_set_won(play_idx);
            if (play_idx + 1 < levels.count && sv.cur_level <= play_idx) sv.cur_level = play_idx + 1;
            game_save();
            play_start(play_idx + 1 < levels.count ? play_idx + 1 : play_idx, 0);
        } else sel = 2;
    } else set_state(GS_HOME);
}

/* ---------------------------------------------------------------- tienda */
void ui_open_shop(void)
{
    shop_tab = 0;
    sel = 0;
    set_state(GS_SHOP);
}
static const ShopItemDef *item_def(int type, int id)
{
    for (int i = 0; i < meta.nshop; i++)
        if (meta.shop[i].type == type && meta.shop[i].id == id) return &meta.shop[i];
    return NULL;
}

static void shop_build(void)
{
    shop_count = 0;
    int total = shop_tab ? nbgs : nskins;
    const int *ord = shop_tab ? BG_ORDER : SKIN_ORDER;
    int nord = shop_tab ? (int)(sizeof(BG_ORDER) / sizeof(int)) : (int)(sizeof(SKIN_ORDER) / sizeof(int));
    int used[32] = {0};
    for (int i = 0; i < nord; i++)
        if (ord[i] < total && !used[ord[i]]) { shop_list[shop_count++] = ord[i]; used[ord[i]] = 1; }
    for (int i = 0; i < total && i < 32; i++)
        if (!used[i]) shop_list[shop_count++] = i;
}

/* icono de un articulo de la tienda */
static void item_icon(int tab, int id, float x, float y, float w, float h, uint32_t col)
{
    int icon = tab ? meta.bg_icon[id & 31] : meta.skin_icon[id & 31];
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
    if (!d) return 5;
    if (d->state == 4) {
        *why = T("Diario o fichas");
        return d->need ? d->need : 3;
    }
    return d->need ? d->need : 1;
}

/* ---- packs (ShopIAPController / SpecialBundle / HalloweenBundle / NoAdsBundle): en la PSP se pagan con fichas ---- */
static int pack_list[16], pack_count;

static int pack_valid(int k)
{
    int useful = 0;
    for (int j = 0; j < meta.pack[k].n; j++) {
        int t = meta.pack[k].type[j], q = meta.pack[k].qty[j];
        if (t == RW_HINT || t == RW_SKIP) useful = 1;
        if (t == RW_SKIN && q < nskins) useful = 1;
        if (t == RW_BG && q < nbgs) useful = 1;
    }
    if (meta.pack[k].id == 11 && event_now() != 1) return 0;   /* HalloweenBundle: solo en su evento */
    return useful;
}

static void pack_build(void)
{
    pack_count = 0;
    for (int k = 0; k < meta.npack; k++)
        if (pack_valid(k)) pack_list[pack_count++] = k;
}

/* precio en fichas: 1 por pista o salto (el original da 1 pista por anuncio visto) y 5 por aspecto o fondo, con
 * descuento en los paquetes grandes como en la tienda original */
static int pack_price(int k)
{
    int c = 0;
    for (int j = 0; j < meta.pack[k].n; j++) {
        int t = meta.pack[k].type[j], q = meta.pack[k].qty[j];
        if (t == RW_HINT || t == RW_SKIP) c += q;
        else if (t == RW_SKIN || t == RW_BG) c += 5;
    }
    if (c >= 10) c = c * 3 / 4;
    return c < 1 ? 1 : c;
}

static void pack_text(int k, char *b, int n)
{
    b[0] = 0;
    for (int j = 0; j < meta.pack[k].n; j++) {
        int t = meta.pack[k].type[j], q = meta.pack[k].qty[j];
        char p[32] = "";
        if (t == RW_HINT) snprintf(p, sizeof(p), T("%d pistas"), q);
        else if (t == RW_SKIP) snprintf(p, sizeof(p), T("%d saltos"), q);
        else if (t == RW_SKIN && q < nskins) snprintf(p, sizeof(p), "%s", T("aspecto"));
        else if (t == RW_BG && q < nbgs) snprintf(p, sizeof(p), "%s", T("fondo"));
        if (!p[0]) continue;
        if (b[0]) strncat(b, " + ", n - strlen(b) - 1);
        strncat(b, p, n - strlen(b) - 1);
    }
}

static void packs_draw(void)
{
    pack_build();
    if (sel >= pack_count) sel = pack_count ? pack_count - 1 : 0;
    for (int r = 0; r < pack_count && r < 8; r++) {
        int k = pack_list[r];
        float y = 40 + r * 25;
        gfx_fill(20, y, 440, 23, r == sel ? RGBA(250, 200, 40, 200) : RGBA(0, 0, 0, 110));
        char b[96];
        pack_text(k, b, sizeof(b));
        int icon = WSPR(UI_ICON_HINT);
        for (int j = 0; j < meta.pack[k].n; j++)
            if (meta.pack[k].type[j] == RW_SKIN && meta.pack[k].qty[j] < nskins) icon = -2 - meta.pack[k].qty[j];
        if (icon >= 0) gfx_sprite_fit(&world, icon, 36, y + 11, 20, 20, WHITE);
        else gfx_sprite_fit(&shop_sheet, meta.skin_icon[(-2 - icon) & 31], 36, y + 11, 22, 22, WHITE);
        text_draw(54, y + 3, FONT_M, b, WHITE, 0);
        snprintf(b, sizeof(b), T("%d fichas"), pack_price(k));
        text_draw(450, y + 3, FONT_M, b, sv.tokens >= pack_price(k) ? RGBA(150, 255, 150, 255) : RGBA(255, 150, 150, 255), 2);
    }
    if (!pack_count) text_draw(SCR_W / 2, 120, FONT_M, T("No hay paquetes"), WHITE, 1);
}

static void packs_buy(void)
{
    pack_build();
    if (sel >= pack_count) return;
    int k = pack_list[sel], price = pack_price(k);
    if (sv.tokens < price) {
        play_sfx(SFX_STOP);
        ui_message(T("Faltan fichas: ganas 1 por cada nivel nuevo superado"), 2.5f);
        return;
    }
    sv.tokens -= price;
    for (int j = 0; j < meta.pack[k].n; j++) {
        int t = meta.pack[k].type[j], q = meta.pack[k].qty[j];
        if (t == RW_HINT) sv.hints += q;
        else if (t == RW_SKIP) sv.skips += q;
        else if (t == RW_SKIN && q < nskins && !skin_available(q)) { sv.skins_owned |= 1u << q; reward_popup(0, q); }
        else if (t == RW_BG && q < nbgs && !bg_available(q)) { sv.bgs_owned |= 1u << q; reward_popup(1, q); }
    }
    play_sfx(SFX_REWARD);
    char b[96], m[120];
    pack_text(k, b, sizeof(b));
    snprintf(m, sizeof(m), T("Comprado: %s"), b);
    ui_message(m, 2.0f);
    dlog("TIENDA paquete %d por %d fichas", meta.pack[k].id, price);
    game_save();
}

static void shop_draw(void)
{
    if (shop_tab) {
        static int prev = -1;
        int id = shop_list[sel];
        if (prev != id && bg_available(id)) { /* vista previa del fondo elegido */ }
        prev = id;
    }
    gfx_bg(&bg_tex, WHITE);
    gfx_fill(0, 0, SCR_W, SCR_H, RGBA(0, 0, 0, 70));
    shop_build();
    if (sel >= shop_count) sel = shop_count - 1;
    if (sel < 0) sel = 0;
    text_draw(120, 4, FONT_L, T("TIENDA"), WHITE, 1);
    draw_button(196, 4, 90, 26, T("ASPECTOS"), shop_tab == 0, 1);
    draw_button(290, 4, 90, 26, T("FONDOS"), shop_tab == 1, 1);
    draw_button(384, 4, 90, 26, "PACKS", shop_tab == 2, 1);
    if (shop_tab == 2) {
        packs_draw();
        counters(SCR_H - 40);
        help(T(BTN_X " Comprar con fichas   " BTN_O " Volver   L/R Pestana"));
        return;
    }
    /* vista previa */
    draw_panel(8, 40, 200, 196);
    int id = shop_list[sel];
    if (shop_tab == 0 && id == sv.skin) draw_snake_preview(108, 112, 26, 5);
    else {
        item_icon(shop_tab, id, 108, 112, shop_tab ? 170 : 110, 110, WHITE);
    }
    char b[64];
    int owned = shop_tab ? bg_available(id) : skin_available(id);
    int equipped = shop_tab ? sv.bg == id : sv.skin == id;
    const char *why = NULL;
    int need = owned ? 0 : item_need(shop_tab, id, &why);
    if (equipped) snprintf(b, sizeof(b), "%s", T("EQUIPADO"));
    else if (owned) snprintf(b, sizeof(b), "%s", T(BTN_X " Equipar"));
    else if (need > 0) snprintf(b, sizeof(b), T(BTN_X " Pagar ficha (%d/%d)"), shop_tab ? sv.bg_paid[id] : sv.skin_paid[id], need);
    else snprintf(b, sizeof(b), "%s", T("Bloqueado"));
    text_draw(108, 182, FONT_M, b, TXT_DARK, 1);
    if (!owned && why) text_draw(108, 206, FONT_S, why, RGBA(200, 90, 30, 255), 1);
    /* cuadricula */
    /* 4 filas a la vista: la cuadricula se desplaza con el cursor */
    int first_row = sel / 5 > 3 ? sel / 5 - 3 : 0;
    int rows = (shop_count + 4) / 5;
    for (int k = first_row * 5; k < shop_count; k++) {
        int i = shop_list[k];
        float x = 240 + (k % 5) * 47, y = 58 + (k / 5 - first_row) * 46;
        if (y > 220) break;
        int own = shop_tab ? bg_available(i) : skin_available(i);
        int slot = WSPR(own ? UI_SLOT_UNLOCKED : UI_SLOT_LOCKED);
        float s = k == sel ? 44 : 40;
        if (slot >= 0) gfx_sprite_rect(&world, slot, x - s / 2, y - s / 2, s, s, WHITE);
        else gfx_fill(x - s / 2, y - s / 2, s, s, own ? RGBA(90, 160, 60, 255) : RGBA(90, 90, 90, 255));
        item_icon(shop_tab, i, x, y, s * 0.8f, s * 0.8f, own ? WHITE : RGBA(110, 110, 110, 255));
        if ((shop_tab ? sv.bg : sv.skin) == i) gfx_sprite_fit(&world, WSPR(UI_ICON_TICK), x + 14, y + 14, 18, 18, WHITE);
    }
    /* flechas si hay filas por arriba o por abajo */
    if (first_row > 0) text_draw(352, 30, FONT_M, "^", WHITE, 1);
    if (first_row + 4 < rows) text_draw(352, 226, FONT_M, "v", WHITE, 1);
    counters(SCR_H - 40);
    help(T(BTN_X " Elegir/pagar   " BTN_O " Volver   L/R Pestana"));
}

static void shop_input(unsigned in)
{
    shop_build();
    if (in & IN_L) { shop_tab = (shop_tab + 2) % 3; sel = 0; }
    if (in & IN_R) { shop_tab = (shop_tab + 1) % 3; sel = 0; }
    if (shop_tab == 2) {
        pack_build();
        if (in & IN_UP && sel > 0) sel--;
        if (in & IN_DOWN && sel + 1 < pack_count) sel++;
        if (in & IN_CIRCLE) { play_sfx(SFX_CLICK); sel = H_SHOP; set_state(GS_HOME); return; }
        if (in & IN_CROSS) packs_buy();
        return;
    }
    shop_build();
    if (in & IN_LEFT && sel > 0) sel--;
    if (in & IN_RIGHT && sel + 1 < shop_count) sel++;
    if (in & IN_UP && sel >= 5) sel -= 5;
    if (in & IN_DOWN && sel + 5 < shop_count) sel += 5;
    if (in & IN_CIRCLE) { play_sfx(SFX_CLICK); sel = H_SHOP; set_state(GS_HOME); return; }
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
    if (need <= 0) { play_sfx(SFX_STOP); ui_message(why ? why : T("Bloqueado"), 1.5f); return; }
    if (!sv.tokens) { play_sfx(SFX_STOP); ui_message(T("Sin fichas: gana 1 ficha por cada nivel nuevo superado"), 2.5f); return; }
    sv.tokens--;
    uint8_t *paid = shop_tab ? &sv.bg_paid[id] : &sv.skin_paid[id];
    (*paid)++;
    play_sfx(SFX_CLICK8);
    if (*paid >= need) {
        if (shop_tab) sv.bgs_owned |= 1u << id;
        else sv.skins_owned |= 1u << id;
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
            int icon = meta.skin_icon[d->value & 31];
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
    if (in & IN_CIRCLE) { play_sfx(SFX_CLICK); sel = H_DAILY; set_state(GS_HOME); return; }
    if (!(in & IN_CROSS) || !daily_ready()) return;
    int n = meta.ndaily ? meta.ndaily : 7;
    int day = sv.daily_day % n;
    const DailyDef *d = &meta.daily[day];
    sv.daily_day = (day + 1) % n;   /* al terminar los 7 dias se vuelve a empezar */
    sv.daily_last = today_ymd();
    if (d->kind == 1) {
        if (!skin_available(d->value)) {
            sv.skins_owned |= 1u << d->value;
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
    gfx_bg(&bg_tex, WHITE);
    gfx_fill(0, 0, SCR_W, SCR_H, RGBA(0, 0, 0, 90));
    draw_panel(110, 20, 260, 230);
    text_draw(SCR_W / 2, 30, FONT_L, T("AJUSTES"), TXT_DARK, 1);
    char b[64];
    snprintf(b, sizeof(b), T("SONIDO: %s"), T(sv.sound ? "SI" : "NO"));
    draw_button(150, 70, 180, 32, b, sel == 0, 1);
    snprintf(b, sizeof(b), T("MUSICA: %s"), T(sv.music ? "SI" : "NO"));
    draw_button(150, 110, 180, 32, b, sel == 1, 1);
    draw_button(150, 150, 180, 32, T(confirm_reset ? "PULSA OTRA VEZ" : "BORRAR PARTIDA"), sel == 2, 1);
    unsigned m = sv.play_seconds / 60;
    snprintf(b, sizeof(b), T("%d niveles superados | %uh %02um jugadas"), levels_won_count(), m / 60, m % 60);
    text_draw(SCR_W / 2, 196, FONT_S, b, TXT_DARK, 1);
    text_draw(SCR_W / 2, 214, FONT_S, T("Snake Puzzle 2.3.4 - port para PSP"), TXT_DARK, 1);
    help(T(BTN_X " Cambiar   " BTN_O " Volver"));
}

extern void save_defaults(void);

static void settings_input(unsigned in)
{
    if (in & IN_UP) { sel = (sel + 2) % 3; confirm_reset = 0; }
    if (in & IN_DOWN) { sel = (sel + 1) % 3; confirm_reset = 0; }
    if (in & IN_CIRCLE) { play_sfx(SFX_CLICK); sel = H_SETTINGS; set_state(GS_HOME); return; }
    if (!(in & IN_CROSS)) return;
    play_sfx(SFX_CLICK);
    if (sel == 0) sv.sound ^= 1;
    if (sel == 1) sv.music ^= 1;
    if (sel == 2) {
        if (!confirm_reset) confirm_reset = 1;
        else {
            save_defaults();
            load_skin(0);
            load_bg(0);
            confirm_reset = 0;
            ui_message(T("Partida borrada"), 1.5f);
            dlog("AJUSTES partida borrada");
        }
    }
    audio_set_enabled(sv.sound, sv.music);
    game_save();
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
    int icon = type ? meta.bg_icon[id & 31] : meta.skin_icon[id & 31];
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
