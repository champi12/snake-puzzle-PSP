# Snake Puzzle para PSP

Port nativo para PSP de **Snake Puzzle 2.3.4** (Android, `puzzle.snakes.puzzle.slithereat.gaming`, Unity 2022.3).
No es un juego "parecido": la logica del original se ha leido del codigo compilado (IL2CPP, ARM64) y se ha
**traducido a C**, y los niveles, graficos, fuente, sonidos y musica se **extraen del propio APK**.

> El repositorio **no contiene ningun recurso del juego**. Cada uno genera la carpeta `data/` con su propia copia
> del XAPK usando `tools/extract.py`.

> **Hecho con ayuda de IA.** Este port se ha desarrollado con la ayuda de **Claude** (Anthropic), un asistente de
> inteligencia artificial, que ha escrito gran parte del codigo junto al autor. Si no estas de acuerdo con el uso de
> estas herramientas, este proyecto no es para ti.

## Que se ha portado

| Original (Unity/C#) | Port (C) | Notas |
|---|---|---|
| `SnakeController` (`HandleMovement`, `TryMove`, `Grow`, `CheckForSnakeFall`, `Initialize`) | `src/logic.c` | Raycast de 1 casilla desde la cabeza y circulo de radio 0,2 bajo cada parte, pasados a casillas |
| `Stone` (`CanPush`, `TryPush`, `TryTeleportStone`) | `src/logic.c` | Filas de piedras empujadas enteras, piedras por los portales, gravedad |
| `Trap`, `SawRotate` | `src/logic.c`, `src/play.c` | Pinchos y sierras: entrar en su casilla mata; pisarlos por arriba no |
| `TeleportGate` (`GetDirection`, `GetPosTeleport`) | `src/logic.c` | Portales por parejas con su direccion de entrada |
| `Switch`, `Barrie` | `src/logic.c` | Interruptor que recoge la barrera; la barrera no aplasta la serpiente |
| `Apple` (`isDouble`) | `src/logic.c` | Manzanas normales y dobles (+2 de largo) |
| `Portal`, `PortalRotate`, `StartAutoMove` | `src/play.c` | La meta: la serpiente entra entera y `LevelComplete` |
| `ToolHint`, `ToolFileHandler` (pistas cifradas con XOR "appleworm") | `src/play.c`, `tools/extract.py` | La pista muestra la solucion oficial del nivel paso a paso |
| `ClassicGamePlay`, `ClassicController`, `ClassicLeaderboardData` | `src/classic.c` | Modo clasico 13x12 con bordes que se cruzan, ranking de 10 rivales y pistas de premio |
| `ShopController`, `ShopRegionUI.sortIndex`, `ShopInfo` | `src/ui.c` | Tienda de 29 aspectos y 14 fondos en el orden del original |
| `SkinProgress`, `RewardProgressPopupController` | `src/game.c`, `src/ui.c` | Barra de progreso y aspectos/fondos regalados por niveles superados |
| `DailyRewardController` | `src/ui.c` | Recompensa diaria de 7 dias (pistas y aspectos) con el reloj de la PSP |
| `top_player` | `src/ui.c` | "Mejor que el X% de los jugadores" al superar un nivel |
| `SoundManager`, BGM y BGM_Lunar | `src/audio_psp.c` | Efectos PCM y musica MP3 por el Media Engine; la del Ano Nuevo Lunar en sus fechas |
| Eventos (Halloween, Navidad, Ano Nuevo Lunar) | `src/game.c` | Los articulos de evento se regalan mientras dura el evento |

**Contenido:** 266 niveles (todos los que trae el APK con contenido), 29 aspectos de serpiente con sus animaciones de
cabeza (parpadeo, comer, atascarse, morir), 14 fondos, modo clasico, tienda, recompensa diaria, progreso de
aspectos, pistas, saltos de nivel, ajustes y la musica original.

**Lo que dependia de internet** se ha sustituido por algo local:
* "Ver un anuncio" para desbloquear un aspecto o fondo -> **fichas**: 1 por cada nivel nuevo superado (cada articulo
  pide las mismas fichas que anuncios pedia el original). Cada 5 niveles nuevos, +1 pista.
* El ranking online del modo clasico -> los 10 rivales que trae el APK (`ClassicLeaderboardData`).
* Las compras y los packs de pago (`ShopIAP`, `SpecialBundle`, `NoAds`) no existen en la PSP: no hay anuncios.
* Los niveles 101, 147, 167 y 175 vienen vacios en el APK (el original los sustituia con configuracion remota): se omiten.

**Extra del port:** deshacer el ultimo movimiento (L).

### Fidelidad de la logica

`tests/host_test.c` juega en el PC la **solucion oficial de cada nivel** (las pistas del APK) con la logica del port:
hoy coinciden **247 de 266**. En los 19 restantes la solucion oficial no llega a la meta con las reglas actuales
(sobre todo niveles con piedras y portales); se pueden jugar y superar igual, pero la pista de esos niveles puede no
llevar a la meta. Ver [docs/DESCOMPILACION.md](docs/DESCOMPILACION.md).

## Controles

| Boton | En el juego | En los menus |
|---|---|---|
| Cruceta o stick | Mover la serpiente (mantener: repetir) | Elegir |
| X | | Aceptar |
| O | | Volver |
| □ (cuadrado) | Pista (gasta 1) | Sonido (en pausa) |
| △ (triangulo) | Reiniciar el nivel | Musica (en pausa) |
| L | Deshacer | Pagina/pestana anterior |
| R | Saltar el nivel (gasta 1) | Pagina/pestana siguiente |
| START | Pausa | |
| SELECT | Medidor de rendimiento (3 paginas) | |

## Como generar los datos

Necesitas Python 3 y:

    pip install -r tools/requirements.txt
    python3 tools/extract.py snake-puzzle-2-3-4.xapk data/

Si tienes el XAPK partido en `.rar`, descomprimelo antes (`unrar x snake-puzzle-2-3-4.part1.rar`). Tambien vale el APK
base con `config.arm64_v8a.apk` en la misma carpeta (hace falta `libil2cpp.so` para leer los scripts).

| Archivo | Contenido |
|---|---|
| `levels.bin` | 266 niveles: cuadricula, casillas de decorado, objetos y la solucion oficial |
| `world.spr` | Casillas, objetos e interfaz del juego (32 px por casilla, paleta de 256 colores, *swizzled*) |
| `skinNN.spr` | Cada aspecto: cuerpo, cola, esquinas, 28 fotogramas de la cabeza y su manzana (512x256, 129 KB) |
| `bgNN.tex`, `home.tex` | Fondos a 480x272 en 16 bits con difuminado |
| `shop.spr` | Iconos de la tienda y del progreso |
| `font.spr` | La fuente del juego (Lilita One) a 12, 16, 22 y 30 px, mas los simbolos ✕ ○ □ △ de la PSP |
| `meta.bin` | Tienda, progreso de aspectos, recompensa diaria, ranking clasico y top de jugadores |
| `sfx.bin` | 14 efectos en PCM 16 bits 22.050 Hz |
| `music.mp3`, `music_lunar.mp3` | La musica original en MP3 (la decodifica el Media Engine) |
| `ICON0.PNG`, `PIC1.PNG` | Icono y fondo del menu de la PSP (van dentro del EBOOT) |

En total unos 12 MB en la Memory Stick.

## Como compilar

Toolchain [pspdev](https://github.com/pspdev/pspdev) (la compilada para Linux sirve tal cual, tambien en WSL):

    export PSPDEV=<ruta>/pspdev; export PATH=$PSPDEV/bin:$PATH
    make                # genera EBOOT.PBP
    ./build_dist.sh     # arma dist/PSP/GAME/SNAKEPZL con el EBOOT y data/

Como en Subway Surfers, la compilacion normal es la version de pruebas: escribe el registro `devlog.txt` (en la
carpeta del juego; si no se puede, en `ms0:/PSP/GAME/SNAKEPZL/` o en `ms0:/devlog_snakepuzzle.txt`) y el capturador
de cuelgues `crash.txt` (ver [docs/RENDIMIENTO.md](docs/RENDIMIENTO.md)). La pantalla de inicio muestra donde se
esta escribiendo el devlog y el estado del audio. `make DEVLOG=0` (o `DEVLOG=0 ./build_dist.sh`) genera la version
publica, que solo guarda la partida (`save.dat`).

Idioma: los textos salen en espanol si la consola esta en espanol y en ingles con cualquier otro idioma.

### Pruebas en el PC (sin PSP)

    make -C tests check DATA=../data    # juega las 266 soluciones oficiales con la logica del port
    make -C tests shots DATA=../data    # el juego entero por software: capturas en tests/out/

## Instalar

En la Memory Stick: `ms0:/PSP/GAME/SNAKEPZL/EBOOT.PBP` y `ms0:/PSP/GAME/SNAKEPZL/data/`. La version de pruebas lleva
ademas `exhandler.prx` y `exloader.prx`. Funciona en firmware personalizado (CFW/ARK/PRO) y en PPSSPP.

## Rendimiento y memoria

* 60 FPS: el juego es 2D y dibuja 100-230 sprites por fotograma.
* Pantalla de 16 bits con doble buffer y sin buffer Z (544 KB de VRAM); el resto de la VRAM (1,5 MB) para el atlas del
  mundo, el aspecto y el fondo, que caben enteros.
* Texturas reducidas a 32 px por casilla (el original usa 100) y con paleta de 256 colores: el atlas del mundo ocupa
  771 KB en vez de varios MB.
* Musica MP3 por el Media Engine: no gasta CPU. Efectos mezclados en un hilo aparte.
* Solo se carga el aspecto y el fondo elegidos; los demas se leen de la Memory Stick al cambiarlos.

## Pendiente

* Probarlo en una PSP real y en PPSSPP (todavia no se ha ejecutado en ninguno de los dos).
* Los 19 niveles cuya solucion oficial no coincide todavia con las reglas del port.
* Anadir la campana del **Apple Worm** original (135 niveles de `levels.json`) como contenido extra.
