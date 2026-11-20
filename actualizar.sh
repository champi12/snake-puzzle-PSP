#!/bin/bash
# Actualiza el codigo, genera data/ desde tu copia del juego (solo si hace falta), compila y copia la carpeta PSP
# al escritorio de Windows.
#
#   ./actualizar.sh                        busca el juego (.xapk o los .rar partidos) aqui y en el escritorio (si hay
#                                          varias versiones usa la mas nueva, p. ej. la 14.2.9), y copia el resultado
#                                          al escritorio de Windows
#   ./actualizar.sh juego.xapk             juego concreto
#   ./actualizar.sh juego.xapk "/mnt/c/Users/Joan jimenez/Desktop"
#                                          ademas elige a que carpeta se copia la carpeta PSP
#
# Sale la version publica (sin devlog). Para la de PRUEBAS (devlog.txt y crash.txt): DEVLOG=1 ./actualizar.sh
set -e
export DEVLOG=${DEVLOG:-0}
cd "$(dirname "$0")"

if [ -z "$SNK_ACTUALIZADO" ]; then
    echo "== 1/4 Bajando cambios =="
    git pull --ff-only || echo "(git pull no pudo actualizar; se compila lo que hay)"
    # si git pull ha cambiado este mismo script, bash seguiria leyendo la version vieja: se vuelve a empezar
    SNK_ACTUALIZADO=1 exec "$0" "$@"
fi

# escritorios de Windows vistos desde WSL (el primero es el que se usa)
desktops() {
    # 1) el escritorio que Windows usa de verdad (si OneDrive lo ha movido, es ...\OneDrive\Escritorio)
    if command -v powershell.exe > /dev/null; then
        local w
        w=$(powershell.exe -NoProfile -Command "[Environment]::GetFolderPath('Desktop')" 2>/dev/null | tr -d '\r')
        [ -n "$w" ] && w=$(wslpath -u "$w" 2>/dev/null) && [ -d "$w" ] && echo "$w"
    fi
    # 2) si no, los de OneDrive primero y luego los normales
    ls -d /mnt/c/Users/*/OneDrive*/Escritorio /mnt/c/Users/*/OneDrive*/Desktop 2>/dev/null | grep -v -E '/(Public|Default|All Users)/' || true
    ls -d /mnt/c/Users/*/Escritorio /mnt/c/Users/*/Desktop 2>/dev/null | grep -v -E '/(Public|Default|All Users)/' || true
}

# es Snake Puzzle? (el XAPK trae el APK puzzle.snakes...; el APK suelto trae assets/bin/Data)
is_snake() {
    python3 - "$1" <<'EOF2'
import sys, zipfile
try:
    n = zipfile.ZipFile(sys.argv[1]).namelist()
except Exception:
    sys.exit(1)
ok = any('puzzle.snakes' in x for x in n) or 'assets/bin/Data/data.unity3d' in n
sys.exit(0 if ok else 1)
EOF2
}

# version del juego (version_name del manifest.json del XAPK, o la del nombre del archivo): para quedarse con la mas
# nueva si hay varias (la 2.3.4 y la 14.2.9, por ejemplo)
game_version() {
    python3 - "$1" <<'EOF2'
import sys, zipfile, json, re
v = ''
try:
    z = zipfile.ZipFile(sys.argv[1])
    if 'manifest.json' in z.namelist():
        v = json.loads(z.read('manifest.json')).get('version_name', '')
except Exception:
    pass
if not v:
    m = re.search(r'(\d+)[-_.](\d+)[-_.](\d+)', sys.argv[1].split('/')[-1])
    v = '.'.join(m.groups()) if m else '0'
print('%05d%05d%05d %s' % tuple([int(x) for x in (re.findall(r'\d+', v) + ['0', '0', '0'])[:3]] + [v]))
EOF2
}

echo "== 2/4 Python (entorno para el extractor) =="
# los sonidos de Unity (FSB5/Vorbis) se convierten con libogg y libvorbis del sistema: sin ellas el juego sale mudo
if ! ldconfig -p 2>/dev/null | grep -q 'libvorbis.so.0' || ! ldconfig -p 2>/dev/null | grep -q 'libogg.so.0' \
   || ! ldconfig -p 2>/dev/null | grep -q 'libsndfile.so.1'; then
    echo "Faltan las librerias de sonido (libogg, libvorbis, libsndfile): se instalan (pide tu contrasena de Ubuntu)"
    sudo apt-get update -qq && sudo apt-get install -y libogg0 libvorbis0a libvorbisfile3 libsndfile1 \
        || { echo "No se pudieron instalar. Ejecuta: sudo apt install libogg0 libvorbis0a libvorbisfile3 libsndfile1"; exit 1; }
fi
VENV=$HOME/venv-snake
if [ ! -x "$VENV/bin/python3" ]; then
    echo "Creando $VENV (solo la primera vez)..."
    python3 -m venv "$VENV" || { echo "Falta python3-venv: sudo apt install python3-venv python3-pip"; exit 1; }
fi
REQ_SUM=$(sha1sum tools/requirements.txt | cut -c1-16)
if [ "$(cat "$VENV/.req" 2>/dev/null)" != "$REQ_SUM" ]; then
    "$VENV/bin/pip" install -q -r tools/requirements.txt
    echo "$REQ_SUM" > "$VENV/.req"
fi
PY=$VENV/bin/python3

echo "== 3/4 Datos del juego =="
GAME=${1:-$SNAKE_XAPK}
if [ -z "$GAME" ]; then
    # 1) los .rar partidos (SnakePuzzle_...14.2.9...part1.rar, snake-puzzle-2-3-4.part1.rar ...): cada uno se
    #    descomprime una vez en juego/ (se apunta en juego/.hecho)
    mkdir -p juego
    while IFS= read -r d; do
        for f in "$d"/*[Ss]nake*part1.rar "$d"/*[Ss]nake*.rar "$d"/*/*[Ss]nake*part1.rar "$d"/*/*[Ss]nake*.rar; do
            [ -f "$f" ] || continue
            case "$f" in *.part[2-9].rar|*.part[1-9][0-9].rar) continue ;; esac
            base=$(basename "$f")
            grep -qxF "$base" juego/.hecho 2>/dev/null && continue
            echo "Descomprimiendo $f ..."
            if command -v unrar > /dev/null; then unrar x -o+ "$f" juego/ > /dev/null
            elif command -v 7z > /dev/null; then 7z x -y -ojuego "$f" > /dev/null
            elif command -v unar > /dev/null; then unar -f -o juego "$f" > /dev/null
            else echo "Para los .rar hace falta unrar: sudo apt install unrar  (o 7zip / unar)"; exit 1; fi
            echo "$base" >> juego/.hecho
        done
    done < <(echo "."; desktops)
    # 2) de todos los .xapk/.apk de Snake Puzzle (aqui, en juego/ y en el escritorio), el de version mas alta
    BEST=""
    while IFS= read -r d; do
        for f in "$d"/*.xapk "$d"/*.apk "$d"/*/*.xapk; do
            [ -f "$f" ] || continue
            is_snake "$f" || continue
            v=$(game_version "$f")
            if [ -z "$BEST" ] || [ "${v%% *}" \> "${BEST%% *}" ]; then BEST="$v|$f"; fi
        done
    done < <(echo "."; echo "juego"; desktops)
    if [ -n "$BEST" ]; then
        GAME="${BEST#*|}"
        V="${BEST%%|*}"
        echo "Juego: $GAME (version ${V#* })"
    fi
    # en juego/ solo hace falta la version que se usa: las demas (que descomprimio este script) se borran para no
    # ocupar cientos de MB. No se toca nada fuera de juego/ (ni el escritorio ni los .rar)
    if [ -n "$GAME" ]; then
        for f in juego/*.xapk juego/*/*.xapk juego/*.apk juego/*/*.apk; do
            [ -f "$f" ] || continue
            [ "$(realpath "$f")" = "$(realpath "$GAME")" ] && continue
            echo "Borrando version vieja descomprimida: $f ($(du -h "$f" | cut -f1))"
            rm -f "$f"
        done
    fi
fi
VER=$(cat tools/extract.py tools/levels.py tools/unityload.py | sha1sum | cut -c1-16)
# data/ tambien se rehace si el juego elegido es otro (al pasar de la 2.3.4 a la 14.2.9, por ejemplo)
GAME_ID=""
[ -n "$GAME" ] && [ -f "$GAME" ] && GAME_ID=$(stat -c '%s' "$GAME")
if [ -n "$GAME_ID" ] && [ "$(cat data/.game 2>/dev/null)" != "$GAME_ID" ]; then
    rm -f data/.version
fi
# data/ sin sonido (generada sin libvorbis): se rehace
SFX_SIZE=$(stat -c %s data/sfx.bin 2>/dev/null || echo 0)
if [ "$SFX_SIZE" -lt 10000 ] || [ ! -f data/music.mp3 ]; then
    rm -f data/.version
fi
if [ -f data/.version ] && [ "$(cat data/.version)" = "$VER" ]; then
    echo "data/ esta al dia (el extractor no ha cambiado)"
else
    if [ -z "$GAME" ] || [ ! -f "$GAME" ]; then
        echo "Hay que generar data/ pero no encuentro el juego (el .xapk de Snake Puzzle 14.2.9 o sus .rar)."
        echo "Ponlo en el escritorio o en esta carpeta, o pasalo: ./actualizar.sh ruta/al/juego.xapk"
        exit 1
    fi
    echo "Generando data/ desde $GAME (1-2 minutos)..."
    rm -rf data.new
    "$PY" tools/extract.py "$GAME" data.new/
    rm -rf data && mv data.new data
    echo "$VER" > data/.version
    echo "$GAME_ID" > data/.game
fi

# Apple Worm se quito del todo (tambien su selector de niveles: la pantalla NIVELES es la de Snake Puzzle 14.2.9)
rm -rf data/aw data/lvsel.spr data/lvsel.idx data/.lvsel

echo "== 4/4 Compilando =="
[ -z "$PSPDEV" ] && export PSPDEV=$HOME/pspdev && export PATH=$PSPDEV/bin:$PATH
command -v psp-gcc > /dev/null || { echo "No encuentro psp-gcc: exporta PSPDEV con la ruta de pspdev"; exit 1; }
./build_dist.sh
rm -f dist/PSP/GAME/SNAKEPZL/data/.version dist/PSP/GAME/SNAKEPZL/data/.game

DEST=$2
# destino fijo de Joan: C:\Users\Joan jimenez\Desktop\PSP\GAME\SNAKEPZL
[ -z "$DEST" ] && [ -d "/mnt/c/Users/Joan jimenez/Desktop" ] && DEST="/mnt/c/Users/Joan jimenez/Desktop"
[ -z "$DEST" ] && DEST=$(desktops | head -1)
if [ -n "$DEST" ]; then
    # no se crea la carpeta destino: si no existe es que la ruta esta mal escrita
    if [ ! -d "$DEST" ]; then
        echo
        echo "No existe la carpeta: $DEST"
        echo "El juego SI se ha compilado: esta en $(pwd)/dist/PSP"
        echo "Escritorios encontrados en Windows (usa uno de estos, entre comillas):"
        desktops | sed 's/^/    "/; s/$/"/'
        exit 1
    fi
    rm -rf "$DEST/PSP/GAME/SNAKEPZL"
    mkdir -p "$DEST/PSP/GAME"
    cp -r dist/PSP/GAME/SNAKEPZL "$DEST/PSP/GAME/"
    echo
    echo "Copiado a: $DEST/PSP/GAME/SNAKEPZL"
    echo "Copia la carpeta PSP a la raiz de la Memory Stick (o a memstick/ de PPSSPP)."
else
    echo
    echo "Listo en $(pwd)/dist/PSP (no encontre el escritorio de Windows para copiarlo)"
fi
