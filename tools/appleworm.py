"""appleworm.py - niveles de Apple Worm (assets/levels.json del APK) al formato de levels.bin del port

levels.json: 135 niveles de 16x16 en config[x][y] con la y hacia abajo. Codigos:
    1 cabeza, 2 cuerpo, 3 cola del gusano
    103 suelo, 105 portal de salida, 104 manzana, 101 pinchos, 102 piedra, 106 suelo (otra pieza), 107 sierra
steps: la solucion oficial con el mismo enum de direcciones que Snake Puzzle (0 arriba, 1 abajo, 2 izq., 3 der.).

    python3 tools/appleworm.py juego.xapk data/   ->  data/aw/ (niveles, graficos, interfaz y fondo)

Graficos (data/aw/aw.spr + aw.idx, orden fijo de src/aw.h):
    0..15  bloques de suelo de la skin Original, Block_<arriba><derecha><abajo><izquierda>f (1 = hay vecino)
    16..22 manzana, sierra, pinchos (delante y detras), piedra, portal (fondo y remolino)
    23..   interfaz de Apple Worm (seleccion de nivel y de skins) y la vista previa de cada skin
"""
import io
import json
import os
import struct
import sys
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from levels import O_APPLE, O_STONE, O_SPIKE, O_SAW   # noqa: E402

O_BODY = 9          # parte del cuerpo del gusano (a = posicion desde la cabeza)

# codigo de levels.json -> tipo de objeto (None: casilla de suelo)
CODE_OBJ = {104: O_APPLE, 102: O_STONE, 101: O_SPIKE, 107: O_SAW}
CODE_SOLID = {103, 106}


def read_json(src, name):
    """assets/<name> del .apk de Apple Worm (o del .xapk que lo contiene, o de una carpeta ya extraida)"""
    if os.path.isdir(src):
        p = os.path.join(src, 'assets', name)
        return open(p, 'rb').read()
    z = zipfile.ZipFile(src)
    names = z.namelist()
    if 'assets/' + name in names:
        return z.read('assets/' + name)
    for n in names:
        if n.endswith('.apk') and 'appleworm' in n:
            return zipfile.ZipFile(io.BytesIO(z.read(n))).read('assets/' + name)
    raise SystemExit('no encuentro assets/%s en %s' % (name, src))


def load_levels(src):
    raw = read_json(src, 'levels.json').decode('utf-8-sig')
    return json.loads(raw)['levels']


def neighbours(c):
    x, y = c
    return [(x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)]


def convert(lv):
    """un nivel de Apple Worm -> dict para level_blob (mismas claves que levels.extract_level)"""
    g = lv['config']
    W = len(g)
    H = len(g[0])
    cells = {}
    for x in range(W):
        for y in range(H):
            v = g[x][y]
            if v:
                cells[(x, H - 1 - y)] = v      # y hacia arriba, como en el port
    head = next(c for c, v in cells.items() if v == 1)
    tail = next(c for c, v in cells.items() if v == 3)
    body = [c for c, v in cells.items() if v == 2]
    # el cuerpo en orden desde la cabeza hasta la cola
    chain = [head]
    left = set(body)
    while left:
        nxt = [n for n in neighbours(chain[-1]) if n in left]
        if not nxt:
            break
        chain.append(nxt[0])
        left.discard(nxt[0])
    chain.append(tail)
    L = {'num': lv['id'], 'name': 'aw_%d' % lv['id'], 'solid': set(), 'layers': [], 'objs': [],
         'snake': {'cell': head, 'f': {k: 0 for k in ('isFallWhenCreate', 'isDownWhenCreate', 'isUpWhenCreate',
                                                       'isLeftWhenCreate', 'isSecondRightWhenCreate',
                                                       'isSecondDownWhenCreate', 'isSecondUpWhenCreate', 'isTypeL')}},
         'goal': None, 'hint': ''.join(str(s) for s in lv['steps']), 'coins': lv['coinsReward'],
         'skin_reward': lv['skinName'] if lv['skinReward'] else ''}
    L['snake']['f']['isFallWhenCreate'] = 1
    ground = []
    for c, v in cells.items():
        if v in CODE_SOLID:
            L['solid'].add(c)
            ground.append((c[0], c[1], 0, 0, 1.0))
        elif v == 105:
            L['goal'] = c
        elif v in CODE_OBJ:
            o = {'t': CODE_OBJ[v], 'cell': c}
            if v == 104:
                o['double'] = 0
            if v in (101, 107):
                o['rot'] = 0
            L['objs'].append(o)
    for i, c in enumerate(chain[1:], 1):
        L['objs'].append({'t': O_BODY, 'cell': c, 'idx': i})
    L['layers'].append({'name': 'Ground', 'order': 10, 'layer': 0, 'cells': ground, 'solid': True})
    return L


def level_blob_aw(L, tile_spr):
    """como extract.level_blob pero con las partes del cuerpo (O_BODY) y una sola casilla de suelo"""
    cells = [c for c in L['solid']] + [L['goal'], L['snake']['cell']] + [o['cell'] for o in L['objs']]
    x0 = min(c[0] for c in cells) - 1
    y0 = min(c[1] for c in cells) - 1
    x1 = max(c[0] for c in cells) + 1
    y1 = max(c[1] for c in cells) + 1
    W, H = x1 - x0 + 1, y1 - y0 + 1
    flags = 1   # SF_FALL: el gusano cae al empezar si no tiene suelo
    out = bytearray(struct.pack('<HhhBBBBbbbb', 1000 + L['num'], x0, y0, W, H, flags, 0,
                                L['snake']['cell'][0] - x0, L['snake']['cell'][1] - y0,
                                L['goal'][0] - x0, L['goal'][1] - y0))
    solid = bytearray(W * H)
    for (x, y) in L['solid']:
        solid[(y - y0) * W + (x - x0)] = 1
    out += solid
    tiles = [(x - x0, y - y0, 0, 8, tile_spr) for (x, y) in sorted(L['solid'])]
    out += struct.pack('<H', len(tiles))
    for t in tiles:
        out += struct.pack('<BBBBH', *t)
    out += struct.pack('<H', len(L['objs']))
    for o in L['objs']:
        a = o.get('idx', 0) if o['t'] == O_BODY else o.get('double', 0) if o['t'] == O_APPLE else 0
        out += struct.pack('<BbbBbBH', o['t'], o['cell'][0] - x0, o['cell'][1] - y0, a, 0, 0, 0)
        out += struct.pack('<hh', 0, 0)
    hint = L['hint'].encode()
    out += struct.pack('<H', len(hint)) + hint
    return bytes(out)


def write_pack(levels, path, tile_spr=0):
    blobs = [level_blob_aw(L, tile_spr) for L in levels]
    offs, pos = [], 0
    for b in blobs:
        offs.append(pos)
        pos += len(b)
    head = b'SNL1' + struct.pack('<I', len(blobs)) + b''.join(struct.pack('<I', 8 + 4 * len(blobs) + o) for o in offs)
    with open(path, 'wb') as f:
        f.write(head + b''.join(blobs))


# ---------------------------------------------------------------- graficos e interfaz
UI = ['window_lev_sel', 'btn_lev_norm', 'btn_lev_press', 'place_level', 'place_level_with_skin', 'star_left',
      'star_centre', 'star_right', 'star_left_gray', 'star_centre_gray', 'star_right_gray', 'icon_lock',
      'arrow_left_norm', 'arrow_right_norm', 'btn_back_norm', 'shop_window', 'skins_norm', 'corner_lev_skin',
      'icon_check', 'icon_coin_big', 'btn_green_norm', 'btn_gr_big_norm']
ELEMENTS = ['objects/apple', 'objects/saw', 'objects/spikes_front', 'objects/spikes_back', 'objects/stone_all',
            'portal/portal_bg', 'portal/portal_swirl']
CELL_PX = 314        # pixeles de una casilla en los graficos de Apple Worm (cara delantera de Block_1111f)


def spine_regions(atlas_text, tex_w=None):
    """regiones de un .atlas de Spine (formato con xy/size): nombre -> (x, y, w, h, rotada). Unity reduce la textura
    (maximo 2048): si tex_w no coincide con el 'size' de la pagina, las coordenadas se escalan"""
    out, cur, page_w = {}, None, None
    for line in atlas_text.splitlines():
        if line.startswith('size:') and page_w is None:
            page_w = int(line.split(':', 1)[1].split(',')[0])
    sc = tex_w / page_w if tex_w and page_w else 1.0
    for line in atlas_text.splitlines():
        if not line.strip():
            continue
        if not line.startswith(' ') and ':' not in line:
            cur = line.strip()
            out[cur] = {}
            continue
        if cur and ':' in line:
            k, v = line.strip().split(':', 1)
            out[cur][k.strip()] = v.strip()
    reg = {}
    for n, d in out.items():
        if 'xy' not in d:
            continue
        x, y = (int(t) for t in d['xy'].split(','))
        w, h = (int(t) for t in d['size'].split(','))
        rot = d.get('rotate', 'false') not in ('false', '0')
        reg[n] = (round(x * sc), round(y * sc), round(w * sc), round(h * sc), rot)
    return reg


def region_image(tex, r):
    x, y, w, h, rot = r
    if rot:   # Spine guarda las regiones giradas 90 grados a la derecha
        return tex.crop((x, y, x + h, y + w)).rotate(90, expand=True)
    return tex.crop((x, y, x + w, y + h))


def export_assets(src, out):
    """Graficos de Apple Worm con las herramientas del extractor de Snake Puzzle"""
    import UnityPy
    from PIL import Image
    from unityload import unpack
    import extract as ex
    data_dir, _ = unpack(src)
    env = UnityPy.load(data_dir)
    files = {}
    for k, f in env.files.items():
        if getattr(f, 'objects', None):
            files[k.split('/')[-1]] = f
        sub = getattr(f, 'files', None)
        if isinstance(sub, dict):
            for k2, f2 in sub.items():
                if getattr(f2, 'objects', None):
                    files[str(k2).split('/')[-1]] = f2

    def resolve(af, ptr):
        fid, pid = ptr['m_FileID'], ptr['m_PathID']
        if not pid:
            return None
        tgt = af if fid == 0 else files.get(af.externals[fid - 1].path.split('/')[-1])
        return tgt.objects.get(pid) if tgt else None
    paths = {}
    for o in files['globalgamemanagers'].objects.values():
        if o.type.name == 'ResourceManager':
            for p, ptr in o.read_typetree()['m_Container']:
                ob = resolve(o.assets_file, ptr)
                if ob is not None:
                    paths.setdefault(p, []).append(ob)

    def get(path, typ):
        for o in paths.get(path, []):
            if o.type.name == typ:
                return o
        return None
    sheet = ex.SpriteSheet(ex.FMT_T8)
    idx = []
    # bloques de la skin Original (SpriteAtlas packoriginal)
    sa_o = get('packs/original/platforms/packoriginal', 'SpriteAtlas')
    sa = sa_o.read_typetree()
    blocks = {}
    for n, ptr in zip(sa['m_PackedSpriteNamesToIndex'], sa['m_PackedSprites']):
        blocks[n] = resolve(sa_o.assets_file, ptr)
    for m in range(16):
        o = blocks.get('Block_%sf' % format(m, '04b'))
        idx.append(sheet.add_unity(o) if o is not None else -1)
    # elementos (atlas de Spine de la skin Original)
    tex = get('packs/original/elements_export/skeleton', 'Texture2D').read().image.convert('RGBA')
    at = get('packs/original/elements_export/skeleton.atlas', 'TextAsset').read().m_Script
    reg = spine_regions(at.decode('utf8', 'replace') if isinstance(at, bytes) else at, tex.width)
    at_s = at.decode('utf8', 'replace') if isinstance(at, bytes) else at
    page_w = next((int(l.split(':', 1)[1].split(',')[0]) for l in at_s.splitlines() if l.startswith('size:')), tex.width)
    scale_k = tex.width / page_w   # misma reduccion que la textura
    for n in ELEMENTS:
        if n not in reg:
            idx.append(-1)
            continue
        im = region_image(tex, reg[n])
        w, h = im.size
        cpx = CELL_PX * scale_k     # pixeles de una casilla en la textura ya reducida
        small = im.resize((max(1, int(w * ex.PX / cpx)), max(1, int(h * ex.PX / cpx))), Image.LANCZOS)
        idx.append(sheet.add_image(small, -w / cpx / 2, -h / cpx / 2, w / cpx, h / cpx))
    # fondo
    if 'bg/bg_classic' in reg:
        ex.write_bg(region_image(tex, reg['bg/bg_classic']), os.path.join(out, 'aw_bg.tex'))
    # interfaz (sharedassets0) y vistas previas de las skins
    ui = {}
    for f in files.values():
        for o in f.objects.values():
            if o.type.name == 'Sprite':
                try:
                    n = o.peek_name()
                except Exception:
                    continue
                if n in UI and n not in ui:
                    ui[n] = o
    for n in UI:
        idx.append(sheet.add_unity(ui[n], scale=48, maxpx=200) if n in ui else -1)
    skins = load_skins(src)
    for sk in skins:
        o = get('packs/' + sk['previewPath'].lower(), 'Sprite')
        idx.append(sheet.add_unity(o, scale=48, maxpx=96) if o is not None else -1)
    size = sheet.write(os.path.join(out, 'aw.spr'))
    with open(os.path.join(out, 'aw.idx'), 'wb') as f:
        f.write(struct.pack('<H', len(idx)) + struct.pack('<%dh' % len(idx), *idx))
    # skins: nombre, como se consigue (0 gratis, 1 monedas, 3 compra, 4 completar niveles) y cantidad
    with open(os.path.join(out, 'aw_skins.bin'), 'wb') as f:
        f.write(struct.pack('<H', len(skins)))
        for sk in skins:
            nm = sk['name'].encode()[:15]
            f.write(struct.pack('<B', len(nm)) + nm + struct.pack('<BH', sk['typeOfGetting'], sk['countForGet']))
    print('  Apple Worm: %d graficos (%d KB), %d skins' % (len(sheet.items), size // 1024, len(skins)))


def load_skins(src):
    raw = read_json(src, 'skins.json').decode('utf-8-sig')
    raw = '\n'.join(l for l in raw.splitlines() if not l.strip().startswith('//'))
    return json.loads(raw)['skins']


def export(src, out):
    os.makedirs(out, exist_ok=True)
    lv = [convert(x) for x in load_levels(src)]
    write_pack(lv, os.path.join(out, 'aw_levels.bin'))
    print('  Apple Worm: %d niveles' % len(lv))
    export_assets(src, out)


if __name__ == '__main__':
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)
    export(sys.argv[1], os.path.join(sys.argv[2], 'aw'))
