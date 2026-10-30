# Rendimiento y memoria: cómo saber si funciona en una PSP real

Hay dos herramientas dentro del propio juego, y las dos funcionan igual en PPSSPP y en la consola (son las mismas
que las del port de Subway Surfers):

1. **El medidor en pantalla** (botón **SELECT**): FPS, tiempos, RAM, VRAM y una gráfica de fotogramas.
2. **El devlog** (`devlog.txt` en la carpeta del juego): un registro de todo lo que pasa, para leerlo después en el PC.

## 1. El medidor en pantalla (SELECT)

Cada pulsación de SELECT pasa a la página siguiente: **resumen → memoria → gráfica → oculto**.

### Página 1: resumen

| Dato | Qué es |
|---|---|
| `FPS` | Fotogramas por segundo (verde ≥ 55, amarillo ≥ 30, rojo menos). El juego va a 60 |
| `CPU x ms` | Trabajo de la CPU en cada fotograma (lógica + preparar el dibujo) |
| `espera x ms` | Tiempo esperando a que el GE termine y llegue el VBlank |
| `dibujos / vert / texturas` | Llamadas de dibujo, vértices y cambios de textura del fotograma |
| `RAM` / `VRAM` | Memoria ocupada (verde < 75 %, amarillo < 90 %, rojo más) |

### Página 2: memoria

| Línea | Qué mirar |
|---|---|
| **RAM montón: usado / total (pico)** | Toda la memoria que pide el juego con `malloc` (niveles, atlas, efectos). El **total** es lo que la consola le deja al juego. Si el **pico** se acerca al total, se colgaría al cargar algo |
| **Sistema libre** | Memoria fuera del montón (pilas de los hilos, módulos de audio). Por debajo de 256 KB sale en rojo |
| **VRAM** | La memoria de vídeo (2 MB): 544 KB son las dos pantallas de 16 bits; el resto, el atlas del mundo, el aspecto elegido y el fondo |
| **Texturas en RAM** | Las que no caben en la VRAM se leen de la RAM. Funciona, pero gasta más ancho de banda (con los datos actuales debe ser 0) |
| **Lista GE** | Espacio usado de la lista de comandos del GE. Por encima del 80 % sale en rojo |
| **Tirones** | Fotogramas de más de 50 ms desde que arrancó el juego |
| **CPU / bus / batería** | Frecuencias reales y estado de la batería |

### Página 3: gráfica de tiempos

Los últimos 128 fotogramas: **verde** por debajo de 16,7 ms (60 FPS), **amarillo** por debajo de 33,3 ms (30 FPS) y
**rojo** por encima.

## 2. El devlog (`devlog.txt`)

Solo lo escribe la **versión de pruebas** (`make clean && make DEVLOG=1` o `DEVLOG=1 ./build_dist.sh`). El juego lo
escribe en su carpeta: `ms0:/PSP/GAME/SNAKEPZL/devlog.txt` en la consola, `memstick/PSP/GAME/SNAKEPZL/` en PPSSPP.
Se guardan los **tres últimos**: `devlog.txt`, `devlog_prev.txt` y `devlog_prev2.txt`.

Lo escribe un hilo de baja prioridad **cada segundo**, abriendo y cerrando el archivo: si la consola se cuelga, en la
tarjeta queda todo menos el último segundo. Los errores se escriben al momento.

```
[    0.00] ===== Snake Puzzle PSP - devlog =====
[    0.00] Firmware: 6.61 (0x06061010)
[    0.00] CPU 333 MHz, bus 166 MHz | bateria 87% 31 C | cargador no
[    0.00] RAM: monton de C 23200 KB (lo que la consola le deja al juego para datos)
[    0.05] GE iniciado: VRAM pantallas 544 KB (16 bits, doble buffer, sin Z), 1504 KB libres para texturas
[    0.07] CARGA sfx.bin           587 KB, 14 efectos
[    0.09] AUDIO modulos: avcodec 0x00000000 mp3 0x00000000 | sceMp3InitResource 0x00000000
[    0.12] CARGA levels.bin        166 KB en   30 ms
[    0.31] CARGA world.spr         771 KB en  180 ms
[    0.40] Juego: 266 niveles, 29 aspectos, 14 fondos, 34 articulos de tienda | partida: nivel 1, ...
[    0.41] MUSICA MP3 .../data/music.mp3 (44100 Hz)
[    0.45] Arranque completo en 0.45 s | monton 3100/23200 KB usado | libre fuera del monton 900 KB
[    0.46] ESTADO - -> INICIO (nivel 0)
[    2.10] NIVEL 1 (indice 0) 9x6, 23 casillas, 0 objetos
[    5.00] STAT fps 60.0/59.6 | cpu 1.2/2.0 ms | ge 14.5 ms | draw 114 vert 456 tex 9 | heap ... | vram ... | INICIO nivel 1
[    5.00] PERFIL logica 0.05 ms | dibujo 1.10 ms (fondo 0.02, casillas 0.30, objetos 0.10, serpiente 0.20, ...)
[    6.80] SUPERADO nivel 1 en 5 movimientos (primera vez: +1 ficha)
```

| Tipo de línea | Qué dice |
|---|---|
| Cabecera | Firmware, frecuencias, batería, **RAM total que la consola le da al juego**, módulos de audio |
| `CARGA` | Cada archivo de `data/`, su tamaño y lo que tardó en leerse de la Memory Stick |
| `STAT` (cada 5 s) | FPS medios y mínimos, CPU media y máxima, espera al GE, dibujos, memoria, VRAM, lista del GE, pantalla y nivel, MHz y batería |
| `PERFIL` (cada 5 s) | En qué se va la CPU: lógica y dibujo (fondo, casillas, objetos, serpiente, efectos, menús, texto, interfaz) |
| `ESTADO`, `NIVEL`, `SUPERADO`, `PERDIDO`, `SALTO`, `TIENDA`, `DIARIO`, `PREMIO`, `CLASICO`, `MUSICA` | Qué estaba pasando: si el juego se cuelga, las últimas líneas dicen dónde |
| `TIRON` | Fotograma de más de 50 ms, con su CPU, su espera al GE, el tiempo "otros" y si coincidió con una carga |
| `AVISO` | Memoria por encima del 90 %, lista del GE por encima del 80 %, textura sin sitio en VRAM, batería caliente |
| `ERROR` | Archivo que falta, sin memoria, fallo de la música |
| `BLOQUEO`, `RECUPERADO` | El vigilante (abajo) |

### El vigilante (BLOQUEO)

Un hilo de prioridad alta comprueba cada medio segundo que el bucle principal sigue avanzando. Si lleva **3 s parado**
(10 s durante la carga inicial) escribe al momento en qué fase estaba cada hilo (bucle principal, música, mezclador de
efectos) y el estado del GE. Si después de un cuelgue no hay ninguna línea `BLOQUEO`, la consola entera se detuvo por
una excepción de la CPU.

### El capturador de excepciones (`crash.txt`)

La versión de pruebas lleva `exhandler.prx` (módulo de kernel, solo en CFW). Si la CPU lanza una excepción, en vez de
congelarse la consola se muestra una pantalla con el error y se escribe `crash.txt` (y lo mismo al final de
`devlog.txt`) con el tipo de excepción, la dirección que falló, el hilo, la fase de cada hilo, los registros y la pila.
Para traducirlo a función y línea hace falta el `snake.elf` de **la misma compilación** (`build_dist.sh` lo deja en
`dist/snake.elf`):

    tools/crash.sh crash.txt dist/snake.elf

### Diferencias entre PPSSPP y la consola

| En la consola real | En PPSSPP (por defecto) |
|---|---|
| Leer un `int` de una dirección no múltiplo de 4 cuelga la consola | Funciona |
| Leer una dirección inválida cuelga la consola | Se ignora |
| Un hilo de más prioridad puede interrumpir a otro en cualquier punto | Los hilos solo cambian en las llamadas al sistema |
| La FPU viene con las excepciones activadas (dividir por cero cuelga) | Se calcula infinito o NaN |

Por eso: todos los datos se leen byte a byte (`level.c`, `gfx.c`, `save.c`), todos los hilos llaman a
`fpu_no_exceptions()` al empezar y solo el hilo principal formatea decimales en el devlog.

## 3. ¿Qué datos del emulador valen para la consola?

| Dato | En PPSSPP | Por qué |
|---|---|---|
| RAM, VRAM, lista del GE, dibujos | **Exacto** | Son cuentas del propio juego |
| CPU ms | **Muy por lo bajo** | PPSSPP no imita la caché ni las esperas de memoria (en Subway era ~5 veces más en la consola) |
| Espera al GE, FPS | **No vale** | Solo se puede medir en la consola |

**Ajustes de PPSSPP para parecerse a la consola:** Sistema → Modelo PSP-1000 y CPU a 333 MHz; Gráficos →
renderizado por software, sin saltar fotogramas.

## 4. Optimizaciones

| Técnica | Qué ahorra |
|---|---|
| Atlas a 32 px por casilla (el original usa 100) con paleta de 256 colores y *swizzle* | Memoria y lectura de texturas del GE |
| Pantalla de 16 bits sin buffer Z | 544 KB de VRAM en vez de 1,3 MB: todo el atlas cabe en VRAM |
| Solo el aspecto y el fondo elegidos en memoria | 129 KB + 272 KB en vez de 3,7 MB + 3,8 MB |
| Fondos dibujados en tiras de 64 píxeles | Lectura de textura del GE (la caché de texturas es pequeña) |
| Música MP3 por el Media Engine | La CPU no decodifica audio |
| Lógica instantánea por casillas; el dibujo recorre "fotos" | Nada de física por fotograma |
| Texto: la fuente ya viene pre-escalada a los 4 tamaños | Texto nítido sin escalar en el GE |
