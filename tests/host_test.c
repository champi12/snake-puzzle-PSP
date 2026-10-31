/* host_test.c - juega la solucion oficial de cada nivel (hint/level_N del APK) con la logica del port.
 *   make -C tests && tests/host_test data/levels.bin [-v nivel]
 * Sirve para comprobar que las reglas traducidas del original se comportan igual que el juego. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/level.h"
#include "../src/logic.h"

static Level L;
static GLevel G;
static GState S;
static GSteps ST;

static void draw(void)
{
    for (int y = G.h + 2; y >= -2; y--) {
        printf("%4d ", y);
        for (int x = 0; x < G.w; x++) {
            char c = '.';
            if (x >= 0 && y >= 0 && G.solid[y * LV_MAXW + x]) c = '#';
            if (y >= 0 && G.kill[y * LV_MAXW + x]) c = '^';
            if (logic_apple_at(&G, &S, x, y) >= 0) c = 'a';
            if (logic_stone_at(&S, x, y) >= 0) c = 'O';
            if (logic_gate_at(&G, x, y) >= 0) c = 'T';
            if (logic_beam_at(&G, &S, x, y) >= 0) c = '=';
            if (x == G.gx && y == G.gy) c = 'G';
            for (int i = S.n - 1; i >= 0; i--)
                if (S.s[i].x == x && S.s[i].y == y) c = i ? 's' : 'H';
            putchar(c);
        }
        putchar('\n');
    }
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "uso: host_test data/levels.bin [-v nivel]\n");
        return 1;
    }
    int verbose = argc > 3 ? atoi(argv[3]) : 0;
    LevelPack pk;
    if (levels_open(&pk, argv[1])) {
        fprintf(stderr, "no se pudo abrir %s\n", argv[1]);
        return 1;
    }
    int ok = 0, total = 0;
    char fails[4096] = "";
    for (int i = 0; i < pk.count; i++) {
        if (level_get(&pk, i, &L)) continue;
        if (verbose && L.num != verbose) continue;
        logic_load(&G, &S, &L);
        if (L.flags & SF_FALL) logic_start(&G, &S, &ST);
        if (verbose) draw();
        int res = 0, k;
        for (k = 0; k < L.nhint; k++) {
            res = logic_move(&G, &S, L.hint[k] - '0', &ST);
            if (verbose) {
                printf("%d %c -> %d (pasos %d)\n", k, "UDLR"[L.hint[k] - '0'], res, ST.n);
                draw();
            }
            if (S.dead || S.win) break;
        }
        total++;
        /* ganar antes de la ultima flecha tambien vale: alguna pista grabada trae pulsaciones de mas (nivel 87:
         * una flecha pulsada mientras la serpiente caia) */
        if (S.win) ok++;
        else {
            char b[64];
            snprintf(b, sizeof(b), " %d(%s %d/%d)", L.num, S.dead ? "muere" : S.win ? "antes" : "no", k, L.nhint);
            strncat(fails, b, sizeof(fails) - strlen(fails) - 1);
        }
    }
    printf("soluciones oficiales: %d de %d niveles correctos\n", ok, total);
    if (ok != total) printf("distintos:%s\n", fails);
    levels_close(&pk);
    return 0;
}
