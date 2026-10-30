/* play.c - un nivel: movimientos con animacion, pistas, deshacer, victoria y derrota, y el dibujo del nivel
 *
 * La logica (logic.c) es instantanea y devuelve una lista de fotos; aqui se recorren con las duraciones del original
 * (SnakeController.moveDuration 0,1 s, fallDuration 0,1 s) interpolando cada parte de la serpiente.
 */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "game.h"
#include "audio.h"
#include "devlog.h"
#include "ui_ids.h"
#include "skin.h"

#define MOVE_T 0.10f
#define FALL_T 0.08f
#define UNDO_MAX 128

Level cur_level;
int play_idx = -1;
int play_hint;                 /* modo pista: solo se acepta el siguiente paso de la solucion */
static int hint_pos;
static GLevel G;
static GState S, shown_from;
static GSteps steps;
static int step_i;             /* foto que se esta animando */
static float step_t;
static GState undo_buf[UNDO_MAX];
static int undo_n;
static int moves;
static float cs, ox, oy;       /* tamano de casilla y origen en pantalla */
static float blink_t, eat_t, end_t;
static int queued_dir = -1;
static float repeat_t;
static int held_dir = -1;
static float win_t;            /* entrada en el portal (StartAutoMove) */
#define ENTER_T 0.075f         /* segundos por casilla al entrar */
static float goal_ang, saw_ang;

static int animating(void) { return step_i < steps.n; }
static void face_update(float dt);
static void face_reset(void);

static void layout(void)
{
    /* casillas de 16 a 36 px; la franja de arriba es para el nivel y los contadores */
    float top = 24, bottom = 16;
    float a = (SCR_W - 8) / (float)G.w, b = (SCR_H - top - bottom) / (float)G.h;
    cs = a < b ? a : b;
    if (cs > 36) cs = 36;
    ox = (SCR_W - cs * G.w) * 0.5f;
    oy = top + (SCR_H - top - bottom - cs * G.h) * 0.5f;
}

static float sx(float cx) { return ox + (cx + 0.5f) * cs; }
static float sy(float cy) { return oy + (G.h - cy - 0.5f) * cs; }

void play_start(int idx, int hint_mode)
{
    if (idx < 0) idx = 0;
    if (idx >= levels.count) idx = levels.count - 1;
    if (play_idx != idx) {
        if (level_get(&levels, idx, &cur_level)) {
            dlog_error("nivel %d no valido", idx);
            return;
        }
    }
    play_idx = idx;
    logic_load(&G, &S, &cur_level);
    memset(&steps, 0, sizeof(steps));
    shown_from = S;
    if (cur_level.flags & SF_FALL) logic_start(&G, &S, &steps);
    step_i = 0;
    step_t = 0;
    undo_n = 0;
    moves = 0;
    queued_dir = -1;
    play_hint = hint_mode;
    hint_pos = 0;
    win_t = 0;
    end_t = 0;
    face_reset();
    layout();
    dbg_game.level = cur_level.num;
    dbg_game.moves = 0;
    dlog("NIVEL %d (indice %d) %dx%d, %d casillas, %d objetos%s", cur_level.num, idx, G.w, G.h, cur_level.ntiles,
         cur_level.nobj, hint_mode ? " - modo pista" : "");
    set_state(GS_PLAY);
}

static void do_move(int dir)
{
    if (S.dead || S.win) return;
    if (play_hint) {
        if (hint_pos >= cur_level.nhint || cur_level.hint[hint_pos] - '0' != dir) {
            play_sfx(SFX_STOP);
            return;
        }
    }
    GState before = S;
    int r = logic_move(&G, &S, dir, &steps);
    if (r == MV_BLOCKED || r == MV_NONE) {
        play_sfx(SFX_STOP);   /* chocar solo suena; la cara la pone OnChangeEmoji tras cada paso */
        return;
    }
    if (play_hint) hint_pos++;
    if (undo_n == UNDO_MAX) {
        memmove(undo_buf, undo_buf + 1, sizeof(GState) * (UNDO_MAX - 1));
        undo_n--;
    }
    undo_buf[undo_n++] = before;
    shown_from = before;
    step_i = 0;
    step_t = 0;
    moves++;
    dbg_game.moves = moves;
    switch (r) {
    case MV_EAT: play_sfx(SFX_EAT); eat_t = 0.5f; break;
    case MV_PUSH: play_sfx(SFX_MOVE_STONE); break;
    case MV_DIE: play_sfx(SFX_THORN); break;
    default: play_sfx(SFX_WORM); break;
    }
}

void play_input(unsigned in, unsigned held)
{
    int d = -1;
    if (in & IN_UP) d = D_UP;
    else if (in & IN_DOWN) d = D_DOWN;
    else if (in & IN_LEFT) d = D_LEFT;
    else if (in & IN_RIGHT) d = D_RIGHT;
    held_dir = (held & IN_UP) ? D_UP : (held & IN_DOWN) ? D_DOWN : (held & IN_LEFT) ? D_LEFT : (held & IN_RIGHT) ? D_RIGHT : -1;
    if (d >= 0) {
        repeat_t = 0.28f;
        if (animating()) queued_dir = d;
        else do_move(d);
    }
    if (S.win || S.dead) return;
    if (in & IN_START) {
        play_sfx(SFX_CLICK);
        set_state(GS_PAUSE);
        ui_return_state = GS_PLAY;
    }
    if (in & IN_TRIANGLE) {           /* reiniciar (Btn_Replay) */
        play_sfx(SFX_CLICK);
        play_start(play_idx, play_hint);
    }
    if ((in & IN_L) && undo_n > 0 && !play_hint) {   /* deshacer */
        S = undo_buf[--undo_n];
        shown_from = S;
        steps.n = 0;
        step_i = 0;
        play_sfx(SFX_CLICK8);
    }
    if (in & IN_SQUARE) {             /* pista: vuelve a empezar y ensena la solucion paso a paso */
        if (play_hint) return;
        if (!cur_level.nhint) {
            ui_message("Este nivel no tiene pista", 1.5f);
        } else if (sv.hints > 0) {
            sv.hints--;
            game_save();
            play_sfx(SFX_REWARD);
            play_start(play_idx, 1);
        } else
            ui_message("No te quedan pistas: consiguelas en el Diario y en el modo Clasico", 2.5f);
    }
    if (in & IN_R) {                  /* saltar nivel (Icon_SkipLevel) */
        if (sv.skips > 0) {
            sv.skips--;
            level_set_won(play_idx);
            if (play_idx + 1 < levels.count && sv.cur_level <= play_idx) sv.cur_level = play_idx + 1;
            game_save();
            play_sfx(SFX_CLICK);
            dlog("SALTO nivel %d", cur_level.num);
            play_start(play_idx + 1 < levels.count ? play_idx + 1 : play_idx, 0);
        } else
            ui_message("No te quedan saltos de nivel", 1.5f);
    }
}

static void on_win(void)
{
    int first = !level_won(play_idx);
    level_set_won(play_idx);
    sv.total_wins++;
    if (first) {
        sv.tokens++;                  /* en vez de ver un anuncio */
        if (levels_won_count() % 5 == 0) sv.hints++;
    }
    if (play_idx + 1 < levels.count && sv.cur_level <= play_idx) sv.cur_level = play_idx + 1;
    game_save();
    dlog("SUPERADO nivel %d en %d movimientos%s", cur_level.num, moves, first ? " (primera vez: +1 ficha)" : "");
    play_sfx(SFX_WIN);
    set_state(GS_WIN);
    shop_unlock_check();
}

void play_update(float dt)
{
    goal_ang += dt * 1.6f;
    saw_ang += dt * 6.0f;
    blink_t += dt;
    face_update(dt);
    if (eat_t > 0) eat_t -= dt;
    if (gstate != GS_PLAY) return;
    if (animating()) {
        float dur = steps.step[step_i].kind == STEP_FALL ? FALL_T : MOVE_T;
        step_t += dt / dur;
        while (step_t >= 1 && animating()) {
            step_t -= 1;
            int was_fall = steps.step[step_i].kind == STEP_FALL;
            shown_from = steps.step[step_i].st;
            step_i++;
            if (!animating() && was_fall) play_sfx(steps.snake_fell ? SFX_DROP : SFX_STONE_DROP);
        }
        if (!animating()) step_t = 0;
    }
    if (!animating()) {
        if (S.win) {
            /* StartAutoMove: la serpiente entra entera en el portal y LevelComplete */
            win_t += dt;
            if (win_t > ENTER_T * (S.n + 1) + 0.35f) on_win();
            return;
        }
        if (S.dead) {
            end_t += dt;
            if (end_t > 0.9f) {
                play_sfx(SFX_LOSE);
                dlog("PERDIDO nivel %d (%s) en %d movimientos", cur_level.num, S.dead == DEAD_FALL ? "caida" : "trampa", moves);
                set_state(GS_LOSE);
            }
            return;
        }
        if (queued_dir >= 0) {
            int d = queued_dir;
            queued_dir = -1;
            do_move(d);
        } else if (held_dir >= 0) {
            repeat_t -= dt;
            if (repeat_t <= 0) {
                repeat_t = 0.12f;
                do_move(held_dir);
            }
        }
    }
}

/* ---------------------------------------------------------------- dibujo */
static int skin_id(int k) { return k < skin_sheet.nspr ? k : -1; }

static int dir_between(Cell a, Cell b)
{
    int dx = b.x - a.x, dy = b.y - a.y;
    if (dx == 1 && !dy) return D_RIGHT;
    if (dx == -1 && !dy) return D_LEFT;
    if (dy == 1 && !dx) return D_UP;
    if (dy == -1 && !dx) return D_DOWN;
    return -1;
}

/* giros de 90 grados a la izquierda: derecha 0, arriba 1, izquierda 2, abajo 3 */
static int rot_of(int d) { return d == D_RIGHT ? 0 : d == D_UP ? 1 : d == D_LEFT ? 2 : 3; }

static int right_of_dir(int d) { return d == D_RIGHT ? D_DOWN : d == D_DOWN ? D_LEFT : d == D_LEFT ? D_UP : D_RIGHT; }

/* SnakeController.OnChangeEmoji: despues de cada paso la cara depende de lo que hay junto a la cabeza: una manzana
 * delante o a un lado abre la boca (idleToEatImgs); pared, cuerpo o piedra justo delante pone la cara de atascada
 * (idleToStuckImgs); al cambiar se vuelve a la normal con openToIdleImgs / stuckToIdleImgs */
enum { FACE_IDLE, FACE_OPEN, FACE_STUCK };
static int face, face_prev;
static float face_t;

static int face_wanted(const GState *s)
{
    if (s->dead || s->win || !s->n) return FACE_IDLE;
    int hx = s->s[0].x, hy = s->s[0].y, d = s->dir;
    int dirs[3] = {d, right_of_dir(d), dir_opposite(right_of_dir(d))};
    for (int k = 0; k < 3; k++)
        if (logic_apple_at(&G, s, hx + DIR_DX[dirs[k]], hy + DIR_DY[dirs[k]]) >= 0) return FACE_OPEN;
    int fx = hx + DIR_DX[d], fy = hy + DIR_DY[d];
    int solid = fx >= 0 && fy >= 0 && fx < G.w && fy < G.h && G.solid[fy * LV_MAXW + fx];
    int body = 0;
    for (int i = 1; i < s->n; i++)
        if (s->s[i].x == fx && s->s[i].y == fy) body = 1;
    if (solid || body || logic_stone_at(s, fx, fy) >= 0) return FACE_STUCK;
    return FACE_IDLE;
}

static void face_reset(void)
{
    face = face_prev = FACE_IDLE;
    face_t = 1;
}

static void face_update(float dt)
{
    int w = animating() ? face : face_wanted(&S);
    if (w != face) {
        face_prev = face;
        face = w;
        face_t = 0;
    }
    face_t += dt;
}

static int head_frame(const GState *s)
{
    static const int blink_seq[] = {SK_IDLE, SK_BLINK, SK_BLINK + 1, SK_BLINK + 2, SK_BLINK + 2, SK_BLINK + 1,
                                    SK_BLINK + 3, SK_BLINK + 4, SK_IDLE};
    static const int eat_seq[] = {SK_ITE + 4, SK_EAT, SK_EAT + 1, SK_EAT + 1, SK_EAT + 2, SK_EAT + 3, SK_EAT + 3,
                                  SK_EAT + 4, SK_EAT + 4, SK_EAT + 5, SK_EAT + 6, SK_EAT + 7, SK_EAT + 8, SK_EAT + 9,
                                  SK_IDLE};
    if (s->dead) return SK_DIE;
    if (eat_t > 0) {
        int f = (int)((0.5f - eat_t) * 30);
        return eat_seq[f < 0 ? 0 : f > 14 ? 14 : f];
    }
    int f = (int)(face_t * 24);   /* las animaciones de la cara van a 24 fotogramas por segundo */
    if (face == FACE_OPEN) return SK_ITE + (f > 4 ? 4 : f);
    if (face == FACE_STUCK) return SK_ITS + (f > 3 ? 3 : f);
    if (face_prev == FACE_OPEN && f < 5) return SK_ITE + 4 - f;
    if (face_prev == FACE_STUCK && f < 4) return SK_ITS + 3 - f;
    float b = fmodf(blink_t, 3.2f);
    if (b > 2.9f) {
        int k = (int)((b - 2.9f) / 0.3f * 9);
        return blink_seq[k > 8 ? 8 : k];
    }
    return SK_IDLE;
}

/* Dibujo de cada parte como SnakeController.GoStraight / UpdateBodyMoveDirect: la serpiente gira como una pieza
 * rigida. La panza (el lado amarillo) va siempre a la derecha del sentido de la marcha: hacia la derecha, abajo;
 * hacia arriba, a la derecha; hacia la izquierda queda panza arriba (GoStraight: flipX + flipY, la pieza girada 180
 * grados) y la cabeza tambien. Las piezas no se deforman: solo giro y espejo. */
#define right_of right_of_dir
static int belly_of(int heading, int rev) { return rev ? dir_opposite(right_of(heading)) : right_of(heading); }

/* cabeza y cola: el sprite base va hacia la derecha con la panza abajo */
static void draw_oriented(int id, float px, float py, float sc, int heading, int belly, uint32_t col)
{
    int rot = 0, flip = 0;
    switch (heading) {
    case D_RIGHT: flip = belly == D_UP ? FLIP_Y : 0; break;
    case D_LEFT: flip = belly == D_UP ? FLIP_X | FLIP_Y : FLIP_X; break;   /* panza arriba: girada 180 */
    case D_UP: rot = 1; flip = belly == D_LEFT ? FLIP_Y : 0; break;
    default: rot = 1; flip = FLIP_X | (belly == D_LEFT ? FLIP_Y : 0); break;
    }
    gfx_sprite(&skin_sheet, skin_id(id), px, py, sc, rot, flip, col);
}

static void draw_part(int i, const GState *s, float px, float py, uint32_t col, float size)
{
    int n = s->n;
    float sc = cs * size;
    int rev = 0;   /* panza a la derecha de la marcha en todas las partes (ver arriba) */
    if (i == 0) {
        draw_oriented(head_frame(s), px, py, sc, s->dir, belly_of(s->dir, rev), col);
        return;
    }
    int a = dir_between(s->s[i], s->s[i - 1]);                     /* hacia la cabeza (sentido de la marcha) */
    int b = i + 1 < n ? dir_between(s->s[i], s->s[i + 1]) : -1;   /* hacia la cola */
    if (a < 0 && b >= 0) a = dir_opposite(b);                      /* paso por un portal: se dibuja recto */
    if (a < 0) a = D_RIGHT;
    if (i == n - 1) {
        draw_oriented(SK_T2, px, py, sc, a, belly_of(a, rev), col);
        return;
    }
    if (b < 0) b = dir_opposite(a);
    if (b == dir_opposite(a)) {
        int belly = belly_of(a, rev);
        /* girada 180 grados = flipX + flipY, como GoStraight */
        if (a == D_LEFT || a == D_RIGHT) gfx_sprite(&skin_sheet, skin_id(SK_H2), px, py, sc, 0, belly == D_UP ? FLIP_X | FLIP_Y : 0, col);
        else gfx_sprite(&skin_sheet, skin_id(SK_V2), px, py, sc, 0, belly == D_LEFT ? FLIP_X | FLIP_Y : 0, col);
        return;
    }
    /* esquina: la panza va por dentro de la curva (bodyCorner_TopLeft, in_L) o por fuera (TopRight, out_R) */
    int hin = dir_opposite(b), right_turn = a == right_of(hin);
    int inner = right_turn ? !rev : rev;
    int r = (a == D_RIGHT || b == D_RIGHT), d = (a == D_DOWN || b == D_DOWN);
    if (inner) /* in_L une derecha y abajo */
        gfx_sprite(&skin_sheet, skin_id(SK_C2), px, py, sc, 0, (r ? 0 : FLIP_X) | (d ? 0 : FLIP_Y), col);
    else       /* out_R une arriba e izquierda */
        gfx_sprite(&skin_sheet, skin_id(SK_O2), px, py, sc, 0, (r ? FLIP_X : 0) | (d ? FLIP_Y : 0), col);
}

static void draw_snake(const GState *from, const GState *to, float t, int pulled)
{
    for (int i = to->n - 1; i >= 0; i--) {
        float fx = to->s[i].x, fy = to->s[i].y;
        if (i < from->n) {
            float dx = to->s[i].x - from->s[i].x, dy = to->s[i].y - from->s[i].y;
            if (fabsf(dx) + fabsf(dy) <= 1.01f) {
                fx = from->s[i].x + dx * t;
                fy = from->s[i].y + dy * t;
            }
        }
        float size = 1.0f;
        uint32_t col = WHITE;
        if (pulled && i >= to->n - pulled) continue;   /* ya dentro del portal */
        draw_part(i, to, sx(fx), sy(fy), col, size);
    }
}

/* StartAutoMove: la serpiente sigue su propio camino hacia el portal; la cabeza entra la primera y cada parte se
 * encoge al llegar al centro (en el original la tapa una SpriteMask) */
static void draw_snake_enter(const GState *s, float u)
{
    static GState tmp;
    int n = s->n;
    tmp = *s;
    for (int j = 0; j < n; j++) {
        int k = (int)floorf(j - u);
        tmp.s[j] = s->s[k < 0 ? 0 : k];
    }
    for (int i = n - 1; i >= 0; i--) {
        float k = i - u, size = 1;
        float x, y;
        if (k <= -1) continue;                     /* ya dentro */
        if (k < 0) {
            size = 1 + k;
            x = s->s[0].x;
            y = s->s[0].y;
        } else {
            int f = (int)floorf(k), g = f + 1 < n ? f + 1 : f;
            float fr = k - f;
            x = s->s[f].x + (s->s[g].x - s->s[f].x) * fr;
            y = s->s[f].y + (s->s[g].y - s->s[f].y) * fr;
        }
        draw_part(i, &tmp, sx(x), sy(y), WHITE, size);
    }
}

static void draw_tiles(void)
{
    for (int i = 0; i < cur_level.ntiles; i++) {
        const LvTile *t = &cur_level.tiles[i];
        gfx_sprite(&world, t->spr, sx(t->x), sy(t->y), cs, (t->flags & 4) ? 1 : 0, t->flags & 1, WHITE);
    }
}

static void draw_objects(const GState *s, const GState *from, float t)
{
    unsigned t0 = plat_us();
    /* barreras: agujero, haz y tapa */
    for (int b = 0; b < G.nbar; b++) {
        float hx = sx(G.bar[b].x), hy = sy(G.bar[b].y);
        int r = rot_of(G.bar_dir[b]);
        gfx_sprite(&world, WSPR(W_HOLE_BACK), hx, hy, cs, (r + 3) & 3, 0, WHITE);
        int len = s->bar_len[b];
        if (len > 0) {
            int dx = DIR_DX[G.bar_dir[b]], dy = DIR_DY[G.bar_dir[b]];
            float x0 = sx(G.bar[b].x + dx * 0.5f), y0 = sy(G.bar[b].y + dy * 0.5f);
            float x1 = sx(G.bar[b].x + dx * (len + 0.5f)), y1 = sy(G.bar[b].y + dy * (len + 0.5f));
            float th = cs * 0.34f;
            if (dx) gfx_sprite_rect(&world, WSPR(W_BARRIER), x0 < x1 ? x0 : x1, y0 - th / 2, fabsf(x1 - x0), th, WHITE);
            else gfx_sprite_rect(&world, WSPR(W_BARRIER), x0 - th / 2, y0 < y1 ? y0 : y1, th, fabsf(y1 - y0), WHITE);
        }
        gfx_sprite(&world, WSPR(W_HOLE_FRONT), hx, hy, cs, (r + 3) & 3, 0, WHITE);
    }
    for (int k = 0; k < G.nswitch; k++) gfx_sprite(&world, WSPR(W_TRIGGER), sx(G.sw[k].x), sy(G.sw[k].y), cs, 0, 0, WHITE);
    /* portales: parte de atras (la serpiente pasa por delante de ella) */
    for (int g = 0; g < G.ngate; g++)
        gfx_sprite(&world, WSPR(W_PORTAL_BACK), sx(G.gate[g].x), sy(G.gate[g].y), cs, rot_of(G.gate_dir[g]), 0, WHITE);
    /* pinchos y sierras */
    for (int i = 0; i < cur_level.nobj; i++) {
        const LvObj *o = &cur_level.obj[i];
        if (o->type == O_SPIKE) {
            gfx_sprite(&world, WSPR(W_SPIKE2), sx(o->x), sy(o->y), cs, o->a & 3, 0, WHITE);
            gfx_sprite(&world, WSPR(W_SPIKE1), sx(o->x), sy(o->y), cs, o->a & 3, 0, WHITE);
        } else if (o->type == O_SAW)
            gfx_sprite_angle(&world, WSPR(W_SAW), sx(o->x), sy(o->y), cs, saw_ang, WHITE);
        else if (o->type == O_TUT)
            gfx_sprite(&world, o->spr, ox + (o->px / 256.0f + 0.5f) * cs, oy + (G.h - o->py / 256.0f - 0.5f) * cs, cs, 0, 0,
                       RGBA(255, 255, 255, 230));
    }
    /* manzanas (ChangeAppleSkin: la del aspecto elegido) */
    int apple = SK_APPLE < skin_sheet.nspr ? SK_APPLE : -1;
    (void)skin_id;
    for (int a = 0; a < G.napple; a++) {
        if (!s->apple_alive[a]) continue;
        float bob = sinf(gtime * 3 + a) * cs * 0.03f;
        float x = sx(G.apple[a].x), y = sy(G.apple[a].y) + bob;
        if (apple >= 0) gfx_sprite(&skin_sheet, apple, x, y, cs * 0.9f, 0, 0, WHITE);
        else gfx_sprite(&world, WSPR(W_APPLE), x, y, cs * 0.9f, 0, 0, WHITE);
        if (G.apple_double[a]) text_draw(x + cs * 0.15f, y - cs * 0.1f, FONT_S, "x2", WHITE, 0);
    }
    /* piedras */
    for (int i = 0; i < s->nstone; i++) {
        if (!s->st_alive[i]) continue;
        float fx = s->st[i].x, fy = s->st[i].y;
        if (i < from->nstone && from->st_alive[i]) {
            float dx = fx - from->st[i].x, dy = fy - from->st[i].y;
            if (fabsf(dx) + fabsf(dy) <= 1.01f) {
                fx = from->st[i].x + dx * t;
                fy = from->st[i].y + dy * t;
            }
        }
        gfx_sprite(&world, WSPR(W_STONE), sx(fx), sy(fy), cs, 0, 0, WHITE);
    }
    /* meta (PortalRotate) */
    gfx_sprite_angle(&world, WSPR(W_GOAL), sx(G.gx), sy(G.gy), cs, goal_ang, WHITE);
    dbg_sect_ms[2] += (plat_us() - t0) / 1000.0f;
}

void play_draw(void)
{
    unsigned t0 = plat_us();
    gfx_bg(&bg_tex, WHITE);
    unsigned t1 = plat_us();
    draw_tiles();
    unsigned t2 = plat_us();
    dbg_sect_ms[0] = (t1 - t0) / 1000.0f;
    dbg_sect_ms[1] = (t2 - t1) / 1000.0f;
    dbg_sect_ms[2] = 0;
    const GState *to = animating() ? &steps.step[step_i].st : &S;
    float t = animating() ? step_t : 1;
    draw_objects(to, &shown_from, t);
    unsigned t3 = plat_us();
    if (S.win && !animating()) {
        draw_snake_enter(&S, win_t / ENTER_T);
        /* el portal por encima mientras traga la serpiente, girando mas deprisa */
        float grow = 1.0f + 0.25f * sinf(win_t * 6.0f < 3.14159f ? win_t * 6.0f : 3.14159f);
        gfx_sprite_angle(&world, WSPR(W_GOAL), sx(G.gx), sy(G.gy), cs * grow, goal_ang + win_t * win_t * 6, WHITE);
    } else
        draw_snake(&shown_from, to, t, 0);
    /* portales: parte de delante (tapa la serpiente al entrar) */
    for (int g = 0; g < G.ngate; g++)
        gfx_sprite(&world, WSPR(W_PORTAL_FRONT), sx(G.gate[g].x), sy(G.gate[g].y), cs, rot_of(G.gate_dir[g]), 0, WHITE);
    /* flecha de la pista (HintArrow) junto a la cabeza */
    if (play_hint && hint_pos < cur_level.nhint && !animating() && !S.win && !S.dead) {
        int d = cur_level.hint[hint_pos] - '0';
        float bounce = 0.55f + 0.12f * sinf(gtime * 8);
        float x = sx(S.s[0].x + DIR_DX[d] * bounce), y = sy(S.s[0].y + DIR_DY[d] * bounce);
        /* HintArrow apunta a la derecha */
        gfx_sprite(&world, WSPR(UI_HINTARROW), x, y, cs * 1.6f, rot_of(d), 0, WHITE);
    }
    dbg_sect_ms[3] = (plat_us() - t3) / 1000.0f;
    /* barra de arriba: nivel, pistas, saltos */
    unsigned t4 = plat_us();
    gfx_fill(0, 0, SCR_W, 22, RGBA(0, 0, 0, 90));
    char b[64];
    snprintf(b, sizeof(b), "NIVEL %d", play_idx + 1);
    text_draw(SCR_W / 2, 2, FONT_M, b, WHITE, 1);
    gfx_sprite_fit(&world, WSPR(UI_ICON_HINT), 12, 11, 18, 18, WHITE);
    snprintf(b, sizeof(b), "%d", sv.hints);
    text_draw(24, 3, FONT_S, b, WHITE, 0);
    gfx_sprite_fit(&world, WSPR(UI_ICON_SKIPLEVEL), 52, 11, 18, 18, WHITE);
    snprintf(b, sizeof(b), "%d", sv.skips);
    text_draw(64, 3, FONT_S, b, WHITE, 0);
    snprintf(b, sizeof(b), "Movs. %d", moves);
    text_draw(SCR_W - 6, 3, FONT_S, b, WHITE, 2);
    if (gstate == GS_PLAY)
        text_draw(SCR_W / 2, SCR_H - 15, FONT_S,
                  play_hint ? "PISTA: sigue la flecha   " BTN_TRI " Reiniciar   START Pausa"
                            : BTN_SQ " Pista   " BTN_TRI " Reiniciar   L Deshacer   R Saltar   START Pausa",
                  RGBA(255, 255, 255, 200), 1);
    dbg_sect_ms[7] = (plat_us() - t4) / 1000.0f;
}

/* serpiente en posiciones de pantalla dadas (modo clasico): mismas piezas que en el juego */
void draw_snake_screen(const GState *st, const float *xs, const float *ys, float cell)
{
    float save_cs = cs;
    cs = cell;
    for (int i = st->n - 1; i >= 0; i--) draw_part(i, st, xs[i], ys[i], WHITE, 1.0f);
    cs = save_cs;
}

/* serpiente de muestra (tienda): horizontal mirando a la derecha */
void draw_snake_preview(float x, float y, float c, int len)
{
    GState s;
    memset(&s, 0, sizeof(s));
    s.n = len;
    s.dir = D_RIGHT;
    for (int i = 0; i < len; i++) s.s[i] = (Cell){(int8_t)(len - 1 - i), 0};
    float save_cs = cs, save_ox = ox, save_oy = oy;
    int save_h = G.h;
    cs = c;
    G.h = 1;
    ox = x - len * c * 0.5f;
    oy = y - c * 0.5f;
    for (int i = len - 1; i >= 0; i--) draw_part(i, &s, sx(s.s[i].x), sy(0), WHITE, 1.0f);
    cs = save_cs;
    ox = save_ox;
    oy = save_oy;
    G.h = save_h;
}
