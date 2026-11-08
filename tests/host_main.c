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
int plat_secs_of_day(void) { return 15 * 3600 + 25 * 60 + 40 + fake_ms / 1000; }
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
        for (int k = 1; k < 60; k++)
            if (skin_exists(k % (nskins ? nskins : 1))) load_skin(k % (nskins ? nskins : 1));
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
    int fails = 0;
    /* partida de la version 1 (port de la 2.3.4): se convierte sin perder nada */
    {
        struct {
            uint32_t magic, version;
            uint16_t cur_level;
            uint8_t won[MAX_LEVELS / 8 + 1];
            uint16_t hints, skips;
            uint16_t tokens;
            uint8_t skin, bg;
            uint32_t skins_owned, bgs_owned;
            uint8_t skin_paid[32], bg_paid[32];
            uint8_t sound, music;
            uint8_t daily_day;
            uint32_t daily_last;
            uint16_t classic_best;
            uint8_t progress_done;
            uint8_t event_done;
            uint32_t total_wins;
            uint32_t play_seconds;
        } v1;
        memset(&v1, 0, sizeof(v1));
        v1.magic = 0x4b4e5350u;
        v1.version = 1;
        v1.cur_level = levels.count - 1;
        for (int i = 0; i < levels.count - 1; i++) v1.won[i >> 3] |= 1 << (i & 7);
        v1.hints = 7; v1.skips = 2; v1.tokens = 41; v1.skin = 3; v1.bg = 2;
        v1.skins_owned = 0x9u | 1u << 27; v1.bgs_owned = 0x5;
        v1.skin_paid[5] = 2; v1.bg_paid[4] = 1;
        v1.sound = 1; v1.music = 0; v1.daily_day = 4; v1.daily_last = 20260101; v1.classic_best = 33;
        v1.progress_done = 5; v1.total_wins = 400; v1.play_seconds = 7777;
        FILE *f = fopen("save.dat", "wb");
        fwrite(&v1, 1, sizeof(v1), f);
        fwrite("APPLEWORM-EXTRA", 1, 15, f);   /* las de la epoca de Apple Worm eran mas largas */
        fclose(f);
        extern int save_load(void);
        save_load();
        int ok = sv.version == 2 && sv.cur_level == levels.count - 1 && sv.hints == 7 && sv.skips == 2 && sv.tokens == 41 &&
                 sv.skin == 3 && sv.bg == 2 && sv.skins_owned == (0x9u | 1u << 27) && sv.bgs_owned == 0x5 &&
                 sv.skin_paid[5] == 2 && sv.bg_paid[4] == 1 && !sv.music && sv.daily_day == 4 && sv.classic_best == 33 &&
                 sv.progress_done == 5 && sv.total_wins == 400 && sv.play_seconds == 7777 &&
                 levels_won_count() == levels.count - 1 && sv.loop_real[0] == 0xffff;
        printf("partida v1 -> v2: %s\n", ok ? "OK" : "MAL");
        fails += !ok;
        save_load();   /* ya guardada como v2: se vuelve a leer igual */
        ok = sv.version == 2 && sv.tokens == 41 && sv.skins_owned == (0x9u | 1u << 27);
        printf("partida v2 releida: %s\n", ok ? "OK" : "MAL");
        fails += !ok;
    }
    /* bucle de niveles (UserLevelInfo): superar el ultimo nivel lleva al nivel 267 con un nivel real al azar */
    {
        int last = levels.count - 1;
        play_solution(last, -1);
        frames(200, 0);
        while (gstate == GS_REWARD) frames(2, IN_CIRCLE);   /* premio de progreso antes de la ventana de superado */
        shot("ultimo_superado");
        int ok = gstate == GS_WIN && sv.cur_level == levels.count;
        printf("  (tras el ultimo: pantalla %s, posicion en curso %d, jugado %d)\n", dbg_state_name(gstate), sv.cur_level + 1,
               play_pos + 1);
        frames(2, IN_CROSS);   /* SIGUIENTE */
        frames(40, 0);
        shot("bucle_nivel267");
        ok = ok && gstate == GS_PLAY && play_pos == levels.count && play_idx >= 0 && play_idx < levels.count;
        int num = cur_level.num;
        for (int i = 0; i < meta.nignore; i++) ok = ok && meta.ignore[i] != num;
        printf("bucle: tras el ultimo nivel -> posicion %d, nivel real %d: %s\n", play_pos + 1, num, ok ? "OK" : "MAL");
        fails += !ok;
        /* el mismo nivel del bucle al reintentar */
        int was = play_idx;
        play_start(play_pos, 0);
        ok = play_idx == was;
        printf("bucle: reintentar da el mismo nivel: %s\n", ok ? "OK" : "MAL");
        fails += !ok;
        /* 400 niveles del bucle: nunca uno ignorado; sin repetir hasta agotar los posibles */
        static int seen[MAX_LEVELS];
        int pool = 0;
        for (int i = 0; i < levels.count; i++) {
            int ig = 0, n = levels_num(&levels, i);
            for (int k = 0; k < meta.nignore; k++) ig |= meta.ignore[k] == n;
            pool += !ig;
        }
        int bad = 0, firstrep = -1;
        for (int k = 0; k < 400; k++) {
            int pos = sv.cur_level;
            int idx = pos_level(pos);
            int n = levels_num(&levels, idx);
            for (int i = 0; i < meta.nignore; i++) bad += meta.ignore[i] == n;
            if (seen[idx]++ && firstrep < 0) firstrep = k;
            pos_advance(pos);
        }
        ok = !bad && firstrep >= pool - LOOP_AHEAD - 2;
        printf("bucle: 400 niveles, %d ignorados, posibles %d, primera repeticion en el %d: %s\n", bad, pool, firstrep,
               ok ? "OK" : "MAL");
        fails += !ok;
        sv.cur_level = levels.count - 1;
    }
    /* selector con manzanas: superados, saltado, actual y bloqueados */
    {
        memset(sv.won, 0, sizeof(sv.won));
        memset(sv.skipped, 0, sizeof(sv.skipped));
        for (int i = 0; i < 9; i++) level_set_won(i);
        level_set_skipped(4, 1);
        sv.cur_level = 9;
        set_state(GS_SELECT);
        frames(2, 0);
        shot("niveles_manzanas");
        frames(2, IN_R);
        shot("niveles_pagina2");
    }
    /* tienda de la 14.2.9: aspectos nuevos y paquetes de temporada */
    {
        sv.tokens = 200;
        ui_open_shop();
        frames(2, 0);
        shot("tienda_1429");
        frames(2, IN_R);
        shot("tienda_fondos_1429");
        frames(2, IN_R);
        shot("tienda_packs_1429");
        int bought = 0;
        for (int k = 0; k < 5; k++) {   /* tarjetas: especial, las 4 de temporada y la oferta; se compra la segunda */
            for (int j = 0; j < 20; j++) frames(1, IN_LEFT);
            frames(1, IN_RIGHT);
            frames(2, IN_CROSS);
            while (gstate == GS_REWARD) { frames(2, IN_CIRCLE); bought++; }
            set_state(GS_SHOP);
        }
        shot("tienda_packs_comprados");
        int ok = skin_available(30) + skin_available(31) + skin_available(32) + skin_available(33) == 4 ? 1 : 0;
        printf("tienda: %d premios de paquetes, aspectos 30-33: %s\n", bought, ok ? "OK" : "MAL");
        fails += !ok;
        set_state(GS_SHOP);
        frames(2, IN_L);
        frames(2, IN_L);
        shot("tienda_aspectos_comprados");
    }
    /* modo retro: la cola no debe separarse del cuerpo (recta y curva, varios momentos de cada paso) */
    {
        classic_start();
        frames(2, IN_RIGHT);
        frames(30, 0);           /* crece algo de recorrido */
        for (int k = 0; k < 4; k++) {
            frames(4, 0);
            char b[32];
            snprintf(b, sizeof(b), "retro_recta%d", k);
            shot(b);
        }
        frames(1, IN_UP);
        for (int k = 0; k < 8; k++) {
            frames(4, 0);
            char b[32];
            snprintf(b, sizeof(b), "retro_curva%d", k);
            shot(b);
        }
    }
    printf("pruebas 14.2.9: %s\n", fails ? "FALLOS" : "todo bien");
    return fails ? 1 : 0;
}
