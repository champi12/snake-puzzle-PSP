#!/bin/bash
# Actualiza el codigo, genera data/ desde tu copia del juego (solo si hace falta), compila y copia la carpeta PSP
# al escritorio de Windows.
#
#   ./actualizar.sh                        busca el juego (.xapk o los .rar partidos) aqui y en el escritorio,
#                                          y copia el resultado al escritorio de Windows que encuentre
#   ./actualizar.sh juego.xapk             juego concreto
#   ./actualizar.sh juego.xapk "/mnt/c/Users/Joan jimenez/Desktop"
#                                          ademas elige a que carpeta se copia la carpeta PSP
#
# Sale la version de PRUEBAS (devlog.txt y crash.txt). Para la publica, sin devlog: DEVLOG=0 ./actualizar.sh
set -e
export DEVLOG=${DEVLOG:-1}
cd "$(dirname "$0")"

if [ -z "$SNK_ACTUALIZADO" ]; then
    echo "== 1/4 Bajando cambios =="
    git pull --ff-only || echo "(git pull no pudo actualizar; se compila lo que hay)"
    # si git pull ha cambiado este mismo script, bash seguiria leyendo la version vieja: se vuelve a empezar
    SNK_ACTUALIZADO=1 exec "$0" "$@"
fi

# escritorios de Windows vistos desde WSL (el primero que exista)
desktops() {
    ls -d /mnt/c/Users/*/Desktop /mnt/c/Users/*/Escritorio /mnt/c/Users/*/OneDrive*/Desktop \
          /mnt/c/Users/*/OneDrive*/Escritorio 2>/dev/null | grep -v -E '/(Public|Default|All Users)/' || true
}

# es Snake Puzzle? (el XAPK trae el APK puzzle.snakes...; el APK suelto trae assets/bin/Data)
is_snake() {
    python3 - "$1" <<'EOF'
import sys, zipfile
try:
    n = zipfile.ZipFile(sys.argv[1]).namelist()
except Exception:
    sys.exit(1)
ok = any('puzzle.snakes' in x for x in n) or 'assets/bin/Data/data.unity3d' in n
sys.exit(0 if ok else 1)
EOF
}

echo "== 2/4 Python (entorno para el extractor) =="
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
    # 1) un .xapk/.apk de Snake Puzzle aqui o en el escritorio
    while IFS= read -r d; do
        for f in "$d"/*.xapk "$d"/*.apk; do
            [ -f "$f" ] || continue
            if is_snake "$f"; then GAME="$f"; break 2; fi
        done
    done < <(echo "."; desktops)
fi
if [ -z "$GAME" ]; then
    # 2) los .rar partidos (snake-puzzle-2-3-4.part1.rar ...): se descomprimen aqui una vez
    RAR=""
    while IFS= read -r d; do
        for f in "$d"/*snake*part1.rar "$d"/*Snake*part1.rar "$d"/*snake*.rar; do
            [ -f "$f" ] && { RAR="$f"; break 2; }
        done
    done < <(echo "."; desktops)
    if [ -n "$RAR" ]; then
        echo "Descomprimiendo $RAR ..."
        mkdir -p juego
        if command -v unrar > /dev/null; then unrar x -o+ "$RAR" juego/ > /dev/null
        elif command -v 7z > /dev/null; then 7z x -y -ojuego "$RAR" > /dev/null
        elif command -v unar > /dev/null; then unar -f -o juego "$RAR" > /dev/null
        else echo "Para los .rar hace falta unrar: sudo apt install unrar  (o 7zip / unar)"; exit 1; fi
        for f in juego/*.xapk juego/*/*.xapk; do [ -f "$f" ] && GAME="$f" && break; done
    fi
fi
VER=$(cat tools/extract.py tools/levels.py tools/unityload.py | sha1sum | cut -c1-16)
if [ -f data/.version ] && [ "$(cat data/.version)" = "$VER" ]; then
    echo "data/ esta al dia (el extractor no ha cambiado)"
else
    if [ -z "$GAME" ] || [ ! -f "$GAME" ]; then
        echo "Hay que generar data/ pero no encuentro el juego (snake-puzzle-2-3-4.xapk o sus .rar)."
        echo "Ponlo en el escritorio o en esta carpeta, o pasalo: ./actualizar.sh ruta/al/juego.xapk"
        exit 1
    fi
    echo "Generando data/ desde $GAME (1-2 minutos)..."
    rm -rf data.new
    "$PY" tools/extract.py "$GAME" data.new/
    rm -rf data && mv data.new data
    echo "$VER" > data/.version
fi

echo "== 4/4 Compilando =="
[ -z "$PSPDEV" ] && export PSPDEV=$HOME/pspdev && export PATH=$PSPDEV/bin:$PATH
command -v psp-gcc > /dev/null || { echo "No encuentro psp-gcc: exporta PSPDEV con la ruta de pspdev"; exit 1; }
./build_dist.sh
rm -f dist/PSP/GAME/SNAKEPZL/data/.version

DEST=$2
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
