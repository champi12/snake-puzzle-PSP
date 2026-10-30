# Cómo se descompiló Snake Puzzle

Snake Puzzle 2.3.4 está hecho con **Unity 2022.3.67f2** y compilado con **IL2CPP**: no hay `Assembly-CSharp.dll`
(.NET) que se pueda descompilar a C#. El C# se convirtió a C++ y se compiló a código máquina ARM64
(`lib/arm64-v8a/libil2cpp.so`). Los nombres de clases, campos y métodos siguen en `global-metadata.dat`.

## Herramientas (tools/decompile/)

* `il2cpp.py`: lector de `global-metadata.dat` (versión 31) y de `libil2cpp.so`. Saca cada clase con sus campos y la
  **dirección de cada método** en el binario (lo mismo que Il2CppDumper). Aplica las relocaciones del ELF para
  seguir los punteros de `Il2CppCodeGenModule`.
* `il2dis.py`: desensamblador ARM64 (capstone) **anotado**: cada llamada sale con el nombre del método
  (`SnakeController::Grow`), cada acceso a `this` con el nombre del campo (`.isMoving`), y cada literal con su valor
  (`STR "Apple"`, `TypeInfo GameLogicController`). Con `FULL=1` saca todas las instrucciones; sin él, solo llamadas,
  campos, cadenas y saltos (se lee casi como pseudocódigo).

```
IL2CPP_METADATA=.../global-metadata.dat IL2CPP_SO=.../libil2cpp.so python3 tools/decompile/il2cpp.py Assembly-CSharp.dll
IL2CPP_METADATA=... IL2CPP_SO=... python3 tools/decompile/il2dis.py 'SnakeController::HandleMovement'
```

Los MonoBehaviour de los niveles se guardan **sin typetree**; `tools/unityload.py` genera la estructura de cada
script a partir de `libil2cpp.so` + `global-metadata.dat` (TypeTreeGeneratorAPI), igual que hace Unity al cargar.

## Lo que dice el código

### Movimiento (`SnakeController.HandleMovement`)

Un `Physics2D.RaycastAll` de 1 casilla desde la cabeza en la dirección pulsada; según la etiqueta de lo primero que
toca (que no sea la propia serpiente):

| Choca con | Qué hace |
|---|---|
| Capa `SnakeBody` con el sprite visible | Bloqueado (sonido `stop`, cara de atascado) |
| `Apple` | `AnimEating`, `GameLogicController.EatApple`, `Grow(isDouble)` |
| `Stone` | `Stone.TryPush`; si no, `TryTeleportStone`; si no, "Detect Stone blocked!" |
| `Wall` | Bloqueado |
| `Goal` / `Mask` | Entra y `StartAutoMove` hacia el portal → `StopAutoMove` → `GameManager.LevelComplete` |
| `Kill` / `Saw` | Entra, `IsDeath`, sonido `thorn`, cara `head_die`, `GameOver` a los 0,5 s |
| Nada | Se mueve y `CheckForSnakeFall` |

La meta está siempre abierta: `EatApple` solo cuenta manzanas (no hace falta comérselas todas).

### Caída (`CheckForSnakeFall`)

Para cada parte, `Physics2D.OverlapCircle(posición + Vector3.down * gridSize, gridSize * 0.2, groundLayer)` con
`groundLayer = 64` (capa `Ground`): el suelo, las manzanas, las piedras, la meta y los portales están en esa capa. Si
ninguna parte toca nada (salvo objetos con etiqueta `Kill`), toda la serpiente baja una casilla
(`DOMove`, `fallDuration` 0,1 s) y se vuelve a comprobar. Si cae fuera del mapa, `FallTimeoutCoroutine` hace
`GameOver("fall_off")`.

### Posiciones

`SnapGrid.OnEnable` y `SnakeController.SnapToGrid`:
`CellToWorld(WorldToCell(p)) + (cellSize.x / 3, cellSize.y / 1.5)` con celda de 1×1: la casilla de un objeto es
`floor(p)`. Las `Tilemap` con `TilemapCollider2D` (capa `Ground`) son suelo; las demás, decorado.

### Serpiente inicial (`OnEnable` → `Initialize(pos, Vector2.right, 3)`)

Largo 3 mirando a la derecha con el cuerpo a la izquierda; `isDownWhenCreate` (cabeza arriba, cuerpo debajo),
`isUpWhenCreate`, `isLeftWhenCreate` y `isSecondRight/Down/UpWhenCreate` (la tercera parte en L).

### Portales (`TeleportGate.GetDirection`)

Por el giro del objeto: 0° entra hacia la derecha, 90° hacia arriba, 180° hacia la izquierda, 270° hacia abajo. La
cabeza ocupa la casilla del portal de entrada; el siguiente paso la saca por la pareja.

### Pistas (`ToolFileHandler.EncryptDecrypt`)

`hint/level_N` es un texto XOR con la clave estática **"appleworm"** (del constructor estático de
`ToolFileHandler`); dentro hay una cadena JSON de dígitos: 0 arriba, 1 abajo, 2 izquierda, 3 derecha
(`ToolHint.AppendMovement`). Algunos archivos llevan BOM, que Unity quita antes de descifrar.

### Modo clásico (`ClassicGamePlay`)

Tablero de `p0 (-7, -6)` a `p1 (5, 5)`, `IsLoop` + clones (los bordes se cruzan), `IsDie` al morderse, `CreateApple`
en una casilla libre al azar. Ranking: `ClassicLeaderboardData` (10 nombres, `minHighScore` 120, `maxHighScore` 130,
`lowerRankScoreDiff` 8, `firstPlaceHintReward` 3, `hintRewardForTop2To10` 2).

## Comprobación

Las 266 pistas son soluciones grabadas por los diseñadores con el juego real. `tests/host_test.c` las juega con
`src/logic.c`: hoy coinciden **247**. Las reglas que salieron de esa comparación (y que el código confirma):

* Las manzanas `isDouble` dan 2 partes.
* Pisar un pincho por arriba no mata ni deja caer: la caja del pincho (0,31 × 0,51) solo ocupa la mitad baja de su casilla.
* Las piedras entran en los portales (`TryTeleportStone`) y salen por la pareja.

Los 19 que faltan (`make -C tests check` los lista) son sobre todo de piedras cayendo a la vez que la serpiente
(en el original las piedras usan `Rigidbody2D` con gravedad real y se comprueban en `FixedUpdate`).
