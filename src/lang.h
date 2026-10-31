/* lang.h - idioma de los textos: espanol o ingles, segun el idioma configurado en la consola
 *
 * El original solo trae ingles. Los textos se escriben en espanol en el codigo y T() devuelve la traduccion inglesa
 * cuando la consola no esta en espanol.
 */
#ifndef LANG_H
#define LANG_H

extern int lang_en;                 /* 1 ingles, 0 espanol */
void lang_init(int psp_language);   /* PSP_SYSTEMPARAM_LANGUAGE_*: 3 = espanol */
const char *T(const char *es);

#endif
