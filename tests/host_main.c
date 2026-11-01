/* host_main.c - el juego entero en el PC con un guion de botones y capturas de pantalla
 *   make -C tests shots DATA=../data     (deja las capturas en tests/out/)
 * Recorre: inicio, niveles, un nivel jugado con su solucion, portales, pausa, superado, tienda, diario, ajustes y
 * modo clasico. Sirve para ver que el dibujo, los menus y el progreso funcionan antes de probar en la PSP. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "../src/game.h"
#include "../src/audio.h"
#include "../src/devlog.h"
#include "../src/lang.h"

/* lo que en la PSP pone devlog.c */
MemInfo dbg_mem;
DbgGame dbg_game;
int dbg_loads_frame;
float dbg_sect_ms[8];
volatile const char *dbg_phase_main, *dbg_phase_music, *dbg_phase_mixer;
volatile int dbg_tracing;
void dbg_trace(const char *s) { (void)s; }
#include <malloc.h>
void mem_query(MemInfo *m) { struct mallinfo2 mi = mallinfo2(); m->heap_used = mi.uordblks; }

static unsigned fake_ms;
unsigned plat_ms(void) { return fake_ms; }
unsigned plat_us(void) { return fake_ms * 1000; }
void plat_exit(void) {}
const char *plat_write_dir(void) { return "."; }
void plat_date(int *y, int *m, int *d)
{
    const char *e = getenv("HOST_DATE");   /* aaaammdd para probar los eventos */
    int v = e ? atoi(e) : 20260615;
    *y = v / 10000;
    *m = v / 100 % 100;
    *d = v % 100;
}
void fatal_error(const char *msg) { fprintf(stderr, "ERROR: %s\n", msg); exit(1); }

int audio_init(const char *dir) { (void)dir; return 0; }
void audio_shutdown(void) {}
void audio_play(int id, float vol) { (void)id; (void)vol; }
void audio_music(int w) { (void)w; }
void audio_set_enabled(int a, int b) { (void)a; (void)b; }
const char *audio_status(void) { return "Audio: PC"; }

extern void host_shot(const char *path);
static int shot_n;

static void frames(int n, unsigned in_first)
{
    for (int i = 0; i < n; i++) {
        game_input(i == 0 ? in_first : 0, 0);
        game_update(1.0f / 60);
        fake_ms += 16;
    }
}

static void shot(const char *name)
{
    gfx_begin(RGBA(40, 60, 90, 255));
    game_draw();
    char p[128];
    snprintf(p, sizeof(p), "out/%02d_%s.ppm", shot_n++, name);
    host_shot(p);
    printf("captura %s (pantalla %s, %d dibujos)\n", p, dbg_state_name(gstate), gstat.draws);
}

static void play_solution(int idx, int upto)
{
    play_start(idx, 0);
    frames(30, 0);
    for (int k = 0; k < cur_level.nhint && (upto < 0 || k < upto); k++) {
        int d = cur_level.hint[k] - '0';
        unsigned in = d == D_UP ? IN_UP : d == D_DOWN ? IN_DOWN : d == D_LEFT ? IN_LEFT : IN_RIGHT;
        frames(40, in);
    }
}

int main(int argc, char **argv)
{
    if (argc > 1 && chdir(argv[1])) { perror("chdir"); return 1; }   /* carpeta con data/ y out/ */
    remove("save.dat");
    lang_init(getenv("HOST_LANG") ? atoi(getenv("HOST_LANG")) : 3);
    gfx_init();
    game_init();
    shot("inicio");
    frames(2, IN_DOWN);
    frames(2, IN_CROSS);
    shot("niveles");
    frames(2, IN_CIRCLE);
    play_start(0, 0);
    frames(40, 0);
    shot("nivel1");
    play_solution(0, cur_level.nhint - 1);
    {
        int d = cur_level.hint[cur_level.nhint - 1] - '0';
        frames(10, d == D_UP ? IN_UP : d == D_DOWN ? IN_DOWN : d == D_LEFT ? IN_LEFT : IN_RIGHT);
    }
    shot("portal_entrando");
    frames(7, 0);
    shot("portal_entrando2");
    frames(120, 0);
    shot("nivel1_superado");
    frames(2, IN_CROSS);
    frames(40, 0);
    shot("nivel2");
    /* uno con piedras, pinchos y uno con portales e interruptor */
    for (int i = 0; i < levels.count; i++) {
        Level L;
        level_get(&levels, i, &L);
        if (L.num == 30 || L.num == 26 || L.num == 20 || L.num == 102 || L.num == 230 || L.num == 211 || L.num == 241 || L.num == 151 || L.num == 3 || L.num == 1) {
            play_start(i, 0);
            frames(40, 0);
            char b[32];
            snprintf(b, sizeof(b), "nivel%d", L.num);
            shot(b);
            if (L.num == 3) {   /* comer y girar: esquinas y cola en todas las direcciones */
                for (int k = 1; k <= cur_level.nhint; k++) {
                    play_solution(i, k);
                    frames(30, 0);
                    char b2[32];
                    snprintf(b2, sizeof(b2), "nivel3_paso%02d", k);
                    shot(b2);
                }
            }
            if (L.num == 211) {
                play_solution(i, 3);
                frames(5, 0);
                shot("nivel211_portal");
            }
        }
    }
    play_start(5, 0);
    frames(10, 0);
    frames(2, IN_SQUARE);
    frames(30, 0);
    shot("pista");
    frames(2, IN_START);
    shot("pausa");
    frames(2, IN_CROSS);
    /* morir en un nivel con pinchos (nivel 26: bajar hacia el pincho) */
    for (int i = 0; i < levels.count; i++) {
        Level L;
        level_get(&levels, i, &L);
        if (L.num == 26) {
            play_start(i, 0);
            frames(30, 0);
            frames(30, IN_RIGHT);
            frames(120, 0);
            shot("perdido");
        }
    }
    set_state(GS_HOME);
    sv.tokens = 20;
    frames(2, 0);
    shot("inicio2");
    set_state(GS_SHOP);
    frames(2, IN_RIGHT);
    frames(2, IN_RIGHT);
    shot("tienda_aspectos");
    frames(2, IN_CROSS);
    frames(2, IN_CROSS);
    frames(2, IN_CROSS);
    frames(2, 0);
    shot("tienda_pago");
    if (gstate == GS_REWARD) {
        frames(2, IN_CROSS);
        shot("tienda_equipado");
    }
    set_state(GS_SHOP);
    frames(2, IN_R);
    shot("tienda_fondos");
    frames(2, IN_R);
    shot("tienda_packs");
    frames(2, IN_DOWN);
    frames(2, IN_CROSS);
    frames(2, 0);
    shot("tienda_pack_comprado");
    set_state(GS_DAILY);
    frames(2, 0);
    shot("diario");
    frames(2, IN_CROSS);
    shot("diario_cobrado");
    set_state(GS_SETTINGS);
    shot("ajustes");
    /* fugas: equipar 60 aspectos seguidos no debe hacer crecer la memoria */
    {
        load_skin(1);   /* se mide desde un estado con aspecto y fondo ya cargados */
        load_skin(0);
        struct mallinfo2 a = mallinfo2();
        for (int k = 1; k < 60; k++) load_skin(k % (nskins ? nskins : 1));
        load_skin(0);
        struct mallinfo2 b = mallinfo2();
        printf("memoria tras 60 cambios de aspecto: %+ld KB\n", ((long)b.uordblks - (long)a.uordblks) / 1024);
    }
    classic_start();
    frames(2, IN_UP);
    frames(40, 0);
    frames(2, IN_LEFT);
    frames(30, 0);
    frames(2, IN_DOWN);
    frames(20, 0);
    shot("clasico");
    frames(4, 0);
    shot("clasico_b");
    frames(2000, 0);
    shot("clasico_fin");
    printf("pantalla final %s, nivel en curso %d, %d superados, %d fichas, %d pistas\n", dbg_state_name(gstate),
           sv.cur_level + 1, levels_won_count(), sv.tokens, sv.hints);
    return 0;
}
