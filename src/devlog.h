/* devlog.h - registro de desarrollo en la Memory Stick (devlog.txt) y medicion de memoria
 *
 * Escribe en ms0:/PSP/GAME/<carpeta>/devlog.txt (el de la partida anterior queda como devlog_prev.txt):
 *   - al arrancar: firmware, reloj de la CPU, bateria, RAM, VRAM y tiempo de carga de cada archivo
 *   - cada 5 s: FPS, ms de CPU y de GPU, vertices, memoria, VRAM, buffers del GE, bateria
 *   - eventos: cambios de estado, muertes, texturas cargadas en plena partida, tirones, avisos y errores
 * Escribe un hilo de baja prioridad cada 5 s (o al momento si hay un error) para no provocar tirones.
 * Viene activado (como en Subway Surfers); "make DEVLOG=0" lo quita para la version publica.
 */
#ifndef DEVLOG_H
#define DEVLOG_H

typedef struct {
    unsigned heap_total;     /* tamano del monton de C (bytes) */
    unsigned heap_used;      /* ocupado ahora */
    unsigned heap_peak;      /* maximo visto */
    unsigned sys_free;       /* memoria libre del sistema fuera del monton (pilas de hilos, audio...) */
    unsigned sys_max_block;  /* bloque libre mas grande fuera del monton */
    unsigned heap_free_in;   /* libre dentro de lo que el monton ya ha pedido (huecos) */
} MemInfo;

/* PSP-1000 (fat): 32 MB de RAM; al juego le quedan unos 20 MB de monton. El juego tiene que caber ahi */
#define FAT_HEAP_KB (20 * 1024)

/* datos para la pantalla de depuracion (se actualizan en dbg_frame) */
extern MemInfo dbg_mem;
extern int dbg_cpu_mhz, dbg_bus_mhz, dbg_bat_pct, dbg_bat_temp, dbg_plugged;
extern float dbg_frame_hist[128];   /* ms de cada fotograma (grafica) */
extern int dbg_hist_pos;
extern int dbg_hitches;             /* fotogramas de mas de 50 ms desde el arranque */
/* desglose del ultimo fotograma (lo rellena main antes de dbg_frame) */
extern float dbg_upd_ms, dbg_phys_ms, dbg_draw_ms;
/* dibujo por bloques: 0 fondo, 1 casillas, 2 objetos, 3 serpiente, 4 efectos, 5 menus, 6 texto, 7 interfaz */
extern float dbg_sect_ms[8];

void mem_init(void);                /* lo primero en main: mide el tamano real del monton */
void mem_query(MemInfo *m);
/* una vez por fotograma: frame_ms = tiempo real, cpu_ms = trabajo de la CPU, wait_ms = espera al GE + VBlank */
void dbg_frame(float frame_ms, float cpu_ms, float wait_ms);
/* datos del juego que se apuntan en el devlog (los rellena game.c) */
typedef struct {
    int state;            /* pantalla actual (GS_*) */
    int level;            /* numero del nivel original */
    int moves;
    int skin, bg;
} DbgGame;
extern DbgGame dbg_game;
extern int dbg_loads_frame;     /* lecturas de la Memory Stick en este fotograma */
const char *dbg_state_name(int s);

/* activado por defecto, como en Subway Surfers: la version publica se compila con DEVLOG=0 */
#ifndef DEVLOG
#define DEVLOG 1
#endif

/* fase en la que esta cada hilo y contador de vueltas, para el vigilante (watchdog): si el bucle principal deja
 * de avanzar 3 s, escribe en el devlog donde se quedo cada hilo */
extern volatile const char *dbg_phase_main, *dbg_phase_music, *dbg_phase_mixer;
extern volatile unsigned int dbg_count_music, dbg_count_mixer;
#define DBG_PHASE(thread, s) DBG_PHASE_##thread(s)
#define DBG_PHASE_music(s) (dbg_phase_music = (s))
#define DBG_PHASE_mixer(s) (dbg_phase_mixer = (s))
/* modo rastreo (diagnostico de cuelgues que congelan toda la consola): mientras esta activo, cada fase nueva del
 * hilo principal se escribe en trace.txt al momento (abrir, escribir, cerrar). La ultima linea es el ultimo paso que
 * llego a ejecutarse. Cada linea cuesta ~20 ms en la consola: solo se activa 2 fotogramas en las 2 primeras rodadas. */
extern volatile int dbg_tracing;
void dbg_trace(const char *phase);
void dbg_trace_start(const char *why, int frames);
#define DBG_PHASE_main(s) (dbg_phase_main = (s), dbg_tracing ? dbg_trace(s) : (void)0)

#if DEVLOG
const char *dlog_path(void);     /* ruta de devlog.txt (para el medidor de SELECT) */
int dlog_ok(void);               /* 1 si se ha podido escribir */
void dlog_init(void);
void dlog_boot_done(void);       /* fin del arranque: el vigilante pasa de 10 s a 3 s de margen */
void dlog(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void dlog_error(const char *fmt, ...) __attribute__((format(printf, 1, 2)));   /* se escribe al momento */
void dlog_flush(void);
void dlog_close(void);
/* apartado MEMORIA: estado de la memoria en un punto (al volver al inicio, a la tienda...) y aviso de fuga si
 * crece respecto a la primera vez que se paso por el mismo punto */
void dlog_mem(const char *where);
void dlog_mem_reset(void);
#else
#define dlog_mem(w) ((void)0)
#define dlog_mem_reset() ((void)0)
#define dlog_path() "desactivado (version publica: compila con DEVLOG=1)"
#define dlog_ok() 0
#define dlog_init() ((void)0)
#define dlog_boot_done() ((void)0)
#define dlog(...) ((void)0)
#define dlog_error(...) ((void)0)
#define dlog_flush() ((void)0)
#define dlog_close() ((void)0)
#endif

#endif
