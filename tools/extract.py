#!/usr/bin/env python3
"""Convierte el XAPK de Snake Puzzle 2.3.4 (Android, Unity 2022.3 IL2CPP) en los datos del port de PSP.

Uso:
    python3 tools/extract.py snake-puzzle-2-3-4.xapk data/

Tambien acepta el APK base con config.arm64_v8a.apk en la misma carpeta (hace falta libil2cpp.so para leer los
scripts) o una carpeta con el XAPK ya descomprimido.

Genera en data/:
    levels.bin   los niveles (cuadricula, casillas de decorado, objetos y la solucion oficial de cada nivel)
    world.spr    atlas de casillas, objetos e interfaz (paleta de 256 colores, maximo 512x512)
    skinNN.spr   cada aspecto de la serpiente (cuerpo, cabeza con sus animaciones y la manzana a juego)
    bgNN.tex     fondos a 480x272 en 16 bits
    font.spr     la fuente del juego (Lilita One) a los tamanos de la interfaz
    sfx.bin      efectos de sonido (PCM 16 bits 22050 Hz mono)
    music.mp3    musica (MP3 44,1 kHz: la decodifica el Media Engine de la PSP); music_lunar.mp3 la del
                 Ano Nuevo Lunar
    shop.spr     iconos de la tienda y dibujos del progreso de aspectos
    meta.bin     tienda, progreso de aspectos, recompensa diaria, ranking del modo clasico y top de jugadores
    ICON0.PNG, PIC1.PNG   icono y fondo del menu de la PSP

Nada de esto se guarda en el repositorio: cada uno lo genera con su propia copia del juego.
"""
import io
import json
import math
import os
import struct
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from unityload import Game  # noqa: E402
from levels import Sprites, extract_level, level_index, O_APPLE, O_STONE, O_SPIKE, O_SAW, O_GATE, O_SWITCH, \
    O_BARRIER, O_TUT  # noqa: E402

PX = 32              # pixeles por casilla en los atlas (en pantalla una casilla mide 16-34 px)
HINT_KEY = 'appleworm'   # clave XOR de ToolFileHandler.EncryptDecrypt (constructor estatico)
FMT_T8, FMT_5650, FMT_4444, FMT_8888 = 0, 1, 2, 3


# ---------------------------------------------------------------- imagenes
def sprite_full(obj):
    """Imagen del sprite con su rectangulo completo (Unity recorta el borde transparente al empaquetar)."""
    sp = obj.read()
    t = obj.read_typetree()
    im = sp.image.convert('RGBA')
    rw, rh = int(round(t['m_Rect']['width'])), int(round(t['m_Rect']['height']))
    off = t['m_RD']['textureRectOffset']
    full = Image.new('RGBA', (max(rw, 1), max(rh, 1)), (0, 0, 0, 0))
    full.paste(im, (int(round(off['x'])), rh - int(round(off['y'])) - im.height))
    return full, t['m_Pivot'], t['m_PixelsToUnits'] or 100.0


class SpriteSheet:
    """Hojas de sprites en el formato de src/gfx.h (ver write)."""
    def __init__(self, fmt=FMT_T8, maxw=512, maxh=512):
        self.items = []          # (imagen, ox, oy, w, h en unidades)
        self.fmt = fmt
        self.maxw, self.maxh = maxw, maxh

    def add_image(self, im, ox, oy, uw, uh):
        """ox, oy: esquina superior izquierda respecto al punto de anclaje, en casillas (y hacia abajo)."""
        self.items.append((im, ox, oy, uw, uh))
        return len(self.items) - 1

    def add_unity(self, obj, scale=PX, maxpx=None):
        im, pv, ppu = sprite_full(obj)
        uw, uh = im.width / ppu, im.height / ppu
        bb = im.getbbox() or (0, 0, 1, 1)
        crop = im.crop(bb)
        # tamano en el atlas: 'scale' pixeles por unidad de Unity
        tw = max(1, int(round(crop.width / ppu * scale)))
        th = max(1, int(round(crop.height / ppu * scale)))
        if maxpx and max(tw, th) > maxpx:
            f = maxpx / max(tw, th)
            tw, th = max(1, int(tw * f)), max(1, int(th * f))
        small = crop.resize((tw, th), Image.LANCZOS)
        ox = (bb[0] - pv['x'] * im.width) / ppu
        oy = (bb[1] - (1 - pv['y']) * im.height) / ppu
        return self.add_image(small, ox, oy, crop.width / ppu, crop.height / ppu)

    def pack(self):
        order = sorted(range(len(self.items)), key=lambda i: -self.items[i][0].height)
        pages, place = [], {}
        cur = None
        for i in order:
            im = self.items[i][0]
            w, h = im.width + 2, im.height + 2
            if w > self.maxw or h > self.maxh:
                raise SystemExit('sprite demasiado grande para el atlas')
            while True:
                if cur is None:
                    cur = {'x': 0, 'y': 0, 'rowh': 0, 'w': 0, 'h': 0, 'items': []}
                    pages.append(cur)
                if cur['x'] + w > self.maxw:
                    cur['x'] = 0
                    cur['y'] += cur['rowh']
                    cur['rowh'] = 0
                if cur['y'] + h > self.maxh:
                    cur = None
                    continue
                place[i] = (len(pages) - 1, cur['x'] + 1, cur['y'] + 1)
                cur['items'].append(i)
                cur['x'] += w
                cur['rowh'] = max(cur['rowh'], h)
                cur['w'] = max(cur['w'], cur['x'])
                cur['h'] = max(cur['h'], cur['y'] + cur['rowh'])
                break
        return pages, place

    def write(self, path):
        pages, place = self.pack()
        blobs = []
        for pg in pages:
            W = 1 << max(3, (pg['w'] - 1).bit_length())
            H = 1 << max(3, (pg['h'] - 1).bit_length())
            sheet = Image.new('RGBA', (W, H), (0, 0, 0, 0))
            for i in pg['items']:
                im = self.items[i][0]
                _, x, y = place[i]
                sheet.paste(im, (x, y))
                # borde de 1 pixel repetido: el filtro bilineal no mezcla con el vecino
                sheet.paste(im.crop((0, 0, im.width, 1)), (x, y - 1))
                sheet.paste(im.crop((0, im.height - 1, im.width, im.height)), (x, y + im.height))
                sheet.paste(sheet.crop((x, y - 1, x + 1, y + im.height + 1)), (x - 1, y - 1))
                sheet.paste(sheet.crop((x + im.width - 1, y - 1, x + im.width, y + im.height + 1)), (x + im.width, y - 1))
            blobs.append(encode_texture(sheet, self.fmt))
        out = bytearray(b'SPR1' + struct.pack('<HH', len(pages), len(self.items)))
        for (W, H, fmt, data) in blobs:
            out += struct.pack('<HHBBHI', W, H, fmt, 0, 0, len(data))
        for i, (im, ox, oy, uw, uh) in enumerate(self.items):
            p, x, y = place[i]
            out += struct.pack('<HHHHHhhhh', p, x, y, im.width, im.height,
                               int(round(ox * 256)), int(round(oy * 256)), int(round(uw * 256)), int(round(uh * 256)))
        while len(out) % 16:
            out += b'\0'
        for (_, _, _, data) in blobs:
            out += data
            while len(out) % 16:
                out += b'\0'
        with open(path, 'wb') as f:
            f.write(out)
        return sum(len(b[3]) for b in blobs)


def swizzle(data, width_bytes, height):
    """Orden de bloques de 16x8 bytes del GE: lectura de texturas mucho mas rapida."""
    a = np.frombuffer(data, np.uint8).reshape(height, width_bytes)
    bw, bh = width_bytes // 16, height // 8
    a = a.reshape(bh, 8, bw, 16).transpose(0, 2, 1, 3)
    return a.tobytes()


def encode_texture(img, fmt):
    W, H = img.size
    if fmt == FMT_T8:
        # paleta de 256 colores con alfa (las hojas son dibujos planos: se nota muy poco)
        a = np.asarray(img)
        q = img.quantize(256, method=Image.Quantize.FASTOCTREE, dither=Image.Dither.NONE)
        pal = q.getpalette('RGBA') if hasattr(q, 'getpalette') else None
        try:
            pal = q.getpalette('RGBA')
        except TypeError:
            pal = None
        if not pal or len(pal) < 4:
            raw = q.palette.tobytes()
            pal = list(raw)
        pal = (list(pal) + [0] * 1024)[:1024]
        idx = np.asarray(q, np.uint8)
        # los pixeles casi transparentes van al indice de transparencia
        tr = a[:, :, 3] < 8
        if tr.any():
            pal_arr = np.array(pal, np.uint8).reshape(256, 4)
            ti = int(np.argmin(pal_arr[:, 3]))
            pal_arr[ti] = (0, 0, 0, 0)
            idx = idx.copy()
            idx[tr] = ti
            pal = pal_arr.flatten().tolist()
        data = swizzle(idx.tobytes(), W, H) if W >= 16 and H >= 8 else idx.tobytes()
        return (W, H, FMT_T8 | (0x80 if W >= 16 and H >= 8 else 0), bytes(pal) + data)
    a = np.asarray(img.convert('RGBA')).astype(np.uint16)
    if fmt == FMT_4444:
        v = (a[:, :, 0] >> 4) | ((a[:, :, 1] >> 4) << 4) | ((a[:, :, 2] >> 4) << 8) | ((a[:, :, 3] >> 4) << 12)
    else:
        # 5650 con difuminado ordenado: sin bandas en los degradados del cielo
        bayer = np.array([[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]], np.int32)
        d = np.tile(bayer, (H // 4 + 1, W // 4 + 1))[:H, :W]
        r = np.clip(a[:, :, 0] + (d >> 1) - 4, 0, 255) >> 3
        g = np.clip(a[:, :, 1] + (d >> 2) - 2, 0, 255) >> 2
        b = np.clip(a[:, :, 2] + (d >> 1) - 4, 0, 255) >> 3
        v = r | (g << 5) | (b << 11)
    data = v.astype('<u2').tobytes()
    sw = W * 2 >= 16 and H >= 8
    return (W, H, fmt | (0x80 if sw else 0), swizzle(data, W * 2, H) if sw else data)


# ---------------------------------------------------------------- niveles
def decode_hint(s):
    if isinstance(s, bytes):
        s = s.decode('utf-8', 'replace')
    if s.startswith('\ufeff'):           # algunos .txt llevan BOM: Unity lo quita al leer TextAsset.text
        s = s[1:]
    raw = ''.join(chr(ord(c) ^ ord(HINT_KEY[i % len(HINT_KEY)])) for i, c in enumerate(s))
    try:
        v = json.loads(raw)
    except Exception:
        v = raw.strip('"')
    return ''.join(ch for ch in str(v) if ch in '0123')


WORLD = None


def export_levels(G, out, world):
    global WORLD
    WORLD = world
    sprites = Sprites()
    levels = []
    for path in G.container:
        n = level_index(path)
        if n is None:
            continue
        # la 14.2.9 trae versiones nuevas de algunos niveles (Level_N_a: 5, 11 y 113, con su pista hint/level_N_a);
        # el juego las usa con level_test_ver2 (LevelManager.LoadLevel): se usan esas
        if G.get(path + '_a', 'GameObject') is not None:
            print('  nivel %d: version nueva (level_%d_a)' % (n, n))
            path = path + '_a'
        L = extract_level(G, G.get(path, 'GameObject'), sprites, n)
        if not any(ly['cells'] for ly in L['layers']) or not L['snake'] or not L['goal']:
            print('  nivel %d vacio en el APK (el original lo sustituia por configuracion remota): se omite' % n)
            continue
        h = G.get(path.replace('level/', 'hint/'), 'TextAsset')
        L['hint'] = decode_hint(h.read().m_Script) if h else ''
        levels.append(L)
    levels.sort(key=lambda l: l['num'])
    # sprites de casillas y de los tutoriales al atlas del mundo
    spr_map = {}
    for i, (obj, group) in enumerate(sprites.lst):
        spr_map[i] = world.add_unity(obj)
    blob = bytearray()
    offs = []
    for L in levels:
        offs.append(len(blob))
        blob += level_blob(L, spr_map)
        while len(blob) % 4:
            blob += b'\0'
    head = b'SNL1' + struct.pack('<I', len(levels)) + b''.join(struct.pack('<I', 8 + 4 * len(levels) + o) for o in offs)
    with open(os.path.join(out, 'levels.bin'), 'wb') as f:
        f.write(head + blob)
    print('  %d niveles, %d sprites de casillas' % (len(levels), len(sprites.lst)))
    return levels


GATE_ROT = {0: 3, 90: 0, 180: 2, 270: 1}     # giro del portal -> direccion de entrada (0 arriba 1 abajo 2 izq 3 der)


def level_blob(L, spr_map):
    cells = [(c[0], c[1]) for ly in L['layers'] for c in ly['cells']] + [L['goal'], L['snake']['cell']]
    cells += [o['cell'] for o in L['objs'] if 'cell' in o]
    for o in L['objs']:
        if o['t'] == O_TUT and WORLD is not None:
            # la imagen del tutorial (Tut_01: la cruceta para moverse) tiene que caber en pantalla con el nivel
            im, ox, oy, uw, uh = WORLD.items[spr_map[o['spr']]]
            x, y = o['pos']
            cells += [(int(math.floor(x + ox)), int(math.floor(y - oy - uh))), (int(math.floor(x + ox + uw)), int(math.floor(y - oy)))]
    x0 = min(c[0] for c in cells) - 1
    y0 = min(c[1] for c in cells) - 1
    x1 = max(c[0] for c in cells) + 1
    y1 = max(c[1] for c in cells) + 1
    W, H = x1 - x0 + 1, y1 - y0 + 1
    assert W < 64 and H < 64, (L['num'], W, H)
    f = L['snake']['f']
    flags = (f['isFallWhenCreate'] << 0 | f['isDownWhenCreate'] << 1 | f['isUpWhenCreate'] << 2 |
             f['isLeftWhenCreate'] << 3 | f['isSecondRightWhenCreate'] << 4 | f['isSecondDownWhenCreate'] << 5 |
             f['isSecondUpWhenCreate'] << 6)
    out = bytearray(struct.pack('<HhhBBBBbbbb', L['num'], x0, y0, W, H, flags, 0,
                                L['snake']['cell'][0] - x0, L['snake']['cell'][1] - y0,
                                L['goal'][0] - x0, L['goal'][1] - y0))
    solid = bytearray(W * H)
    for (x, y) in L['solid']:
        solid[(y - y0) * W + (x - x0)] = 1
    out += solid
    # casillas de decorado en orden de dibujo (las de atras primero)
    tiles = []
    for li, ly in enumerate(sorted(L['layers'], key=lambda l: l['order'])):
        for (x, y, s, fl, a) in ly['cells']:
            tiles.append((x - x0, y - y0, li, fl | (8 if ly['order'] > 5 else 0), spr_map[s]))
    out += struct.pack('<H', len(tiles))
    for t in tiles:
        out += struct.pack('<BBBBH', *t)
    objs = [o for o in L['objs']]
    out += struct.pack('<H', len(objs))
    for o in objs:
        if o['t'] == O_TUT:
            x, y = o['pos']
            out += struct.pack('<BbbBbBH', o['t'], 0, 0, 0, 0, 0, spr_map[o['spr']])
            out += struct.pack('<hh', int(round((x - x0) * 256)), int(round((y - y0) * 256)))
            continue
        a = b = 0
        if o['t'] == O_APPLE:
            a = o['double']
        elif o['t'] == O_GATE:
            a = GATE_ROT.get(o['rot'], 3)
            b = o['partner']
        elif o['t'] == O_SWITCH:
            b = o['barrier']
        elif o['t'] == O_BARRIER:
            a = o['bdir']
            b = o['dir']
        elif o['t'] in (O_SPIKE, O_SAW):
            a = o['rot'] // 90
        out += struct.pack('<BbbBbBH', o['t'], o['cell'][0] - x0, o['cell'][1] - y0, a, b, 0, 0)
        out += struct.pack('<hh', 0, 0)
    hint = L['hint'].encode()
    out += struct.pack('<H', len(hint)) + hint
    return out


# ---------------------------------------------------------------- sprites del mundo y de la interfaz
UI_SPRITES = ['Logo', 'Background_Home', 'Btn_Play', 'Btn_Green', 'Btn_Gray', 'Btn_Blue', 'Popup_Win', 'Popup_Lose',
              'Banner_Win', 'Banner_Lose', 'Level_Bas', 'Slot_Locked', 'Slot_Unlocked', 'HintArrow', 'Btn_Hint',
              'Btn_Replay', 'Sound_On', 'Sound_Off', 'Music_On', 'Music_Off', 'Btn_Skin', 'Btn_Settings', 'Btn_Back',
              'Star', 'NumberBox', 'button_home', 'icon_tick', 'Frame', 'Title_Base', 'base_popup', 'Icon_Successful',
              'Icon_Lose', 'Btn_NextLevel', 'icon_Hint', 'Icon_BG', 'Icon_Skin', 'TickIcon', 'Star_banner_win_on',
              'Star_banner_win_off', 'button_close', 'icon_arrow', 'Btn_Foward', 'Icon_Replay', 'base_item_daily',
              'base_item_claimed', 'base_item_day7', 'base_item_day7_claimed', 'reward_skin_1', 'reward_skin_2',
              'reward_skin_3', 'icon_Hint', 'Icon_SkipLevel', 'Slot_Locked', 'icon_shop', 'icon_daily', 'icon_setting',
              'Btn_Minigame', 'Title_Rating', 'Icon_Ads', 'NewSkinUnlocked', 'NewBGUnlocked', 'Btn_Skin_Christmas',
              'button_ranking', 'icon_HOME', 'icon_noti', 'Btn_Move', 'Btn_Move_Active', 'icon_MOVE_0',
              # 14.2.9: manzanas del selector de niveles (LevelSelectElement), Page_Base y la oferta limitada
              'Apple_Green', 'Apple_Yellow', 'Apple_Gray', 'Apple_Red', 'Page_Base', 'Icon_LimitedOffer',
              # pantallas de la 14.2.9: banners de los paquetes de temporada (inicio), ventana de ajustes, banderas...
              'Banner_LunarNewYear', 'Banner_Summer', 'Banner_Pride', 'Banner_Worldcup', 'tag_soldout', 'tag_saleOff',
              'Base_Time', 'Popup_Setting_Gameplay', 'Btn_Cancel', 'Flag', 'bg_mask', 'NewSkin', 'NewBG']
UI_MAXPX = {'NewSkinUnlocked': 160, 'NewBGUnlocked': 160, 'Title_Rating': 160, 'Background_Home': 0, 'Logo': 200, 'Popup_Win': 220, 'Popup_Lose': 220, 'base_popup': 220,
            'Banner_Win': 200, 'Banner_Lose': 200, 'Title_Base': 160, 'Frame': 160,
            'Btn_Play': 128, 'Btn_Minigame': 128, 'Banner_LunarNewYear': 128, 'Banner_Summer': 128, 'Banner_Pride': 128,
            'Banner_Worldcup': 128, 'Popup_Setting_Gameplay': 220, 'Flag': 120, 'tag_soldout': 80}


# nombres que cambiaron en la 14.2.9 (Unity 6): se usa el primero que exista
UI_ALIASES = {'Logo': ['Logo', 'Logo_v12']}


def find_sprites_by_name(G, names):
    want = set(names)
    for n in names:
        want.update(UI_ALIASES.get(n, []))
    found = {}
    for fname in sorted(G.files):
        for o in G.files[fname].objects.values():
            if o.type.name == 'Sprite':
                try:
                    n = o.peek_name()
                except Exception:
                    continue
                if n in want and n not in found:
                    found[n] = o
    for n in names:
        if n not in found:
            for a in UI_ALIASES.get(n, []):
                if a in found:
                    found[n] = found[a]
                    break
    return found


def object_sprites(G, world):
    """Sprites de los objetos del nivel, sacados de un prefab de cada tipo."""
    want = {}
    # un nivel con cada objeto: piedras (30), pinchos (26), sierras, portales (211), interruptor (241)
    for n in (1, 26, 30, 50, 211, 241, 120):
        go = G.get('level/level_%d' % n, 'GameObject')
        if go is None:
            continue
        root = G.go(go)

        def walk(g):
            r = g['comp'].get('SpriteRenderer')
            if r and r['m_Sprite']['m_PathID']:
                key = g['name'].split(' ')[0]
                if key not in want:
                    want[key] = G.resolve(g['obj'].assets_file, r['m_Sprite'])
            if 'Stone' in g['mb'] and 'stone_list' not in want:
                # Stone.listSprite son los SpriteRenderer Stone_0, Stone_H1, Stone_H2, Stone_V1 y Stone_V2: la piedra
                # se dibuja con las cinco piezas (cubo delante con orden 8; bordes detras con orden -1 y -2)
                lst = {}
                for p in g['mb']['Stone']['listSprite']:
                    r = G.resolve(g['obj'].assets_file, p)
                    if r is None:
                        continue
                    rt = r.read_typetree()
                    nm = G.resolve(r.assets_file, rt['m_GameObject']).read_typetree()['m_Name']
                    lst[nm] = G.resolve(r.assets_file, rt['m_Sprite'])
                want['stone_list'] = [lst.get(n) for n in ('Stone_0', 'Stone_H1', 'Stone_H2', 'Stone_V1', 'Stone_V2')]
            for c in g['children']:
                walk(c)
        walk(root)
    ids = {}
    # la piedra se ve con una mascara (SpriteMask "Stone") sobre Stone_0/H/V: el sprite "Stone" es la piedra entera
    st = find_sprites_by_name(G, ['Stone'])
    if 'Stone' in st:
        want['Stone'] = st['Stone']
    for key in ('GoalRenderer', 'Stone', 'Spike_1', 'Spike_2', 'SawRenderer', 'Portal_Back', 'Portal_Front', 'Trigger', 'Hole_Back',
                'Hole_Front', 'Barrier', 'AppleRenderer'):
        if want.get(key) is not None and want[key].type.name == 'Sprite':
            ids[key] = world.add_unity(want[key])
        else:
            print('  aviso: falta el sprite', key)
            ids[key] = -1
    ids['stone_list'] = [world.add_unity(o) if o is not None and o.type.name == 'Sprite' else -1
                         for o in want.get('stone_list', [])]
    if -1 in ids['stone_list'] or not ids['stone_list']:
        print('  aviso: faltan piezas de la piedra', ids['stone_list'])
    return ids


def export_world(G, out):
    world = SpriteSheet(FMT_T8)
    levels = export_levels(G, out, world)
    objs = object_sprites(G, world)
    ui = find_sprites_by_name(G, UI_SPRITES)
    ui_ids = {}
    for n in UI_SPRITES:
        if n not in ui or UI_MAXPX.get(n, 1) == 0:
            ui_ids[n] = -1
            continue
        ui_ids[n] = world.add_unity(ui[n], scale=48, maxpx=UI_MAXPX.get(n, 96))
    # tabla de indices para el codigo C (orden fijo de src/gfx.h)
    table = [objs['Stone'], objs['Spike_1'], objs['Spike_2'], objs['SawRenderer'], objs['Portal_Back'],
             objs['Portal_Front'], objs['Trigger'], objs['Hole_Back'], objs['Hole_Front'], objs['Barrier'],
             objs['GoalRenderer'], objs['AppleRenderer']]
    portal = [-1] * 6
    stones = (objs['stone_list'] + [-1] * 5)[:5]
    uitab = [ui_ids[n] for n in UI_SPRITES]
    size = world.write(os.path.join(out, 'world.spr'))
    with open(os.path.join(out, 'world.idx'), 'wb') as f:
        allidx = table + portal + stones + uitab
        f.write(struct.pack('<H', len(allidx)) + struct.pack('<%dh' % len(allidx), *allidx))
    print('  world.spr: %d sprites, %d KB' % (len(world.items), size // 1024))
    # pantalla de inicio (Background_Home) como fondo 0 del menu
    if 'Background_Home' in ui:
        im, _, _ = sprite_full(ui['Background_Home'])
        write_bg(im, os.path.join(out, 'home.tex'))
    return levels


def write_bg(im, path):
    im = im.convert('RGB')
    # recorte al formato de la PSP (480x272 = 16:9 aprox.) sin deformar
    W, H = im.size
    tw = H * 480 / 272
    if tw <= W:
        x0 = (W - tw) / 2
        im = im.crop((int(x0), 0, int(x0 + tw), H))
    else:
        th = W * 272 / 480
        y0 = (H - th) / 2
        im = im.crop((0, int(y0), W, int(y0 + th)))
    im = im.resize((480, 272), Image.LANCZOS)
    canvas = Image.new('RGB', (512, 272))
    canvas.paste(im, (0, 0))
    W, H, fmt, data = encode_texture(canvas.convert('RGBA'), FMT_5650)
    with open(path, 'wb') as f:
        f.write(b'BG16' + struct.pack('<HHBBH', 512, 272, fmt, 0, 0) + data)


def export_backgrounds(G, out):
    n = 0
    for i in range(64):
        o = G.get('background/%d' % i, 'Sprite')
        if o is None:
            continue
        im, _, _ = sprite_full(o)
        write_bg(im, os.path.join(out, 'bg%02d.tex' % i))
        n += 1
    print('  %d fondos' % n)
    return n


# ---------------------------------------------------------------- aspectos de la serpiente
SKIN_PARTS = ['body/light/body_1_horizontal', 'body/light/body_1_verticle', 'body/light/body_1_horizontal_in_l_1',
              'body/light/tail_1_horizontal', 'body/dark/body_2_horizontal', 'body/dark/body_2_verticle',
              'body/dark/body_2_horizontal_in_l_1', 'body/dark/tail_2_horizontal',
              'body/light/body_1_verticle_out_r_1', 'body/dark/body_2_verticle_out_r_1']
# Orden fijo (src/skin.h). Las piezas tienen la sombra amarilla siempre abajo y a la derecha: no se giran, se usan
# las dos esquinas del original (in_L une derecha y abajo, out_R une arriba e izquierda) y sus espejos.
HEAD_FRAMES = (['head/idle/head_idle_%d' % i for i in (1, 2, 3)] + ['head/blink/head_blink_%d' % i for i in range(1, 6)] +
               ['head/eat/head_eat_%d' % i for i in range(1, 11)] +
               ['head/idle_to_eat/head_idle_to_eat_%d' % i for i in range(1, 6)] +
               ['head/idle_to_stuck/head_idle_to_stuck_%d' % i for i in range(1, 5)] + ['head/head_die'])


def export_skins(G, out):
    n = 0
    for sid in range(64):
        base = 'snake/%d/' % sid
        if G.get(base + SKIN_PARTS[0], 'Sprite') is None:
            continue
        sh = SpriteSheet(FMT_T8, 512, 256)
        ok = True
        for p in SKIN_PARTS + HEAD_FRAMES:
            o = G.get(base + p, 'Sprite')
            if o is None:
                # algunos aspectos no traen todos los fotogramas: se repite el de reposo
                o = G.get(base + 'head/idle/head_idle_1', 'Sprite') if 'head' in p else None
            if o is None:
                ok = False
                break
            sh.add_unity(o, scale=PX if 'body' in p else 40)
        if not ok:
            print('  aviso: aspecto %d incompleto' % sid)
            continue
        # manzana a juego (Apple.ChangeSkin) y su version "cerca" (con la boca abierta de la serpiente)
        for ap in ('apple/%d' % sid, 'apple/0'):
            o = G.get(ap, 'Sprite')
            if o is not None:
                sh.add_unity(o)
                break
        icon = G.get('skin/%d' % sid, 'Sprite') or G.get('shopitems/skins/%d' % sid, 'Sprite')
        if icon is not None:
            sh.add_unity(icon, scale=40, maxpx=64)
        sh.write(os.path.join(out, 'skin%02d.spr' % sid))
        n += 1
    print('  %d aspectos de serpiente' % n)
    return n


# ---------------------------------------------------------------- fuente
FONT_SIZES = (12, 16, 22, 30)
FONT_CHARS = ''.join(chr(c) for c in range(32, 127)) + 'áéíóúÁÉÍÓÚñÑüÜ¡¿·'
PAD_CHARS = '\u2715\u25cb\u25a1\u25b3'   # botones de la PSP: equis, circulo, cuadrado, triangulo


def export_font(G, out):
    ttf = None
    for f in G.files.values():
        for o in f.objects.values():
            if o.type.name == 'Font':
                try:
                    t = o.read_typetree()
                except Exception:
                    continue
                if 'LILITA' in t['m_Name'].upper() and t.get('m_FontData'):
                    ttf = bytes(t['m_FontData'])
                    break
        if ttf:
            break
    sh = SpriteSheet(FMT_T8, 512, 512)
    widths = []
    for size in FONT_SIZES:
        try:
            font = ImageFont.truetype(io.BytesIO(ttf), size) if ttf else ImageFont.load_default()
        except Exception:
            font = ImageFont.load_default()
        asc, desc = font.getmetrics()
        for ch in PAD_CHARS:
            # dibujados a mano con los colores de la PSP (la fuente no los trae)
            h = asc + desc + 2
            w = h
            im = Image.new('RGBA', (w + 2, h + 2), (0, 0, 0, 0))
            d = ImageDraw.Draw(im)
            m = max(2, h // 5)
            box = (m, m + 1, w - m + 1, h - m + 2)
            lw = max(1, size // 8)
            if ch == '\u2715':
                d.line((box[0], box[1], box[2], box[3]), fill=(110, 150, 255, 255), width=lw + 1)
                d.line((box[0], box[3], box[2], box[1]), fill=(110, 150, 255, 255), width=lw + 1)
            elif ch == '\u25cb':
                d.ellipse(box, outline=(255, 90, 90, 255), width=lw + 1)
            elif ch == '\u25a1':
                d.rectangle(box, outline=(255, 120, 220, 255), width=lw + 1)
            else:
                d.polygon(((box[0] + box[2]) / 2, box[1], box[2], box[3], box[0], box[3]), outline=(80, 230, 170, 255), width=lw + 1)
            sh.add_image(im, 0, 0, im.width / 256.0, im.height / 256.0)
            widths.append(w + 2)
        for ch in FONT_CHARS:
            bb = font.getbbox(ch)
            adv = font.getlength(ch)
            w = max(1, bb[2] - min(bb[0], 0) + 2)
            h = asc + desc + 2
            # contorno oscuro como en la interfaz del juego (materiales "Outline")
            im = Image.new('RGBA', (w + 2, h + 2), (0, 0, 0, 0))
            d = ImageDraw.Draw(im)
            ox = 1 - min(bb[0], 0)
            d.text((ox + 1, 2), ch, font=font, fill=(255, 255, 255, 255), stroke_width=1 if size >= 16 else 0,
                   stroke_fill=(40, 20, 10, 255))
            sh.add_image(im, 0, 0, im.width / 256.0, im.height / 256.0)
            widths.append(int(round(adv)))
    sh.write(os.path.join(out, 'font.spr'))
    with open(os.path.join(out, 'font.idx'), 'wb') as f:
        chars = PAD_CHARS + FONT_CHARS
        f.write(struct.pack('<HH', len(FONT_SIZES), len(chars)))
        f.write(struct.pack('<%dH' % len(FONT_SIZES), *FONT_SIZES))
        f.write(struct.pack('<%dH' % len(chars), *[ord(c) for c in chars]))
        f.write(struct.pack('<%dB' % len(widths), *widths))
    print('  fuente: %s' % ('Lilita One del APK' if ttf else 'por defecto (no se encontro la del APK)'))


# ---------------------------------------------------------------- sonido
SFX = ['worm', 'eatapple', 'stop', 'thorn', 'drop', 'move_stone', 'stone_drop', 'win', 'lose', 'clickbutton',
       '8bitclick', 'progress', 'rewardpopup', 'unlockskin']


def audio_pcm(o, rate_out=22050, mono=True):
    """AudioClip (FSB5 Vorbis) -> PCM float (mono a 22050 Hz para los efectos)."""
    import fsb5
    import soundfile
    clip = o.read()
    raw = clip.m_AudioData
    if not raw:
        from UnityPy.helpers.ResourceReader import get_resource_data
        r = clip.m_Resource
        raw = get_resource_data(r.m_Source, clip.object_reader.assets_file, r.m_Offset, r.m_Size)
    fsb = fsb5.FSB5(bytes(raw))
    s = fsb.samples[0]
    ogg = fsb.rebuild_sample(s)
    data, rate = soundfile.read(io.BytesIO(ogg), dtype='float32', always_2d=True)
    if not mono:
        st = data if data.shape[1] == 2 else np.repeat(data, 2, axis=1)
        if rate != rate_out:
            n = int(len(st) * rate_out / rate)
            x = np.linspace(0, len(st) - 1, n)
            st = np.stack([np.interp(x, np.arange(len(st)), st[:, c]) for c in range(2)], axis=1)
        return st
    mono = data.mean(axis=1)
    if rate != rate_out:
        n = int(len(mono) * rate_out / rate)
        mono = np.interp(np.linspace(0, len(mono) - 1, n), np.arange(len(mono)), mono)
    return mono


AUDIO_HELP = '''
ERROR: no se pudo convertir el sonido del juego (%s).
Los sonidos de Unity (FSB5/Vorbis) necesitan las librerias libogg y libvorbis del sistema. En Ubuntu/WSL:
    sudo apt install libogg0 libvorbis0a libvorbisfile3 libsndfile1
y vuelve a ejecutar ./actualizar.sh'''


def export_audio(G, out):
    clips = []
    for name in SFX:
        o = G.get('sounds/' + name, 'AudioClip')
        if o is None:
            print('  aviso: falta el sonido', name)
            clips.append(b'')
            continue
        try:
            pcm = audio_pcm(o)
        except Exception as e:
            raise SystemExit(AUDIO_HELP % ('%s: %s' % (name, e)))
        clips.append((np.clip(pcm, -1, 1) * 32000).astype('<i2').tobytes())
    head = b'SFX1' + struct.pack('<I', len(clips))
    off = 8 + 8 * len(clips)
    tab = b''
    for c in clips:
        tab += struct.pack('<II', off, len(c) // 2)
        off += (len(c) + 15) & ~15
    body = b''.join(c + b'\0' * (((len(c) + 15) & ~15) - len(c)) for c in clips)
    with open(os.path.join(out, 'sfx.bin'), 'wb') as f:
        f.write(head + tab + body)
    # musica en MP3: la decodifica el Media Engine de la PSP (sceMp3), sin gastar CPU
    import lameenc
    for clip, fn in (('sounds/bgm', 'music.mp3'), ('sounds/bgm_lunar', 'music_lunar.mp3')):
        o = G.get(clip, 'AudioClip')
        if o is None:
            continue
        try:
            st = audio_pcm(o, 44100, mono=False)
            enc = lameenc.Encoder()
            enc.set_bit_rate(112)
            enc.set_in_sample_rate(44100)
            enc.set_channels(2)
            enc.set_quality(2)
            pcm = (np.clip(st, -1, 1) * 30000).astype('<i2').tobytes()
            mp3 = enc.encode(pcm) + enc.flush()
            with open(os.path.join(out, fn), 'wb') as f:
                f.write(mp3)
            print('  musica %s: %.0f s, %d KB' % (fn, len(st) / 44100, len(mp3) // 1024))
        except Exception as e:
            raise SystemExit(AUDIO_HELP % ('%s: %s' % (clip, e)))
    print('  %d efectos de sonido' % sum(1 for c in clips if c))


# ---------------------------------------------------------------- icono del menu de la PSP
def export_xmb(G, out):
    ui = find_sprites_by_name(G, ['Background_Home', 'Logo'])
    if 'Background_Home' in ui:
        bg, _, _ = sprite_full(ui['Background_Home'])
        bg = bg.convert('RGB')
        W, H = bg.size
        th = W * 272 / 480
        bg = bg.crop((0, int((H - th) / 2), W, int((H + th) / 2))).resize((480, 272), Image.LANCZOS)
        bg.save(os.path.join(out, 'PIC1.PNG'))
        icon = bg.crop((0, 0, 272, 150)).resize((144, 80), Image.LANCZOS).convert('RGBA')
        if 'Logo' in ui:
            lg, _, _ = sprite_full(ui['Logo'])
            lg = lg.crop(lg.getbbox())
            f = min(140 / lg.width, 76 / lg.height)
            lg = lg.resize((int(lg.width * f), int(lg.height * f)), Image.LANCZOS)
            icon.alpha_composite(lg, ((144 - lg.width) // 2, (80 - lg.height) // 2))
        icon.convert('RGB').save(os.path.join(out, 'ICON0.PNG'))


def row_json(G, o):
    cl, d = G.mono(o)
    try:
        return json.loads(d['serializedRow'])
    except Exception:
        return None


# graficos de la tienda de la 14.2.9 (orden fijo: SU_* de src/ui_ids.h), a 1/4 de su tamano (1920x1080 -> 480x270)
SHOP_UI = ['PopUp_SkinShop', 'Tab_Selected', 'Tab_Unselected', 'Icon_Skin', 'Icon_BG', 'Slot_Unlocked', 'Slot_Highlight',
           'Base_Lock', 'Lock', 'TagNew', 'TagLunar', 'TagHalloween', 'TagXmas', 'TagVip', 'Ground', 'Scroller',
           'Scroller2', 'Btn_Back', 'SpecialBundle', 'Summer_Bundle', 'Pride_Bundle', 'Worldcup_Bundle',
           'LunarNewYear_Bundle_', 'DavidDuck_Bundle', 'BasePack', 'icon_hint']


def export_shop(G, out):
    """Tienda: iconos de aspectos y fondos (shopitems/), dibujos de la barra de progreso (progress/) y los datos
    de juego: estado de cada articulo (data/new: ShopInfo), progreso de aspectos (SkinProgress), recompensa diaria,
    el ranking del modo clasico (ClassicLeaderboardData) y el porcentaje de jugadores por nivel (top_player)."""
    sh = SpriteSheet(FMT_T8, 512, 512)
    skin_icon, bg_icon, prog = [], [], []
    for i in range(64):
        o = G.get('shopitems/skins/%d' % i, 'Sprite')
        if o is None and G.get('snake/%d/%s' % (i, SKIN_PARTS[0]), 'Sprite') is not None:
            # aspecto sin icono de tienda en el APK (el 14): su icono propio o su cabeza
            o = G.get('skin/%d' % i, 'Sprite') or G.get('snake/%d/head/idle/head_idle_1' % i, 'Sprite')
        skin_icon.append(sh.add_unity(o, scale=48, maxpx=56) if o else -1)
    for i in range(64):
        o = G.get('shopitems/backgrounds/%d' % i, 'Sprite')
        bg_icon.append(sh.add_unity(o, scale=48, maxpx=72) if o else -1)
    # siluetas del progreso (RewardProgressPopup): progress/progress_N y full_N por numero de aspecto, y las de los
    # fondos en progress/bg/ (indices 0-21 aspectos, 22-43 fondos)
    for folder in ('progress/', 'progress/bg/'):
        for i in range(11):
            for kind in ('progress_%d', 'full_%d'):
                o = G.get(folder + kind % i, 'Sprite')
                prog.append(sh.add_unity(o, scale=48, maxpx=128) if o else -1)
    found = find_sprites_by_name(G, SHOP_UI)
    shop_ui = []
    for nm in SHOP_UI:
        o = found.get(nm)
        if o is None:
            shop_ui.append(-1)
            continue
        r = o.read_typetree()['m_Rect']
        shop_ui.append(sh.add_unity(o, scale=1000, maxpx=max(4, int(round(max(r['width'], r['height']) / 4)))))
    sh.write(os.path.join(out, 'shop.spr'))
    # estado de la tienda (ShopItem: State 0 bloqueado, 2 elegido, 4 recompensa diaria, 6 evento; NeededAdWatchCount)
    items = None
    for k, objs in G.container.items():
        for o in objs:
            if o.type.name == 'MonoBehaviour':
                cl, d = G.mono(o)
                if d and 'shopItems' in d and items is None:
                    items = d['shopItems']
    items = items or []
    sp = None
    o = G.get('data/skinprogressdata', 'MonoBehaviour')
    if o is not None:
        cl, d = G.mono(o)
        sp = d.get('listProgress') if d else None
    sp = sp or []
    daily = []
    o = G.get('data/dailyreward', 'MonoBehaviour')
    if o is not None:
        cl, d = G.mono(o)
        for r in (d or {}).get('rows', []):
            ro = G.resolve(o.assets_file, r)
            j = row_json(G, ro) if ro else None
            if j:
                daily.append(j)
    daily.sort(key=lambda j: j.get('Day', 0))
    names, lbp = [], (120, 130, 8, 3, 2, 1)
    f = G.files.get('sharedassets0.assets')
    for ob in (f.objects.values() if f else []):
        if ob.type.name == 'MonoBehaviour' and (G.script_of(ob) or ('', '', ''))[2] == 'ClassicLeaderboardData':
            r = ob.reader
            r.Position = ob.byte_start
            b = r.read_bytes(ob.byte_size)
            p = 28
            n = struct.unpack_from('<i', b, p)[0]
            p += 4 + ((n + 3) & ~3)
            cnt = struct.unpack_from('<i', b, p)[0]
            p += 4
            for _ in range(cnt):
                ln = struct.unpack_from('<i', b, p)[0]
                p += 4
                names.append(b[p:p + ln].decode('utf-8', 'replace'))
                p += (ln + 3) & ~3
            lbp = struct.unpack_from('<6i', b, p)
    top = []
    o = G.get('top_player', 'TextAsset')
    if o is not None:
        txt = o.read().m_Script
        if isinstance(txt, bytes):
            txt = txt.decode('utf-8', 'replace')
        for line in txt.splitlines()[1:]:
            pr = line.split(',')
            if len(pr) == 2:
                top.append(float(pr[1]))
    blob = bytearray(b'MET2')
    blob += struct.pack('<H', len(items))
    for it in items:
        ev = {'Halloween': 1, 'Xmas': 2, 'Lunar': 3}.get(it.get('Description', ''), 0)
        blob += struct.pack('<BBBBB', it['Id'], it['ShopType'], it['State'], it['NeededAdWatchCount'], ev)
    blob += struct.pack('<H', len(sp))
    for e in sp:
        blob += struct.pack('<HHBB', e['startLevel'], e['endLevel'], e['id'], e['type'])
    blob += struct.pack('<H', len(daily))
    for j in daily:
        rw = j.get('Reward', {})
        kind = 1 if j.get('TypeReward') == 'Skin' else 0
        blob += struct.pack('<BBB', kind, rw.get('Quantity', 1), rw.get('Value', 0))
    blob += struct.pack('<H', len(names))
    for n in names:
        e = n.encode()[:15]
        blob += struct.pack('<B', len(e)) + e
    blob += struct.pack('<6i', *lbp)
    blob += struct.pack('<H', len(top)) + b''.join(struct.pack('<H', int(round(t * 100))) for t in top)
    blob += struct.pack('<H', len(skin_icon)) + struct.pack('<%dh' % len(skin_icon), *skin_icon)
    blob += struct.pack('<H', len(bg_icon)) + struct.pack('<%dh' % len(bg_icon), *bg_icon)
    blob += struct.pack('<H', len(prog)) + struct.pack('<%dh' % len(prog), *prog)
    # paquetes de la tienda de pago (BasePackageItem: 0 sin anuncios, 1 pistas, 2 saltos, 3 aspecto, 4 fondo) y el
    # premio por "ver un anuncio" (BaseAdsPackageItem); en la PSP se pagan con fichas
    packs = {}
    for fname in G.files:
        for ob in G.files[fname].objects.values():
            if ob.type.name != 'MonoBehaviour':
                continue
            sc = G.script_of(ob)
            if not sc or sc[2] != 'BasePackageItem':
                continue
            cl, d = G.mono(ob)
            if d and d.get('packageId') is not None:
                packs.setdefault(d['packageId'], ([(r['type'], r['quantity']) for r in d.get('rewards', [])],
                                                  int(bool(d.get('hasTimer')))))
    blob += struct.pack('<H', len(packs))
    for pid in sorted(packs):
        rw = [r for r in packs[pid][0] if r[0] in (1, 2, 3, 4)][:8]
        # hasTimer: el paquete de la oferta limitada (Limited Offer Zone) de la 14.2.9
        blob += struct.pack('<BBB', pid, packs[pid][1], len(rw)) + b''.join(struct.pack('<BB', t, min(q, 255)) for t, q in rw)
    # orden de la tienda (ShopRegionUI.sortIndex de cada pestana) y niveles fuera del bucle (LevelConfig)
    orders = {}
    ignore = []
    for fname in G.files:
        for ob in G.files[fname].objects.values():
            if ob.type.name != 'MonoBehaviour':
                continue
            sc = G.script_of(ob)
            if not sc or sc[2] not in ('ShopRegionUI', 'LevelConfig'):
                continue
            cl, d = G.mono(ob)
            if not d:
                continue
            if sc[2] == 'ShopRegionUI' and d.get('sortIndex'):
                orders.setdefault(d.get('shopType', 0), d['sortIndex'])
            elif sc[2] == 'LevelConfig' and not ignore:
                ignore = list(d.get('ignoreLoopLevels') or [])
    for t in (0, 1):
        o = [i for i in orders.get(t, []) if 0 <= i < 64][:255]
        blob += struct.pack('<B', len(o)) + bytes(o)
    ignore = ignore[:64]
    blob += struct.pack('<B', len(ignore)) + b''.join(struct.pack('<H', i) for i in ignore)
    blob += struct.pack('<B', len(shop_ui)) + b''.join(struct.pack('<h', i) for i in shop_ui)
    with open(os.path.join(out, 'meta.bin'), 'wb') as fo:
        fo.write(blob)
    print('  tienda: %d articulos, %d paquetes, %d tramos de progreso, %d dias de recompensa, ranking de %d' %
          (len(items), len(packs), len(sp), len(daily), len(names)))
    print('  orden de la tienda: %d aspectos, %d fondos; %d niveles fuera del bucle' %
          (len(orders.get(0, [])), len(orders.get(1, [])), len(ignore)))


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)
    src, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    print('Leyendo el APK...')
    G = Game(src)
    print('Niveles, casillas, objetos e interfaz...')
    export_world(G, out)
    print('Fondos...')
    export_backgrounds(G, out)
    print('Aspectos de la serpiente...')
    export_skins(G, out)
    print('Fuente...')
    export_font(G, out)
    print('Tienda y datos de juego...')
    export_shop(G, out)
    print('Sonido...')
    export_audio(G, out)
    export_xmb(G, out)
    with open(os.path.join(out, 'VERSION'), 'w') as f:
        f.write('snake-puzzle-psp-data 1\n')
    print('Listo: datos en', out)


if __name__ == '__main__':
    main()
