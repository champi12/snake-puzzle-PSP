/* logic.c - reglas de Snake Puzzle (traduccion del C# de SnakeController y compania a casillas)
 *
 * Validado con las soluciones oficiales (hint/level_N del APK): tests/host_test.c las juega todas.
 */
#include <string.h>
#include "logic.h"

#define IDX(x, y) ((y) * LV_MAXW + (x))

static int inb(const GLevel *g, int x, int y) { return x >= 0 && y >= 0 && x < g->w && y < g->h; }
static int solid(const GLevel *g, int x, int y) { return inb(g, x, y) && g->solid[IDX(x, y)]; }
static int killc(const GLevel *g, int x, int y) { return inb(g, x, y) && g->kill[IDX(x, y)]; }
static int sawc(const GLevel *g, int x, int y) { return inb(g, x, y) && g->kill[IDX(x, y)] == 2; }
static int is_goal(const GLevel *g, int x, int y) { return x == g->gx && y == g->gy; }

int logic_gate_at(const GLevel *g, int x, int y)
{
    for (int i = 0; i < g->ngate; i++)
        if (g->gate[i].x == x && g->gate[i].y == y) return i;
    return -1;
}

int logic_stone_at(const GState *s, int x, int y)
{
    for (int i = 0; i < s->nstone; i++)
        if (s->st_alive[i] && s->st[i].x == x && s->st[i].y == y) return i;
    return -1;
}

int logic_apple_at(const GLevel *g, const GState *s, int x, int y)
{
    for (int i = 0; i < g->napple; i++)
        if (s->apple_alive[i] && g->apple[i].x == x && g->apple[i].y == y) return i;
    return -1;
}

static int snake_at(const GState *s, int x, int y, int from, int to)
{
    for (int i = from; i < to && i < s->n; i++)
        if (s->s[i].x == x && s->s[i].y == y) return i;
    return -1;
}

/* haz de una barrera: celdas del agujero + 1 .. + bar_len */
int logic_beam_at(const GLevel *g, const GState *s, int x, int y)
{
    for (int i = 0; i < g->nbar; i++) {
        int dx = DIR_DX[g->bar_dir[i]], dy = DIR_DY[g->bar_dir[i]];
        for (int k = 1; k <= s->bar_len[i]; k++)
            if (g->bar[i].x + dx * k == x && g->bar[i].y + dy * k == y) return i;
    }
    return -1;
}

void logic_load(GLevel *g, GState *s, const Level *L)
{
    memset(g, 0, sizeof(*g));
    memset(s, 0, sizeof(*s));
    g->w = L->w;
    g->h = L->h;
    memcpy(g->solid, L->solid, sizeof(g->solid));
    g->gx = L->gx;
    g->gy = L->gy;
    int barmap[LV_MAXOBJ];
    for (int i = 0; i < L->nobj; i++) barmap[i] = -1;
    int gatemap[LV_MAXOBJ];
    for (int i = 0; i < L->nobj; i++) {
        const LvObj *o = &L->obj[i];
        gatemap[i] = -1;
        switch (o->type) {
        case O_APPLE:
            if (g->napple < MAX_APPLE) {
                g->apple[g->napple] = (Cell){o->x, o->y};
                g->apple_double[g->napple] = o->a;
                s->apple_alive[g->napple++] = 1;
            }
            break;
        case O_STONE:
            if (s->nstone < MAX_STONE) {
                /* dos piedras puestas en la misma casilla (nivel 20): Stone.Start las encaja en el mismo sitio y
                 * quedan una encima de otra, moviendose como una sola (la solucion oficial cuenta con eso) */
                int x = o->x, y = o->y;
                if (logic_stone_at(s, x, y) >= 0) break;
                s->st[s->nstone] = (Cell){x, y};
                s->st_alive[s->nstone++] = 1;
            }
            break;
        case O_SPIKE:   /* solo un trigger: las piedras lo atraviesan */
        case O_SAW:     /* ademas un BoxCollider2D solido de 1x1 en la capa Saw: las piedras se quedan encima */
            if (inb(g, o->x, o->y)) g->kill[IDX(o->x, o->y)] = o->type == O_SAW ? 2 : 1;
            break;
        case O_GATE:
            if (g->ngate < MAX_GATE) {
                gatemap[i] = g->ngate;
                g->gate[g->ngate] = (Cell){o->x, o->y};
                g->gate_dir[g->ngate] = o->a & 3;
                g->gate_partner[g->ngate++] = o->b;   /* indice de objeto: se traduce abajo */
            }
            break;
        case O_BARRIER:
            if (g->nbar < MAX_BAR) {
                barmap[i] = g->nbar;
                g->bar[g->nbar] = (Cell){o->x, o->y};
                g->bar_dir[g->nbar] = o->a & 3;
                /* Barrie.ChangeSizeBarrie: el haz llega hasta la pared (rayo contra Wall/Ground) */
                int dx = DIR_DX[o->a & 3], dy = DIR_DY[o->a & 3], k = 1;
                while (k < 40 && inb(g, o->x + dx * k, o->y + dy * k) && !solid(g, o->x + dx * k, o->y + dy * k)) k++;
                g->bar_max[g->nbar] = k - 1;
                s->bar_len[g->nbar] = k - 1;
                g->nbar++;
            }
            break;
        }
    }
    for (int i = 0; i < g->ngate; i++) {
        int p = g->gate_partner[i];
        g->gate_partner[i] = (p >= 0 && p < L->nobj && gatemap[p] >= 0) ? gatemap[p] : i;
    }
    for (int i = 0; i < L->nobj; i++) {
        const LvObj *o = &L->obj[i];
        if (o->type == O_SWITCH && g->nswitch < MAX_BAR) {
            g->sw[g->nswitch] = (Cell){o->x, o->y};
            g->sw_bar[g->nswitch++] = (o->b >= 0 && o->b < L->nobj) ? barmap[(int)o->b] : -1;
        }
    }
    /* SnakeController.Initialize(posicion, Vector2.right, 3) con las opciones del prefab */
    int dir = D_RIGHT, ox = -1, oy = 0, down = 0;
    if (L->flags & SF_DOWN) { dir = D_UP; down = 1; }
    else if (L->flags & SF_UP) { dir = D_DOWN; ox = 0; oy = 1; }
    else if (L->flags & SF_LEFT) { dir = D_LEFT; ox = 1; oy = 0; }
    s->n = 1;
    s->tele = 0xffff;
    s->s[0] = (Cell){L->sx, L->sy};
    s->dir = dir;
    s->last_in = dir;
    memset(s->rev, dir == D_LEFT, sizeof(s->rev));   /* Initialize: isReversed = direction.x < 0 */
    for (int i = 1; i < 3; i++) {
        if (i == 2) {
            if (L->flags & SF_SECOND_RIGHT) { ox = -1; oy = 0; }
            else if (L->flags & SF_SECOND_DOWN) { ox = 0; oy = 1; }
            else if (L->flags & SF_SECOND_UP) { ox = 0; oy = -1; }
        }
        int ex = down ? 0 : ox, ey = down ? -1 : oy;
        s->s[i] = (Cell){s->s[i - 1].x + ex, s->s[i - 1].y + ey};
        s->n++;
    }
}

/* ---------------------------------------------------------------- fisica */
/* CheckForSnakeFall: circulo de radio 0,2 en la casilla de debajo de cada parte contra groundLayer (solo la capa
 * Ground: suelo, manzanas, piedras y meta; el BoxCollider2D de los portales esta desactivado en todos los niveles) y
 * que no tenga la etiqueta Kill. Pinchos y sierras estan en la
 * capa Default: no sujetan, la serpiente cae dentro de su casilla y su trigger la mata (Trap -> isDeath). */
static int supports_snake(const GLevel *g, const GState *s, int x, int y)
{
    return solid(g, x, y) || logic_apple_at(g, s, x, y) >= 0 || logic_stone_at(s, x, y) >= 0 || is_goal(g, x, y);
}

static int snake_supported(const GLevel *g, const GState *s)
{
    for (int i = 0; i < s->n; i++) {
        if (supports_snake(g, s, s->s[i].x, s->s[i].y - 1)) return 1;
    }
    return 0;
}

/* Caida de una piedra: es un Rigidbody2D dinamico con gravedad; se para en lo que tenga collider solido de una capa
 * que choque con Ground (matriz de Physics2D): suelo, manzanas, otras piedras, la serpiente (SnakeBody), sierras
 * (capa Saw) y haces de barrera (Barrie). La meta, los portales y los pinchos son triggers: los atraviesa. */
static int stone_rests_on(const GLevel *g, const GState *s, int x, int y)
{
    return solid(g, x, y) || snake_at(s, x, y, 0, s->n) >= 0 || logic_stone_at(s, x, y) >= 0 ||
           logic_apple_at(g, s, x, y) >= 0 || sawc(g, x, y) || logic_beam_at(g, s, x, y) >= 0;
}

/* Stone.TryPush: OverlapCircle(destino, cs/4, groundLayer|snakeBodyLayer|barrieLayer). Las consultas de Physics2D
 * ven triggers (queriesHitTriggers), asi que tambien bloquea la meta; no los portales (collider desactivado) ni los
 * pinchos ni las sierras (capas Default y Saw). Otra piedra esta en Ground: bloquea (LayerMask.GetMask("Stone") vale 0 porque esa capa no
 * existe, asi que la rama de empujar dos piedras del original nunca se usa). */
static int push_blocked(const GLevel *g, const GState *s, int x, int y)
{
    return solid(g, x, y) || snake_at(s, x, y, 0, s->n) >= 0 || logic_stone_at(s, x, y) >= 0 ||
           logic_apple_at(g, s, x, y) >= 0 || is_goal(g, x, y) || logic_beam_at(g, s, x, y) >= 0;
}

/* Switch + Barrie: con algo encima del interruptor el haz se recoge; si no, vuelve a salir hasta la pared o
 * hasta lo primero que encuentre (CheckBarrieCanLong no atraviesa la serpiente) */
static int update_barriers(const GLevel *g, GState *s)
{
    int changed = 0;
    for (int b = 0; b < g->nbar; b++) {
        int pressed = 0;
        for (int k = 0; k < g->nswitch; k++) {
            if (g->sw_bar[k] != b) continue;
            int x = g->sw[k].x, y = g->sw[k].y;
            if (snake_at(s, x, y, 0, s->n) >= 0 || logic_stone_at(s, x, y) >= 0) pressed = 1;
        }
        int want = 0;
        if (!pressed) {
            int dx = DIR_DX[g->bar_dir[b]], dy = DIR_DY[g->bar_dir[b]];
            while (want < g->bar_max[b]) {
                int x = g->bar[b].x + dx * (want + 1), y = g->bar[b].y + dy * (want + 1);
                if (snake_at(s, x, y, 0, s->n) >= 0 || logic_stone_at(s, x, y) >= 0) break;
                want++;
            }
        }
        if (s->bar_len[b] != want) {
            s->bar_len[b] = want;
            changed = 1;
        }
    }
    return changed;
}

static void push_step(GSteps *o, int kind, const GState *s)
{
    if (!o) return;
    if (o->n < MAX_STEPS) {
        o->step[o->n].kind = kind;
        o->step[o->n].st = *s;
        o->n++;
    } else {
        o->step[MAX_STEPS - 1].kind = kind;   /* caida muy larga: se salta fotos intermedias */
        o->step[MAX_STEPS - 1].st = *s;
    }
}

/* GameManager.TriggerPhysicsTurn: piedras y serpiente caen de casilla en casilla hasta que nada se mueve */
static void physics(const GLevel *g, GState *s, GSteps *o)
{
    for (int it = 0; it < 200; it++) {
        int moved = update_barriers(g, s);
        /* piedras de abajo arriba: una pila cae entera */
        for (int pass = 0; pass < s->nstone; pass++) {
            int best = -1;
            for (int i = 0; i < s->nstone; i++) {
                if (!s->st_alive[i] || (s->st_alive[i] & 2)) continue;
                if (best < 0 || s->st[i].y < s->st[best].y) best = i;
            }
            if (best < 0) break;
            s->st_alive[best] |= 2;
            int x = s->st[best].x, y = s->st[best].y - 1;
            if (!stone_rests_on(g, s, x, y)) {
                s->st[best].y--;
                moved = 1;
                if (s->st[best].y < -3) s->st_alive[best] = 0;   /* se cayo del mapa */
                if (o) o->stone_landed = 1;
            }
        }
        for (int i = 0; i < s->nstone; i++) s->st_alive[i] &= 1;
        /* CheckForSnakeFall: nada mientras la serpiente cruza un portal (indexMainLoop < bodyParts.Count) */
        if (s->tele >= s->n && !snake_supported(g, s)) {
            for (int i = 0; i < s->n; i++) s->s[i].y--;
            moved = 1;
            if (o) o->snake_fell = 1;
            int all_out = 1;
            for (int i = 0; i < s->n; i++)
                if (s->s[i].y >= -3) all_out = 0;
            if (all_out) {
                s->dead = DEAD_FALL;   /* FallTimeoutCoroutine: GameOver("fall_off") */
                push_step(o, STEP_FALL, s);
                return;
            }
        }
        for (int i = 0; i < s->n; i++)
            if (killc(g, s->s[i].x, s->s[i].y)) {
                s->dead = DEAD_TRAP;   /* Trap.OnTriggerEnter2D + OnCheckBodyStatus: "trap_die" */
                push_step(o, STEP_FALL, s);
                return;
            }
        if (!moved) return;
        push_step(o, STEP_FALL, s);
    }
}

void logic_start(const GLevel *g, GState *s, GSteps *o)
{
    if (o) memset(o, 0, sizeof(*o));
    update_barriers(g, s);
    push_step(o, STEP_MOVE, s);
    physics(g, s, o);
}

/* Stone.TryPush: solo izquierda, derecha o arriba (hacia abajo nunca), una casilla, y una sola piedra. Al acabar
 * (MoveSnakeOnly) CheckStoneFallTeleport: si ha entrado en un portal que se cruza hacia abajo, sale por el otro y se
 * empuja hacia fuera */
static int try_push(const GLevel *g, GState *s, int si, int dir)
{
    if (dir == D_DOWN) return 0;
    int x = s->st[si].x + DIR_DX[dir], y = s->st[si].y + DIR_DY[dir];
    if (push_blocked(g, s, x, y)) return 0;
    s->st[si] = (Cell){x, y};
    int gi = logic_gate_at(g, x, y);
    if (gi >= 0 && g->gate_dir[gi] == D_DOWN) {
        int b = g->gate_partner[gi];
        s->st[si] = g->gate[b];
        int out = dir_opposite(g->gate_dir[b]);
        if (out != D_DOWN) {
            int ex = g->gate[b].x + DIR_DX[out], ey = g->gate[b].y + DIR_DY[out];
            if (!push_blocked(g, s, ex, ey)) s->st[si] = (Cell){ex, ey};
        }
    }
    return 1;
}

/* HandleMovement con una piedra delante: TryPush y, si no puede, SnakeController.TryTeleportStone (la piedra esta
 * dentro de un portal y se empuja en su direccion: aparece en el otro y se empuja hacia fuera; si ahi no cabe se
 * queda en el portal de salida, como en el original, que la coloca antes de probar el empuje) */
static int push_stone(const GLevel *g, GState *s, int si, int dir)
{
    if (try_push(g, s, si, dir)) return 1;
    int gi = logic_gate_at(g, s->st[si].x, s->st[si].y);
    if (gi < 0 || dir != g->gate_dir[gi]) return 0;
    int b = g->gate_partner[gi];
    s->st[si] = g->gate[b];
    try_push(g, s, si, dir_opposite(g->gate_dir[b]));
    return 1;
}

/* la cabeza esta en la casilla de un portal (SnakeController.currentTeleportGate) */
int logic_in_gate(const GLevel *g, const GState *s)
{
    int gi = logic_gate_at(g, s->s[0].x, s->s[0].y);
    return gi >= 0 && g->gate_partner[gi] != gi;
}

/* SnakeController.Move: isReversed = izquierda, o abajo si el paso anterior fue a la izquierda; la parte nueva de la
 * cabeza lo guarda y las demas conservan el suyo (BodyPartInfo.previousIsReversed) */
static void shift(GState *s, int x, int y, int dir)
{
    if (s->tele < s->n) s->tele++;   /* Move -> HideMainBody */
    memmove(&s->s[1], &s->s[0], sizeof(Cell) * (s->n - 1));
    memmove(&s->rev[1], &s->rev[0], s->n - 1);
    s->s[0] = (Cell){x, y};
    s->rev[0] = dir == D_LEFT || (dir == D_DOWN && s->last_in == D_LEFT);
    s->last_in = dir;
}

/* SnakeController.HandleMovement */
int logic_move(const GLevel *g, GState *s, int dir, GSteps *o)
{
    if (o) memset(o, 0, sizeof(*o));
    if (s->dead || s->win) return MV_NONE;
    int hx = s->s[0].x, hy = s->s[0].y;
    /* HandleMovement: con la cabeza en un portal y pulsando su gateDirection, TeleportGate la saca por el otro.
     * Con otra direccion se mueve normal (el BoxCollider2D del portal esta desactivado: se entra y sale por
     * cualquier lado) */
    if (logic_in_gate(g, s) && dir == g->gate_dir[logic_gate_at(g, hx, hy)]) {
        int b = g->gate_partner[logic_gate_at(g, hx, hy)];
        s->tele = 0;   /* TeleportGate -> CreateClone */
        shift(s, g->gate[b].x, g->gate[b].y, dir_opposite(g->gate_dir[b]));
        s->dir = dir_opposite(g->gate_dir[b]);
        push_step(o, STEP_MOVE, s);
        physics(g, s, o);
        return MV_TELE;
    }
    int tx = hx + DIR_DX[dir], ty = hy + DIR_DY[dir];
    /* el rayo choca con el cuerpo (capa SnakeBody) o con una pared (etiqueta Wall) */
    if (snake_at(s, tx, ty, 0, s->n) >= 0 || solid(g, tx, ty) || logic_beam_at(g, s, tx, ty) >= 0) return MV_BLOCKED;
    if (is_goal(g, tx, ty)) {   /* etiqueta Goal/Mask: StartAutoMove hacia el portal y LevelComplete */
        shift(s, tx, ty, dir);
        s->dir = dir;
        s->win = 1;
        push_step(o, STEP_MOVE, s);
        return MV_WIN;
    }
    int ai = logic_apple_at(g, s, tx, ty);
    if (ai >= 0) {   /* etiqueta Apple: EatApple + Grow(isDouble) */
        s->apple_alive[ai] = 0;
        if (s->n < MAX_SNAKE) s->n++;
        shift(s, tx, ty, dir);
        s->dir = dir;
        if (g->apple_double[ai] && s->n < MAX_SNAKE && s->n >= 2) {
            /* GetDirectionTailGrow: el segmento de mas sale detras de la cola */
            Cell tl = s->s[s->n - 1], pv = s->s[s->n - 2];
            int cand[5][2] = {{tl.x - pv.x, tl.y - pv.y}, {0, -1}, {-1, 0}, {1, 0}, {0, 1}};
            for (int k = 0; k < 5; k++) {
                int cx = tl.x + cand[k][0], cy = tl.y + cand[k][1];
                if (solid(g, cx, cy) || snake_at(s, cx, cy, 0, s->n) >= 0 || logic_stone_at(s, cx, cy) >= 0 ||
                    logic_apple_at(g, s, cx, cy) >= 0)
                    continue;
                s->rev[s->n] = s->rev[s->n - 1];
                s->s[s->n++] = (Cell){cx, cy};
                break;
            }
        }
        push_step(o, STEP_MOVE, s);
        physics(g, s, o);
        return MV_EAT;
    }
    int res = MV_MOVED;
    int si = logic_stone_at(s, tx, ty);
    if (si >= 0) {   /* etiqueta Stone: TryPush o TryTeleportStone; si no, "Detect Stone blocked!" */
        if (!push_stone(g, s, si, dir)) return MV_BLOCKED;
        res = MV_PUSH;
    }
    int gi = logic_gate_at(g, tx, ty);
    shift(s, tx, ty, dir);
    s->dir = dir;
    if (killc(g, tx, ty)) {   /* etiqueta Kill/Saw: la cabeza entra y muere */
        s->dead = DEAD_TRAP;
        push_step(o, STEP_MOVE, s);
        return MV_DIE;
    }
    push_step(o, STEP_MOVE, s);
    physics(g, s, o);
    if (s->dead) return MV_DIE;
    return gi >= 0 ? MV_TELE : res;
}
