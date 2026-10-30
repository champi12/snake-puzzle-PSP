/* skin.h - orden de los sprites de cada aspecto (data/skinNN.spr, ver SKIN_PARTS y HEAD_FRAMES en tools/extract.py) */
#ifndef SKIN_H
#define SKIN_H
#define SK_H1 0        /* cuerpo claro horizontal */
#define SK_V1 1        /* cuerpo claro vertical */
#define SK_C1 2        /* esquina clara que une derecha y abajo (bodyCorner_TopLeft) */
#define SK_T1 3        /* cola clara: une con la parte de su derecha */
#define SK_H2 4        /* lo mismo en oscuro */
#define SK_V2 5
#define SK_C2 6
#define SK_T2 7
#define SK_O1 8        /* esquina clara que une arriba e izquierda (bodyCorner_TopRight) */
#define SK_O2 9
#define SK_IDLE 10     /* cabeza: reposo 1-3 */
#define SK_BLINK 13    /* parpadeo 1-5 */
#define SK_EAT 18      /* comer 1-10 */
#define SK_ITE 28      /* abrir la boca 1-5 */
#define SK_ITS 33      /* atascarse 1-4 */
#define SK_DIE 37
#define SK_APPLE 38    /* manzana del aspecto (Apple.ChangeSkin) */
#define SK_ICON 39
#endif
