"""Niveles de Snake Puzzle: los 275 prefabs Resources/level/level_N convertidos a una cuadricula.

Reglas de posicion sacadas del codigo original (libil2cpp.so):
  * GridLayout.WorldToCell + CellToWorld con celda de 1x1: la celda de un objeto es floor(posicion).
  * SnapGrid.OnEnable y SnakeController.SnapToGrid dejan el objeto en floor(p) + (cellSize.x/3, cellSize.y/1.5).
  * Las Tilemap con TilemapCollider2D son suelo/pared (capa Ground); las demas son solo decorado.
"""
import math
import re

# tipos de objeto (deben coincidir con src/level.h)
O_APPLE, O_STONE, O_SPIKE, O_SAW, O_GATE, O_SWITCH, O_BARRIER, O_TUT = range(1, 9)


class Sprites:
    """Registro de sprites usados (se exportan despues como imagenes)."""
    def __init__(self):
        self.lst = []
        self.idx = {}

    def add(self, obj, group):
        if obj is None:
            return -1
        k = (obj.assets_file.name, obj.path_id)
        if k not in self.idx:
            self.idx[k] = len(self.lst)
            self.lst.append((obj, group))
        return self.idx[k]


def mat_mul(a, b):
    return (a[0] * b[0] + a[1] * b[3], a[0] * b[1] + a[1] * b[4], a[0] * b[2] + a[1] * b[5] + a[2],
            a[3] * b[0] + a[4] * b[3], a[3] * b[1] + a[4] * b[4], a[3] * b[2] + a[4] * b[5] + a[5])


def local_mat(tr):
    p, q, s = tr['m_LocalPosition'], tr['m_LocalRotation'], tr['m_LocalScale']
    ang = 2 * math.atan2(q['z'], q['w'])
    c, si = math.cos(ang), math.sin(ang)
    return (c * s['x'], -si * s['y'], p['x'], si * s['x'], c * s['y'], p['y'])


def rot_deg(m):
    return round(math.degrees(math.atan2(m[3], m[0]))) % 360


# origen del Grid del nivel: GridLayout.WorldToCell/CellToWorld cuentan las casillas desde ahi (en algunos niveles
# esta desplazado media casilla o mas: nivel 18 en y 0,5, nivel 20 en (0,6; 0,65))
GRID_O = (0.0, 0.0)


def cell(x, y):
    return (int(math.floor(x - GRID_O[0] + 1e-4)), int(math.floor(y - GRID_O[1] + 1e-4)))


def walk(g, parent, out):
    m = mat_mul(parent, local_mat(g['tr']))
    if 'SnapGrid' in g['mb'] or 'SnakeController' in g['mb']:
        cx, cy = cell(m[2], m[5])
        m = (m[0], m[1], GRID_O[0] + cx + 1 / 3, m[3], m[4], GRID_O[1] + cy + 2 / 3)
    g['m'] = m
    out.append(g)
    for ch in g['children']:
        walk(ch, m, out)


def box_center(g):
    m = g['m']
    b = g['comp'].get('BoxCollider2D')
    ox, oy = (b['m_Offset']['x'], b['m_Offset']['y']) if b else (0, 0)
    return m[0] * ox + m[1] * oy + m[2], m[3] * ox + m[4] * oy + m[5]


def pos_cell(g):
    """Apple/Stone/Trap.Start: transform.position = SnapToGrid(transform.position) (GridLayout.WorldToCell: suelo)"""
    return cell(g['m'][2], g['m'][5])


def sprite_of(game, g):
    r = g['comp'].get('SpriteRenderer')
    if not r or not r['m_Sprite']['m_PathID']:
        return None
    return game.resolve(g['obj'].assets_file, r['m_Sprite'])


def level_index(name):
    m = re.match(r'level/level_(\d+)$', name)
    return int(m.group(1)) if m else None


def extract_level(game, root_obj, sprites, num):
    global GRID_O
    root = game.go(root_obj)
    # primero sin ajustar, para encontrar el Grid; luego otra vez con su origen
    GRID_O = (0.0, 0.0)
    nodes = []
    walk(root, (1, 0, 0, 0, 1, 0), nodes)
    grids = [g for g in nodes if 'Grid' in g['comp']]
    if grids:
        GRID_O = (grids[0]['m'][2], grids[0]['m'][5])
        nodes = []
        walk(root, (1, 0, 0, 0, 1, 0), nodes)
    L = {'num': num, 'name': root['name'], 'solid': set(), 'layers': [], 'objs': [], 'snake': None, 'goal': None}
    gates = {}
    barriers = {}
    for g in nodes:
        if not g['active']:
            continue
        tm = g['comp'].get('Tilemap')
        if tm is not None and g['comp'].get('TilemapRenderer', {}).get('m_Enabled', 1):
            tr_ = g['comp'].get('TilemapRenderer', {})
            sprs = tm['m_TileSpriteArray']
            m = g['m']
            cells = []
            for pos, t in tm['m_Tiles']:
                si = t['m_TileSpriteIndex']
                if si >= len(sprs) or not sprs[si]['m_Data']['m_PathID']:
                    continue
                so = game.resolve(g['obj'].assets_file, sprs[si]['m_Data'])
                wx = m[0] * (pos['x'] + 0.5) + m[1] * (pos['y'] + 0.5) + m[2]
                wy = m[3] * (pos['x'] + 0.5) + m[4] * (pos['y'] + 0.5) + m[5]
                c = cell(wx, wy)
                # matriz de la casilla (espejos/giros del Tile Palette)
                mx = tm['m_TileMatrixArray'][t['m_TileMatrixIndex']]['m_Data'] if t['m_TileMatrixIndex'] < len(tm['m_TileMatrixArray']) else None
                fl = 0
                if mx:
                    if mx['e00'] < 0: fl |= 1
                    if mx['e11'] < 0: fl |= 2
                    if abs(mx['e01']) > 0.5: fl |= 4
                col = tm['m_TileColorArray'][t['m_TileColorIndex']]['m_Data'] if t['m_TileColorIndex'] < len(tm['m_TileColorArray']) else None
                a = col['a'] if col else 1.0
                cells.append((c[0], c[1], sprites.add(so, 'tile'), fl, a))
                if 'TilemapCollider2D' in g['comp']:
                    L['solid'].add(c)
            L['layers'].append({'name': g['name'], 'order': tr_.get('m_SortingOrder', 0),
                                'layer': tr_.get('m_SortingLayerID', 0), 'cells': cells,
                                'solid': 'TilemapCollider2D' in g['comp']})
        mb = g['mb']
        if 'SnakeController' in mb and mb['SnakeController']:
            s = mb['SnakeController']
            if s.get('isClassic') or s.get('isClone'):
                continue
            L['snake'] = {'cell': cell(*box_center(g)), 'f': {k: int(s.get(k, 0)) for k in (
                'isFallWhenCreate', 'isDownWhenCreate', 'isUpWhenCreate', 'isLeftWhenCreate',
                'isSecondRightWhenCreate', 'isSecondDownWhenCreate', 'isSecondUpWhenCreate', 'isTypeL')}}
        if g['tag'] == 'Goal' and 'Portal' in mb:
            L['goal'] = cell(*box_center(g))
        if 'Apple' in mb and mb['Apple'] is not None:
            a = mb['Apple']
            L['objs'].append({'t': O_APPLE, 'cell': pos_cell(g), 'double': int(a.get('isDouble', 0))})
        if 'Stone' in mb and mb['Stone'] is not None:
            spr = None
            for ch in g['children']:
                if ch['name'] == 'Stone':
                    spr = sprite_of(game, ch)
            L['objs'].append({'t': O_STONE, 'cell': pos_cell(g), 'spr': sprites.add(spr, 'obj') if spr else -1})
        if 'Trap' in mb:
            kind = O_SAW if 'Saw' in g['name'] else O_SPIKE
            L['objs'].append({'t': kind, 'cell': pos_cell(g), 'rot': rot_deg(g['m'])})
        if 'TeleportGate' in mb and mb['TeleportGate'] is not None:
            tg = mb['TeleportGate']
            gates[g['obj'].path_id] = len(L['objs'])
            L['objs'].append({'t': O_GATE, 'cell': cell(*box_center(g)), 'rot': rot_deg(g['m']),
                              'partner_pid': tg['partner']['m_PathID']})
        if 'Barrie' in mb and mb['Barrie'] is not None:
            b = mb['Barrie']
            barriers[g['obj'].path_id] = len(L['objs'])
            bar = game.resolve(g['obj'].assets_file, b['barrie']) if b.get('barrie') else None
            L['objs'].append({'t': O_BARRIER, 'cell': cell(*box_center(g)), 'dir': int(b.get('direction', 0)),
                              'rot': rot_deg(g['m']), 'bar_pid': bar.read_typetree()['m_GameObject']['m_PathID'] if bar else 0})
        if 'Switch' in mb and mb['Switch'] is not None:
            sw = mb['Switch']
            L['objs'].append({'t': O_SWITCH, 'cell': cell(*box_center(g)), 'barrie_pid': sw['barrie']['m_PathID']})
        if g['name'].startswith('Tut') and 'SpriteRenderer' in g['comp']:
            spr = sprite_of(game, g)
            if spr is not None:
                m = g['m']
                L['objs'].append({'t': O_TUT, 'pos': (m[2] - GRID_O[0], m[5] - GRID_O[1]), 'spr': sprites.add(spr, 'tut')})
    # parejas de teletransporte: 'partner' apunta al componente TeleportGate del otro
    comp2go = {}
    for g in nodes:
        for c in g['obj'].read_typetree()['m_Component']:
            comp2go[c['component']['m_PathID']] = g['obj'].path_id
    bypid = {g['obj'].path_id: g for g in nodes}
    for o in L['objs']:
        if o['t'] == O_BARRIER:
            bg = bypid.get(o.pop('bar_pid'))
            hx, hy = GRID_O[0] + o['cell'][0] + 1 / 3, GRID_O[1] + o['cell'][1] + 2 / 3
            if bg is not None:
                dx, dy = bg['m'][2] - hx, bg['m'][5] - hy
                o['bdir'] = (3 if dx > 0 else 2) if abs(dx) > abs(dy) else (0 if dy > 0 else 1)
            else:
                o['bdir'] = 0
        if o['t'] == O_GATE:
            o['partner'] = gates.get(comp2go.get(o.pop('partner_pid'), -1), -1)
        if o['t'] == O_SWITCH:
            o['barrier'] = barriers.get(comp2go.get(o.pop('barrie_pid'), -1), -1)
    return L
