/* lang.c - traducciones al ingles de los textos del port (ver lang.h) */
#include <string.h>
#include "lang.h"
#include "game.h"

int lang_en;

void lang_init(int psp_language) { lang_en = psp_language != 3; }

static const char *const tr[][2] = {
    /* inicio */
    {"JUGAR", "PLAY"}, {"NIVELES", "LEVELS"}, {"CLASICO", "CLASSIC"}, {"TIENDA", "SHOP"}, {"DIARIO", "DAILY"},
    {"AJUSTES", "SETTINGS"}, {"JUGAR  (nivel %d)", "PLAY  (level %d)"}, {"%d fichas", "%d tokens"},
    {BTN_X " Aceptar   Cruceta Elegir", BTN_X " Accept   D-pad Select"},
    /* niveles */
    {"NIVELES   %d / %d", "LEVELS   %d / %d"},
    {BTN_X " Jugar   " BTN_O " Volver   L/R Cambiar de pagina", BTN_X " Play   " BTN_O " Back   L/R Change page"},
    {BTN_X " Jugar   " BTN_TRI " Tienda   " BTN_O " Volver   L/R Pagina", BTN_X " Play   " BTN_TRI " Shop   " BTN_O " Back   L/R Page"},
    {"Supera los niveles anteriores para abrir este", "Beat the previous levels to unlock this one"},
    /* pausa */
    {"PAUSA", "PAUSED"}, {"CONTINUAR", "CONTINUE"}, {"REINICIAR", "RESTART"}, {"INICIO", "HOME"},
    {BTN_X " Aceptar   " BTN_SQ " Sonido   " BTN_TRI " Musica   START Seguir",
     BTN_X " Accept   " BTN_SQ " Sound   " BTN_TRI " Music   START Resume"},
    /* superado / perdido */
    {"Fondo nuevo: %d%%", "New background: %d%%"}, {"Aspecto nuevo: %d%%", "New skin: %d%%"},
    {"SUPERADO!", "LEVEL COMPLETE!"}, {"Nivel %d completado", "Level %d completed"},
    {"Mejor que el %d,%02d%% de los jugadores", "Better than %d.%02d%% of players"},
    {"SIGUIENTE", "NEXT"}, {"REPETIR", "REPLAY"}, {"OH, NO!", "OH, NO!"}, {"REINTENTAR", "RETRY"},
    {"PISTA", "HINT"}, {"SALTAR", "SKIP"}, {"PISTA (%d)", "HINT (%d)"}, {"SALTAR (%d)", "SKIP (%d)"},
    /* tienda */
    {"Supera %d niveles", "Beat %d levels"}, {"Evento de Halloween", "Halloween event"},
    {"Evento de Navidad", "Christmas event"}, {"Evento especial", "Special event"},
    {"Diario o fichas", "Daily reward or tokens"}, {"%d pistas", "%d hints"}, {"%d saltos", "%d skips"},
    {"aspecto", "skin"}, {"fondo", "background"}, {"No hay paquetes", "No packs available"},
    {"Faltan fichas: ganas 1 por cada nivel nuevo superado", "Not enough tokens: you earn 1 for each new level beaten"},
    {"Comprado: %s", "Bought: %s"}, {"ASPECTOS", "SKINS"}, {"FONDOS", "SCENES"}, {"PACKS", "PACKS"},
    {BTN_X " Comprar con fichas   " BTN_O " Volver   L/R Pestana", BTN_X " Buy with tokens   " BTN_O " Back   L/R Tab"},
    {"EQUIPADO", "EQUIPPED"}, {BTN_X " Equipar", BTN_X " Equip"}, {BTN_X " Pagar ficha (%d/%d)", BTN_X " Pay token (%d/%d)"},
    {"Bloqueado", "Locked"},
    {BTN_X " Elegir/pagar   " BTN_O " Volver   L/R Pestana", BTN_X " Select/pay   " BTN_O " Back   L/R Tab"},
    {"Sin fichas: gana 1 ficha por cada nivel nuevo superado", "No tokens: earn 1 token for each new level beaten"},
    /* diario */
    {"RECOMPENSA DIARIA", "DAILY REWARD"}, {"Dia %d", "Day %d"}, {BTN_X " Cobrar   " BTN_O " Volver", BTN_X " Claim   " BTN_O " Back"},
    {"Vuelve manana a por mas   " BTN_O " Volver", "Come back tomorrow for more   " BTN_O " Back"},
    {"+%d pista", "+%d hint"}, {"+%d pistas", "+%d hints"},
    /* ajustes */
    {"SONIDO: %s", "SOUND: %s"}, {"MUSICA: %s", "MUSIC: %s"}, {"SI", "ON"}, {"NO", "OFF"},
    {"PULSA OTRA VEZ", "PRESS AGAIN"}, {"BORRAR PARTIDA", "DELETE SAVE"},
    {"%d niveles superados | %uh %02um jugadas", "%d levels beaten | %uh %02um played"},
    {"Snake Puzzle 14.2.9 - port para PSP", "Snake Puzzle 14.2.9 - PSP port"},
    {"En PACKS (con fichas)", "In PACKS (with tokens)"}, {"Oferta limitada", "Limited offer"},
    {"Ano Nuevo Lunar", "Lunar New Year"}, {"Bajo el mar", "Under the Sea"}, {"Desfile arcoiris", "Rainbow Parade"},
    {"Gol de oro", "Golden Goal"},
    {BTN_X " Comprar: %d fichas", BTN_X " Buy: %d tokens"},
    {"Te faltan %d fichas: ganas 1 por cada nivel nuevo superado", "You need %d more tokens: you earn 1 per new level"},
    {"Quedan", "Ends in"}, {"Abre en", "Opens in"},
    {"Snake Retro abre los miercoles y los sabados", "Snake Retro opens on Wednesdays and Saturdays"},
    {BTN_X " Cambiar   " BTN_O " Volver", BTN_X " Change   " BTN_O " Back"}, {"Partida borrada", "Save deleted"},
    /* premio */
    {"FONDO NUEVO!", "NEW SCENE!"}, {"ASPECTO NUEVO!", "NEW SKIN!"},
    {BTN_X " Usar ahora   " BTN_O " Seguir", BTN_X " Use now   " BTN_O " Continue"},
    /* partida */
    {"Este nivel no tiene pista", "This level has no hint"},
    {"No te quedan pistas: consiguelas en el Diario y en el modo Clasico",
     "No hints left: get more from the Daily reward and Classic mode"},
    {"No te quedan saltos de nivel", "No level skips left"}, {"NIVEL %d", "LEVEL %d"}, {"Movs. %d", "Moves %d"},
    {"PISTA: sigue la flecha   " BTN_TRI " Reiniciar   START Pausa", "HINT: follow the arrow   " BTN_TRI " Restart   START Pause"},
    {BTN_SQ " Pista   " BTN_TRI " Reiniciar   L Deshacer   R Saltar   START Pausa",
     BTN_SQ " Hint   " BTN_TRI " Restart   L Undo   R Skip   START Pause"},
    /* clasico */
    {"PUNTOS %d", "SCORE %d"}, {"RECORD %d", "BEST %d"}, {"Pulsa la cruceta para empezar", "Press the D-pad to start"},
    {"TU", "YOU"}, {"Premio: +%d pista", "Reward: +%d hint"}, {"Premio: +%d pistas", "Reward: +%d hints"},
    {BTN_X " Otra vez   " BTN_O " Inicio", BTN_X " Play again   " BTN_O " Home"},
    {"Cruceta Girar   START Pausa   " BTN_O " Salir", "D-pad Turn   START Pause   " BTN_O " Exit"},
    {"NUEVO PRIMER PUESTO!", "NEW FIRST PLACE!"}, {"RANKING", "RANKING"},
};

const char *T(const char *es)
{
    if (!lang_en || !es) return es;
    for (unsigned i = 0; i < sizeof(tr) / sizeof(tr[0]); i++)
        if (!strcmp(tr[i][0], es)) return tr[i][1];
    return es;
}
