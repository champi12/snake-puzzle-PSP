"""spine_render.py - pose fija de un esqueleto Spine 4.x (JSON) a una imagen, para las skins de Apple Worm

Solo lo que usan esas skins: huesos (posicion, giro, escala), adjuntos de tipo region, recortes ('clipping': las
esquinas del gusano son un cuadrado recortado con forma redondeada), orden de dibujo y las lineas de tiempo de
adjunto, giro, posicion, escala y orden de una animacion en un instante. Los adjuntos 'mesh' se ignoran (sombras).
"""
import math

from PIL import Image


def parse_atlas(text, tex):
    """regiones del .atlas -> {nombre: imagen RGBA del tamano original (orig), ya sin recorte ni giro}"""
    lines = text.splitlines()
    page_w = None
    for line in lines:
        if line.startswith('size:'):
            page_w = int(line.split(':', 1)[1].split(',')[0])
            break
    sc = tex.width / page_w if page_w else 1.0
    regs, cur = {}, None
    for line in lines:
        if not line.strip():
            continue
        if not line.startswith(' ') and ':' not in line:
            cur = line.strip()
            regs[cur] = {}
        elif cur and ':' in line:
            k, v = line.strip().split(':', 1)
            regs[cur][k.strip()] = v.strip()
    out = {}
    for name, d in regs.items():
        if 'xy' not in d and 'bounds' not in d:
            continue
        if 'bounds' in d:
            x, y, w, h = (int(t) for t in d['bounds'].split(','))
        else:
            x, y = (int(t) for t in d['xy'].split(','))
            w, h = (int(t) for t in d['size'].split(','))
        rot = d.get('rotate', 'false')
        rot = 90 if rot in ('true', '90') else 0
        ow, oh = (int(t) for t in d.get('orig', '%d,%d' % (w, h)).split(','))
        ox, oy = (int(t) for t in d.get('offset', '0,0').split(','))
        bw, bh = (h, w) if rot else (w, h)
        im = tex.crop((round(x * sc), round(y * sc), round((x + bw) * sc), round((y + bh) * sc)))
        if rot:
            im = im.rotate(90, expand=True)
        im = im.resize((max(1, w), max(1, h)), Image.LANCZOS)
        full = Image.new('RGBA', (ow, oh))
        full.paste(im, (ox, oh - h - oy))    # offset de Spine: desde abajo a la izquierda
        out[name] = full
    return out


def _mat(x, y, rot, sx, sy):
    c, s = math.cos(math.radians(rot)), math.sin(math.radians(rot))
    return (c * sx, -s * sy, x, s * sx, c * sy, y)


def _mul(a, b):
    return (a[0] * b[0] + a[1] * b[3], a[0] * b[1] + a[1] * b[4], a[0] * b[2] + a[1] * b[5] + a[2],
            a[3] * b[0] + a[4] * b[3], a[3] * b[1] + a[4] * b[4], a[3] * b[2] + a[4] * b[5] + a[5])


def _key(tl, t, default):
    """valor de una linea de tiempo en t (el ultimo fotograma <= t, sin interpolar)"""
    v = default
    for k in tl:
        if k.get('time', 0) <= t + 1e-6:
            v = k
    return v


def pose(skel, anim=None, t=None, extra_anims=()):
    """devuelve [(imagen-region, matriz mundo 2x3)] en orden de dibujo"""
    bones = {b['name']: dict(b) for b in skel['bones']}
    slots = [dict(s) for s in skel['slots']]
    for a in ([anim] if anim else []) + list(extra_anims):
        if a is None:
            continue
        an = skel['animations'][a]
        tt = t if t is not None else 1e9
        for bn, tls in an.get('bones', {}).items():
            b = bones.get(bn)
            if not b:
                continue
            base = next(x for x in skel['bones'] if x['name'] == bn)
            if 'rotate' in tls:
                k = _key(tls['rotate'], tt, None)
                if k is not None:
                    b['rotation'] = base.get('rotation', 0) + k.get('value', k.get('angle', 0))
            if 'translate' in tls:
                k = _key(tls['translate'], tt, None)
                if k is not None:
                    b['x'] = base.get('x', 0) + k.get('x', 0)
                    b['y'] = base.get('y', 0) + k.get('y', 0)
            if 'scale' in tls:
                k = _key(tls['scale'], tt, None)
                if k is not None:
                    b['scaleX'] = base.get('scaleX', 1) * k.get('x', 1)
                    b['scaleY'] = base.get('scaleY', 1) * k.get('y', 1)
        for sn, tls in an.get('slots', {}).items():
            for s in slots:
                if s['name'] == sn and 'attachment' in tls:
                    k = _key(tls['attachment'], tt, None)
                    if k is not None:
                        s['attachment'] = k.get('name')
                if s['name'] == sn and 'rgba' in tls:
                    k = _key(tls['rgba'], tt, None)
                    if k is not None:
                        s['color'] = k.get('color', 'ffffffff')
    # orden de dibujo (drawOrder): desplazamientos sobre el orden de los slots
    order = list(range(len(slots)))
    for a in ([anim] if anim else []) + list(extra_anims):
        if a is None:
            continue
        do = skel['animations'][a].get('drawOrder') or skel['animations'][a].get('draworder')
        if do:
            k = _key(do, t if t is not None else 1e9, None)
            if k is not None and k.get('offsets'):
                n = len(slots)
                new = [-1] * n
                unchanged = []
                idx = {s['name']: i for i, s in enumerate(slots)}
                orig = 0
                for off in k['offsets']:
                    si = idx[off['slot']]
                    while orig != si:
                        unchanged.append(orig)
                        orig += 1
                    new[orig + off['offset']] = orig
                    orig += 1
                while orig < n:
                    unchanged.append(orig)
                    orig += 1
                for i in range(n - 1, -1, -1):
                    if new[i] == -1:
                        new[i] = unchanged.pop()
                order = new
    world = {}

    def wm(n):
        if n in world:
            return world[n]
        b = bones[n]
        m = _mat(b.get('x', 0), b.get('y', 0), b.get('rotation', 0), b.get('scaleX', 1), b.get('scaleY', 1))
        if b.get('parent'):
            m = _mul(wm(b['parent']), m)
        world[n] = m
        return m
    skin = skel['skins'][0]['attachments'] if isinstance(skel['skins'], list) else skel['skins']['default']
    out = []
    for si in order:
        s = slots[si]
        an = s.get('attachment')
        if not an:
            continue
        a = skin.get(s['name'], {}).get(an)
        if a is None:
            continue
        if a.get('type') == 'clipping':
            m = wm(s['bone'])
            v = a['vertices']
            pts = [(m[0] * v[i] + m[1] * v[i + 1] + m[2], m[3] * v[i] + m[4] * v[i + 1] + m[5]) for i in range(0, len(v), 2)]
            out.append(('__clip__', pts, a.get('end'), s['name'], 1.0))
            continue
        if a.get('type', 'region') != 'region':
            continue
        alpha = int(s.get('color', 'ffffffff')[6:8], 16) / 255.0 if len(s.get('color', '')) >= 8 else 1.0
        local = _mat(a.get('x', 0), a.get('y', 0), a.get('rotation', 0), a.get('scaleX', 1), a.get('scaleY', 1))
        out.append((a.get('path', an), _mul(wm(s['bone']), local), a['width'], a['height'], alpha, s['name']))
    return out


def render(skel, regions, anim=None, t=None, box=None, px=128, extra_anims=()):
    """pinta la pose en una imagen de px x px; box = (x0, y0, x1, y1) en unidades de Spine (y hacia arriba)"""
    items = pose(skel, anim, t, extra_anims)
    if box is None:
        xs, ys = [], []
        for it in items:
            if it[0] == '__clip__':
                continue
            _, m, w, h = it[:4]
            for cx, cy in ((-w / 2, -h / 2), (w / 2, -h / 2), (-w / 2, h / 2), (w / 2, h / 2)):
                xs.append(m[0] * cx + m[1] * cy + m[2])
                ys.append(m[3] * cx + m[4] * cy + m[5])
        box = (min(xs), min(ys), max(xs), max(ys))
    x0, y0, x1, y1 = box
    k = px / max(x1 - x0, y1 - y0)
    W, H = max(1, round((x1 - x0) * k)), max(1, round((y1 - y0) * k))
    canvas = Image.new('RGBA', (W, H))
    clip = None   # (mascara, slot donde acaba)
    from PIL import ImageDraw, ImageChops
    for it in items:
        if it[0] == '__clip__':
            pts = [((x - x0) * k, (y1 - y) * k) for x, y in it[1]]
            mk = Image.new('L', (W, H), 0)
            ImageDraw.Draw(mk).polygon(pts, fill=255)
            clip = (mk, it[2])
            continue
        name, m, w, h, alpha, slot = it
        im = regions.get(name)
        if im is None:
            if clip and slot == clip[1]:
                clip = None
            continue
        # pixel de la region (u, v) -> unidades locales del adjunto -> mundo -> lienzo (y hacia abajo)
        iw, ih = im.size
        a = (w / iw, 0, -w / 2, 0, -h / ih, h / 2)
        to_canvas = (k, 0, -x0 * k, 0, -k, y1 * k)
        f = _mul(to_canvas, _mul(m, a))
        det = f[0] * f[4] - f[1] * f[3]
        if abs(det) < 1e-9:
            continue
        inv = (f[4] / det, -f[1] / det, (f[1] * f[5] - f[4] * f[2]) / det,
               -f[3] / det, f[0] / det, (f[3] * f[2] - f[0] * f[5]) / det)
        layer = im.transform((W, H), Image.AFFINE, inv, resample=Image.BILINEAR)
        if alpha < 1:
            r, g, b, al = layer.split()
            layer = Image.merge('RGBA', (r, g, b, al.point(lambda v: int(v * alpha))))
        if clip:
            r, g, b, al = layer.split()
            layer = Image.merge('RGBA', (r, g, b, ImageChops.multiply(al, clip[0])))
            if slot == clip[1]:
                clip = None
        canvas.alpha_composite(layer)
    return canvas, box
