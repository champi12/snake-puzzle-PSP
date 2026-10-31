/* classic.c - modo clasico (ClassicGamePlay + ClassicController): la serpiente de siempre en un tablero de 13x12
 *
 * Del original: tablero de p0 (-7,-6) a p1 (5,5); la serpiente avanza sola, la cruceta cambia la direccion; al salir
 * por un borde entra por el contrario (IsLoop + clones); muere al morderse (IsDie); cada manzana suma 1 punto y sale
 * otra en una casilla libre al azar (CreateApple). Al final se compara con el ranking de ClassicLeaderboardData
 * (10 rivales, el primero entre minHighScore y maxHighScore, cada puesto hasta lowerRankScoreDiff menos) y se
 * regalan pistas por el puesto (firstPlaceHintReward, hintRewardForTop2To10).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "game.h"
#include "lang.h"
#include "audio.h"
#include "devlog.h"
#include "ui_ids.h"
#include "skin.h"

#define CW 13
#define CH 12
#define CMAX (CW * CH)

static Cell body[CMAX], prev_body[CMAX];
static uint8_t rev[CMAX];          /* BodyPartInfo.isReversed (ver logic.c) */
static int last_in;
static int n, prev_n, dir, next_dir, score, alive, apple_x, apple_y, started;
static float step_t, step_len, time_s, dead_t;
static int rank_pos, rank_reward;
static int board[10];
static unsigned rng = 12345;

static unsigned rnd(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}

static int occupied(int x, int y)
{
    for (int i = 0; i < n; i++)
        if (body[i].x == x && body[i].y == y) return 1;
    return 0;
}

static void new_apple(void)
{
    int free_n = CMAX - n, k;
    if (free_n <= 0) return;
    k = rnd() % free_n;
    for (int y = 0; y < CH; y++)
        for (int x = 0; x < CW; x++) {
            if (occupied(x, y)) continue;
            if (k-- == 0) {
                apple_x = x;
                apple_y = y;
                return;
            }
        }
}

void classic_start(void)
{
    rng ^= plat_ms() * 2654435761u;
    n = 3;
    dir = next_dir = D_RIGHT;
    /* prefab del modo clasico: la cabeza en (-2,67; 1,67) -> casilla (-3, 1), que desde p0 (-7,-6) es (4, 7) */
    for (int i = 0; i < n; i++) body[i] = (Cell){(int8_t)(4 - i), 7};
    memset(rev, 0, sizeof(rev));
    last_in = D_RIGHT;
    memcpy(prev_body, body, sizeof(body));
    prev_n = n;
    score = 0;
    alive = 1;
    started = 0;
    step_t = 0;
    step_len = 0.2f;   /* SnakeController.headMoveDuration del prefab clasico: velocidad fija, no acelera */
    time_s = 0;
    dead_t = 0;
    new_apple();
    /* ranking del dia (ClassicLeaderboardData): fijo durante la partida */
    int lo = meta.lb_min ? meta.lb_min : 120, hi = meta.lb_max ? meta.lb_max : 130, diff = meta.lb_diff ? meta.lb_diff : 8;
    srand(today_ymd());
    board[0] = lo + rand() % (hi - lo + 1);
    for (int i = 1; i < 10; i++) board[i] = board[i - 1] - 1 - rand() % diff;
    dbg_game.level = 0;
    dlog("CLASICO empieza (record %d, primero del ranking %d)", sv.classic_best, board[0]);
    set_state(GS_CLASSIC);
}

void classic_input(unsigned in)
{
    int d = -1;
    if (in & IN_UP) d = D_UP;
    else if (in & IN_DOWN) d = D_DOWN;
    else if (in & IN_LEFT) d = D_LEFT;
    else if (in & IN_RIGHT) d = D_RIGHT;
    if (d >= 0 && alive) {
        if (d != dir_opposite(dir)) next_dir = d;
        started = 1;
    }
    if (in & IN_START) started = !started;
    if (in & IN_CIRCLE) {
        play_sfx(SFX_CLICK);
        set_state(GS_HOME);
    }
}

static void finish(void)
{
    rank_pos = 10;
    for (int i = 0; i < 10; i++)
        if (score > board[i]) { rank_pos = i; break; }
    rank_reward = 0;
    if (rank_pos == 0) rank_reward = meta.lb_first ? meta.lb_first : 3;
    else if (rank_pos < 10) rank_reward = meta.lb_top10 ? meta.lb_top10 : 2;
    if (score > sv.classic_best) {
        sv.classic_best = score;
        if (!rank_reward) rank_reward = meta.lb_value ? meta.lb_value : 1;
    }
    sv.hints += rank_reward;
    game_save();
    dlog("CLASICO fin: %d puntos en %.0f s, puesto %d, +%d pistas", score, time_s, rank_pos + 1, rank_reward);
    set_state(GS_CLASSIC_END);
}

void classic_update(float dt)
{
    if (!alive) {
        dead_t += dt;
        if (dead_t > 1.0f) finish();   /* MediationManager.DelayCallBackByRealTime(1) */
        return;
    }
    if (!started) return;
    time_s += dt;
    step_t += dt;
    if (step_t < step_len) return;
    step_t -= step_len;
    memcpy(prev_body, body, sizeof(body));
    prev_n = n;
    dir = next_dir;
    int x = (body[0].x + DIR_DX[dir] + CW) % CW, y = (body[0].y + DIR_DY[dir] + CH) % CH;
    int grow = x == apple_x && y == apple_y;
    /* la cola se mueve a la vez: chocar con ella no mata salvo al crecer */
    for (int i = 0; i < n - (grow ? 0 : 1); i++)
        if (body[i].x == x && body[i].y == y) {
            alive = 0;
            play_sfx(SFX_LOSE);
            return;
        }
    if (grow && n < CMAX) n++;
    memmove(&body[1], &body[0], sizeof(Cell) * (n - 1));
    memmove(&rev[1], &rev[0], n - 1);
    body[0] = (Cell){(int8_t)x, (int8_t)y};
    rev[0] = dir == D_LEFT || (dir == D_DOWN && last_in == D_LEFT);
    last_in = dir;
    if (grow) {
        score++;
        play_sfx(SFX_EAT);
        new_apple();
    } else
        play_sfx(SFX_WORM);
}

void classic_draw(void)
{
    gfx_bg(&bg_tex, WHITE);
    float cs = 18, ox = (SCR_W - CW * cs) / 2, oy = 28;
    for (int y = 0; y < CH; y++)
        for (int x = 0; x < CW; x++)
            gfx_fill(ox + x * cs, oy + y * cs, cs, cs, (x + y) & 1 ? RGBA(120, 190, 90, 210) : RGBA(140, 205, 105, 210));
    /* serpiente: se reutiliza el dibujo de la tienda pieza a pieza con la interpolacion del paso */
    float t = started && alive ? step_t / step_len : 1;
    int sk_apple = SK_APPLE < skin_sheet.nspr ? SK_APPLE : -1;
    float ax = ox + (apple_x + 0.5f) * cs, ay = oy + (CH - 1 - apple_y + 0.5f) * cs;
    if (sk_apple >= 0) gfx_sprite(&skin_sheet, sk_apple, ax, ay, cs, 0, 0, WHITE);
    else gfx_sprite(&world, WSPR(W_APPLE), ax, ay, cs, 0, 0, WHITE);
    static GState st;
    static float xs[CMAX], ys[CMAX];
    st.n = n;
    st.dir = dir;
    st.dead = !alive;
    /* el cuerpo se queda en casillas enteras (con sus esquinas) y solo se deslizan la cabeza y la cola: la
     * serpiente avanza sin parar y, si se deslizara cada parte, a mitad de paso las curvas quedarian cortadas */
    for (int i = 0; i < n; i++) {
        float fx = body[i].x, fy = body[i].y;
        int slide = (i == 0 || (i == n - 1 && n == prev_n)) && i < prev_n;
        /* la cola solo se desliza en linea recta: en una curva se separaria del cuerpo */
        if (slide && i == n - 1 && n > 1 &&
            (body[i].x - prev_body[i].x != body[i - 1].x - body[i].x || body[i].y - prev_body[i].y != body[i - 1].y - body[i].y))
            slide = 0;
        if (slide) {
            float dx = body[i].x - prev_body[i].x, dy = body[i].y - prev_body[i].y;
            if (fabsf(dx) + fabsf(dy) <= 1.01f) {   /* al cruzar un borde no se desliza */
                fx = prev_body[i].x + dx * t;
                fy = prev_body[i].y + dy * t;
            }
        }
        st.s[i] = body[i];
        st.rev[i] = rev[i];
        xs[i] = ox + (fx + 0.5f) * cs;
        ys[i] = oy + (CH - 1 - fy + 0.5f) * cs;
    }
    draw_snake_screen(&st, xs, ys, cs);
    char b[64];
    gfx_fill(0, 0, SCR_W, 24, RGBA(0, 0, 0, 110));
    snprintf(b, sizeof(b), T("PUNTOS %d"), score);
    text_draw(10, 3, FONT_M, b, WHITE, 0);
    snprintf(b, sizeof(b), "%d:%02d", (int)time_s / 60, (int)time_s % 60);
    text_draw(SCR_W / 2, 3, FONT_M, b, WHITE, 1);
    snprintf(b, sizeof(b), T("RECORD %d"), sv.classic_best);
    text_draw(SCR_W - 10, 3, FONT_M, b, WHITE, 2);
    if (!started && alive && gstate == GS_CLASSIC)
        text_draw(SCR_W / 2, 120, FONT_L, T("Pulsa la cruceta para empezar"), WHITE, 1);
    if (gstate == GS_CLASSIC) text_draw(SCR_W / 2, SCR_H - 16, FONT_S, T("Cruceta Girar   START Pausa   " BTN_O " Salir"), RGBA(255, 255, 255, 200), 1);
}

void classic_end_draw(void)
{
    gfx_fill(0, 0, SCR_W, SCR_H, RGBA(0, 0, 0, 140));
    draw_panel(90, 10, 300, 250);
    text_draw(SCR_W / 2, 18, FONT_L, T(rank_pos == 0 ? "NUEVO PRIMER PUESTO!" : "RANKING"), RGBA(105, 58, 28, 255), 1);
    char b[64];
    int row = 0;
    for (int i = 0; i <= 10 && row < 11; i++) {
        int is_me = i == rank_pos;
        int bi = i - (i > rank_pos ? 1 : 0);
        if (!is_me && bi >= 10) break;
        const char *nm = is_me ? T("TU") : (bi < meta.nnames ? meta.names[bi] : "Rival");
        int sc = is_me ? score : board[bi];
        snprintf(b, sizeof(b), "%2d. %-10s %4d", i + 1, nm, sc);
        float y = 46 + row * 17;
        if (is_me) gfx_fill(110, y - 1, 260, 17, RGBA(250, 200, 40, 120));
        text_draw(130, y, FONT_S, b, RGBA(105, 58, 28, 255), 0);
        row++;
    }
    if (rank_reward) {
        snprintf(b, sizeof(b), T(rank_reward > 1 ? "Premio: +%d pistas" : "Premio: +%d pista"), rank_reward);
        text_draw(SCR_W / 2, 238, FONT_M, b, RGBA(200, 90, 30, 255), 1);
    }
    text_draw(SCR_W / 2, SCR_H - 16, FONT_S, T(BTN_X " Otra vez   " BTN_O " Inicio"), WHITE, 1);
}

void classic_end_input(unsigned in)
{
    if (in & IN_CROSS) { play_sfx(SFX_CLICK); classic_start(); }
    if (in & IN_CIRCLE) { play_sfx(SFX_CLICK); set_state(GS_HOME); }
}
