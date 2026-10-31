/* aw.c - apartado de Apple Worm (ver aw.h)
 *
 * Seleccion de nivel con la ventana, los botones de nivel, las estrellas, el candado y las flechas del original
 * (LevelSelect); selector de skins con su ventana de tienda y la vista previa de cada skin (skins.json).
 * Lo que en el original se paga con dinero real (typeOfGetting 3) se consigue aqui con monedas del juego.
 */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "game.h"
#include "aw.h"
#include "lang.h"
#include "audio.h"
#include "devlog.h"

int aw_mode;
LevelPack aw_levels;
Sheet aw_sheet;
Tex aw_bg;
static int16_t aidx[96];
static int naidx, loaded = -1;

#define AW_SKINS_MAX 24
static struct { char name[16]; uint8_t type; uint16_t count; } skins[AW_SKINS_MAX];
static int nskins_aw;
#define IAP_PRICE 2500             /* skins de pago del original: con monedas */

static int page, sel, skin_sel;

int aw_spr(int i) { return i >= 0 && i < naidx ? aidx[i] : -1; }

int aw_available(void)
{
    FILE *f = fopen("data/aw/aw_levels.bin", "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

int aw_open(void)
{
    if (loaded >= 0) return loaded;
    loaded = -1;
    if (levels_open(&aw_levels, "data/aw/aw_levels.bin")) {
        dlog_error("Apple Worm: falta data/aw/aw_levels.bin");
        return loaded = -1;
    }
    if (sheet_load(&aw_sheet, "data/aw/aw.spr")) dlog_error("Apple Worm: falta data/aw/aw.spr");
    if (bg_load(&aw_bg, "data/aw/aw_bg.tex")) dlog("AVISO Apple Worm sin fondo propio");
    FILE *f = fopen("data/aw/aw.idx", "rb");
    if (f) {
        uint8_t b[2];
        if (fread(b, 1, 2, f) == 2) naidx = b[0] | b[1] << 8;
        if (naidx > 96) naidx = 96;
        for (int i = 0; i < naidx; i++) {
            if (fread(b, 1, 2, f) != 2) break;
            aidx[i] = (int16_t)(b[0] | b[1] << 8);
        }
        fclose(f);
    }
    f = fopen("data/aw/aw_skins.bin", "rb");
    if (f) {
        uint8_t b[4];
        if (fread(b, 1, 2, f) == 2) nskins_aw = b[0] | b[1] << 8;
        if (nskins_aw > AW_SKINS_MAX) nskins_aw = AW_SKINS_MAX;
        for (int i = 0; i < nskins_aw; i++) {
            int l = fgetc(f);
            if (l < 0 || l > 15) break;
            if (fread(skins[i].name, 1, l, f) != (size_t)l) break;
            skins[i].name[l] = 0;
            if (fread(b, 1, 3, f) != 3) break;
            skins[i].type = b[0];
            skins[i].count = b[1] | b[2] << 8;
        }
        fclose(f);
    }
    dlog("Apple Worm: %d niveles, %d graficos, %d skins", aw_levels.count, naidx, nskins_aw);
    return loaded = 0;
}

int aw_level_won(int i) { return i >= 0 && i < AW_MAX_LEVELS && (sv.aw_won[i >> 3] >> (i & 7)) & 1; }
void aw_level_set_won(int i)
{
    if (i >= 0 && i < AW_MAX_LEVELS) sv.aw_won[i >> 3] |= 1 << (i & 7);
}
int aw_levels_won_count(void)
{
    int n = 0;
    for (int i = 0; i < aw_levels.count; i++) n += aw_level_won(i);
    return n;
}

static int skin_owned(int i) { return i < 32 && (sv.aw_skins_owned >> i) & 1; }

/* typeOfGetting 4: se abre al superar countForGet niveles (skinReward de levels.json) */
static void unlock_by_levels(void)
{
    int won = aw_levels_won_count();
    for (int i = 0; i < nskins_aw; i++)
        if (skins[i].type == 4 && won >= skins[i].count && !skin_owned(i)) {
            sv.aw_skins_owned |= 1u << i;
            ui_message(T("Skin nueva desbloqueada!"), 2.0f);
            dlog("APPLE WORM skin %s desbloqueada (%d niveles)", skins[i].name, won);
        }
}

void aw_on_win(int idx, int first)
{
    if (first) {
        /* coinsReward del nivel: 10 a 30 segun la dificultad; aqui no se guarda por nivel, se aproxima por tramo */
        int c = 10 + (idx / 30) * 5;
        if (c > 30) c = 30;
        sv.coins += c;
    }
    if (idx + 1 < aw_levels.count && sv.aw_cur <= idx) sv.aw_cur = idx + 1;
    unlock_by_levels();
}

void aw_enter(void)
{
    if (aw_open()) {
        ui_message(T("Faltan los datos de Apple Worm (data/aw)"), 2.0f);
        return;
    }
    page = sv.aw_cur / 15;
    sel = sv.aw_cur % 15;
    set_state(GS_AW_SELECT);
}

/* ---------------------------------------------------------------- seleccion de nivel */
static void fit(int id, float x, float y, float w, float h) { gfx_sprite_fit(&aw_sheet, aw_spr(id), x, y, w, h, WHITE); }

static void counters_aw(void)
{
    char b[32];
    fit(AWU_COIN, 20, 16, 22, 22);
    snprintf(b, sizeof(b), "%u", (unsigned)sv.coins);
    text_draw(34, 8, FONT_M, b, WHITE, 0);
}

static int level_open(int i) { return i <= sv.aw_cur || aw_level_won(i); }

void aw_select_draw(void)
{
    gfx_bg(aw_bg.pixels ? &aw_bg : &bg_tex, WHITE);
    int win = aw_spr(AWU_WINDOW_LEV);
    if (win >= 0) gfx_sprite_rect(&aw_sheet, win, 50, 22, 380, 224, WHITE);
    else draw_panel(50, 22, 380, 224);
    char b[48];
    int pages = (aw_levels.count + 14) / 15;
    snprintf(b, sizeof(b), "APPLE WORM   %d / %d", page + 1, pages);
    text_draw(SCR_W / 2, 30, FONT_L, b, WHITE, 1);
    for (int k = 0; k < 15; k++) {
        int i = page * 15 + k;
        if (i >= aw_levels.count) break;
        float x = 106 + (k % 5) * 67, y = 90 + (k / 5) * 54;
        int open = level_open(i), on = k == sel;
        float s = on ? 1.1f + 0.05f * sinf(gtime * 6) : 1.0f;
        if (open) {
            /* place_level; con skin de premio (levels.json skinReward: 10, 50 y 100) place_level_with_skin */
            int skin_lv = i + 1 == 10 || i + 1 == 50 || i + 1 == 100;
            fit(skin_lv ? AWU_PLACE_LEVEL_SKIN : AWU_PLACE_LEVEL, x, y, 46 * s, 44 * s);
            snprintf(b, sizeof(b), "%d", i + 1);
            text_draw(x, y - 12 * s, FONT_L, b, RGBA(110, 70, 40, 255), 1);
            /* estrellas: las tres al superarlo (el original las da segun pistas usadas) */
            int w = aw_level_won(i);
            fit(w ? AWU_STAR_L : AWU_STAR_L_G, x - 13, y + 19, 13, 13);
            fit(w ? AWU_STAR_C : AWU_STAR_C_G, x, y + 17, 15, 15);
            fit(w ? AWU_STAR_R : AWU_STAR_R_G, x + 13, y + 19, 13, 13);
        } else
            fit(AWU_LOCK, x, y, 40 * s, 42 * s);
    }
    if (page > 0) fit(AWU_ARROW_L, 36, 150, 26, 40);
    if (page < pages - 1) fit(AWU_ARROW_R, 444, 150, 26, 40);
    fit(AWU_SKINS, 446, 30, 38, 38);
    text_draw(446, 50, FONT_S, "SKINS", WHITE, 1);
    counters_aw();
    text_draw(SCR_W / 2, SCR_H - 16, FONT_S, T(BTN_X " Jugar   " BTN_TRI " Skins   " BTN_O " Volver   L/R Pagina"),
              RGBA(255, 255, 255, 220), 1);
}

void aw_select_input(unsigned in)
{
    int pages = (aw_levels.count + 14) / 15;
    if (in & (IN_UP | IN_DOWN | IN_LEFT | IN_RIGHT | IN_L | IN_R)) play_sfx(SFX_CLICK8);
    if (in & IN_LEFT) {
        if (sel % 5 > 0) sel--;
        else if (page > 0) { page--; sel += 4; }
    }
    if (in & IN_RIGHT) {
        if (sel % 5 < 4 && page * 15 + sel + 1 < aw_levels.count) sel++;
        else if (page < pages - 1) { page++; sel -= 4; }
    }
    if (in & IN_UP && sel >= 5) sel -= 5;
    if (in & IN_DOWN && sel + 5 < 15 && page * 15 + sel + 5 < aw_levels.count) sel += 5;
    if (in & IN_L && page > 0) page--;
    if (in & IN_R && page < pages - 1) page++;
    while (page * 15 + sel >= aw_levels.count && sel > 0) sel--;
    if (in & IN_CROSS) {
        int i = page * 15 + sel;
        if (level_open(i)) {
            play_sfx(SFX_CLICK);
            aw_mode = 1;
            play_start(i, 0);
        } else {
            play_sfx(SFX_STOP);
            ui_message(T("Supera los niveles anteriores para abrir este"), 1.5f);
        }
    }
    if (in & IN_TRIANGLE) {
        play_sfx(SFX_CLICK);
        skin_sel = sv.aw_skin < nskins_aw ? sv.aw_skin : 0;
        set_state(GS_AW_SKINS);
    }
    if (in & IN_CIRCLE) {
        play_sfx(SFX_CLICK);
        aw_mode = 0;
        set_state(GS_HOME);
    }
}

/* ---------------------------------------------------------------- selector de skins */
static int skin_price(int i) { return skins[i].type == 3 ? IAP_PRICE : skins[i].count; }

void aw_skins_draw(void)
{
    gfx_bg(aw_bg.pixels ? &aw_bg : &bg_tex, WHITE);
    int win = aw_spr(AWU_SHOP_WINDOW);
    if (win >= 0) gfx_sprite_rect(&aw_sheet, win, 30, 14, 420, 236, WHITE);
    else draw_panel(30, 14, 420, 236);
    text_draw(SCR_W / 2, 22, FONT_L, "SKINS", WHITE, 1);
    char b[48];
    for (int i = 0; i < nskins_aw; i++) {
        float x = 78 + (i % 6) * 65, y = 76 + (i / 6) * 50;
        int on = i == skin_sel;
        fit(i == sv.aw_skin ? AWU_PLACE_LEVEL_SKIN : AWU_PLACE_LEVEL, x, y, on ? 58 : 52, on ? 46 : 42);
        fit(AWU_PREVIEW0 + i, x, y - 2, on ? 46 : 40, on ? 36 : 32);
        if (i == sv.aw_skin) fit(AWU_CHECK, x + 20, y - 14, 16, 13);
        else if (!skin_owned(i)) fit(AWU_LOCK, x + 18, y + 12, 14, 14);
    }
    if (skin_sel < nskins_aw) {
        int i = skin_sel;
        if (i == sv.aw_skin) snprintf(b, sizeof(b), "%s  -  %s", skins[i].name, T("EQUIPADO"));
        else if (skin_owned(i)) snprintf(b, sizeof(b), "%s  -  " BTN_X " %s", skins[i].name, T("Equipar"));
        else if (skins[i].type == 4) snprintf(b, sizeof(b), T("%s  -  supera %d niveles"), skins[i].name, skins[i].count);
        else snprintf(b, sizeof(b), T("%s  -  " BTN_X " %d monedas"), skins[i].name, skin_price(i));
        text_draw(SCR_W / 2, 222, FONT_M, b, WHITE, 1);
    }
    counters_aw();
    text_draw(SCR_W / 2, SCR_H - 16, FONT_S, T(BTN_X " Elegir/comprar   " BTN_O " Volver"), RGBA(255, 255, 255, 220), 1);
}

void aw_skins_input(unsigned in)
{
    if (in & (IN_UP | IN_DOWN | IN_LEFT | IN_RIGHT)) play_sfx(SFX_CLICK8);
    if (in & IN_LEFT && skin_sel > 0) skin_sel--;
    if (in & IN_RIGHT && skin_sel + 1 < nskins_aw) skin_sel++;
    if (in & IN_UP && skin_sel >= 6) skin_sel -= 6;
    if (in & IN_DOWN && skin_sel + 6 < nskins_aw) skin_sel += 6;
    if (in & IN_CIRCLE) {
        play_sfx(SFX_CLICK);
        set_state(GS_AW_SELECT);
    }
    if (!(in & IN_CROSS) || skin_sel >= nskins_aw) return;
    int i = skin_sel;
    if (skin_owned(i)) {
        sv.aw_skin = i;
        play_sfx(SFX_CLICK);
    } else if (skins[i].type == 4) {
        play_sfx(SFX_STOP);
        ui_message(T("Se consigue superando niveles de Apple Worm"), 2.0f);
        return;
    } else if (sv.coins >= (unsigned)skin_price(i)) {
        sv.coins -= skin_price(i);
        sv.aw_skins_owned |= 1u << i;
        sv.aw_skin = i;
        play_sfx(SFX_UNLOCK);
        dlog("APPLE WORM skin %s comprada por %d monedas", skins[i].name, skin_price(i));
    } else {
        play_sfx(SFX_STOP);
        ui_message(T("Faltan monedas: se ganan superando niveles de Apple Worm"), 2.0f);
        return;
    }
    game_save();
}
