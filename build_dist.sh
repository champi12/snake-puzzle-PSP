#!/bin/bash
# Arma dist/PSP/GAME/SNAKEPZL con el EBOOT y (si existe) la carpeta data/ generada con tools/extract.py
#   ./build_dist.sh            version publica (sin devlog)
#   DEVLOG=1 ./build_dist.sh   version de pruebas: devlog.txt, vigilante de bloqueos y capturador de cuelgues (crash.txt)
set -e
cd "$(dirname "$0")"
DEVLOG=${DEVLOG:-0}
# siempre desde cero: un .o viejo con otra version de una cabecera da fallos muy raros
make clean > /dev/null
make -C exhandler clean > /dev/null 2>&1 || true
make -C exhandler/loader clean > /dev/null 2>&1 || true
make DEVLOG=$DEVLOG
rm -rf dist
mkdir -p dist/PSP/GAME/SNAKEPZL
cp EBOOT.PBP dist/PSP/GAME/SNAKEPZL/
if [ "$DEVLOG" != 0 ]; then
    make -C exhandler          # capturador de excepciones (crash.txt)
    make -C exhandler/loader   # plan B para cargarlo con kubridge
    cp exhandler/exhandler.prx exhandler/loader/exloader.prx dist/PSP/GAME/SNAKEPZL/
    cp snake.elf dist/snake.elf   # para traducir crash.txt a funcion y linea (tools/crash.sh); no va a la PSP
fi
if [ -d data ]; then
    cp -rL data dist/PSP/GAME/SNAKEPZL/   # -L: si data es un enlace, se copia lo que hay dentro
    rm -f dist/PSP/GAME/SNAKEPZL/data/{ICON0.PNG,PIC1.PNG,VERSION}
else
    echo "AVISO: no hay carpeta data/ (generala con tools/extract.py): el EBOOT solo no arranca el juego"
fi
[ "$DEVLOG" != 0 ] && echo "Version de PRUEBAS (con devlog)" || echo "Version publica (sin devlog)"
echo "Listo: dist/PSP/GAME/SNAKEPZL (copiala a la Memory Stick en ms0:/PSP/GAME/)"
