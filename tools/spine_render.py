"""spine_render.py - pose fija de un esqueleto Spine 4.x (JSON) a una imagen, para las skins de Apple Worm

Solo lo que usan esas skins: huesos (posicion, giro, escala), adjuntos de tipo region, recortes ('clipping': las
esquinas del gusano son un cuadrado recortado con forma redondeada), orden de dibujo y las lineas de tiempo de
adjunto, giro, posicion, escala y orden de una animacion en un instante. Los adjuntos 'mesh' se ignoran (sombras).
"""
import math

ROT_SIGN = -1  # 'rotate: true' en el atlas: la region esta guardada girada; se endereza en sentido horario

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
            im = im.rotate(ROT_SIGN * 90, expand=True)
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
        if a.get('type') == 'mesh':
            # malla (el pelo y la oreja de la oveja...): vertices en el mundo, con pesos de huesos o sin ellos
            uvs, v, nuv = a['uvs'], a['vertices'], len(a['uvs']) // 2
            pts = []
            if len(v) == len(uvs):
                m = wm(s['bone'])
                pts = [(m[0] * v[i] + m[1] * v[i + 1] + m[2], m[3] * v[i] + m[4] * v[i + 1] + m[5])
                       for i in range(0, len(v), 2)]
            else:
                i = 0
                bnames = [b['name'] for b in skel['bones']]
                while i < len(v) and len(pts) < nuv:
                    nb = int(v[i])
                    i += 1
                    wx = wy = 0.0
                    for _ in range(nb):
                        bm = wm(bnames[int(v[i])])
                        x, y, w = v[i + 1], v[i + 2], v[i + 3]
                        wx += (bm[0] * x + bm[1] * y + bm[2]) * w
                        wy += (bm[3] * x + bm[4] * y + bm[5]) * w
                        i += 4
                    pts.append((wx, wy))
            alpha = int(s.get('color', 'ffffffff')[6:8], 16) / 255.0 if len(s.get('color', '')) >= 8 else 1.0
            out.append(('__mesh__', a.get('path', an), pts, [(uvs[k], uvs[k + 1]) for k in range(0, len(uvs), 2)],
                        a['triangles'], alpha, s['name']))
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
            if it[0] == '__mesh__':
                xs += [p[0] for p in it[2]]
                ys += [p[1] for p in it[2]]
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
        if it[0] == '__mesh__':
            _, name, pts, uvs, tris, alpha, slot = it
            im = regions.get(name)
            if im is not None and pts:
                iw, ih = im.size
                layer = Image.new('RGBA', (W, H))
                for t in range(0, len(tris), 3):
                    a_, b_, c_ = tris[t], tris[t + 1], tris[t + 2]
                    if max(a_, b_, c_) >= len(pts):
                        continue
                    dst = [((pts[q][0] - x0) * k, (y1 - pts[q][1]) * k) for q in (a_, b_, c_)]
                    src = [(uvs[q][0] * iw, uvs[q][1] * ih) for q in (a_, b_, c_)]
                    # afin destino -> origen del triangulo
                    (X1, Y1), (X2, Y2), (X3, Y3) = dst
                    den = (X2 - X1) * (Y3 - Y1) - (X3 - X1) * (Y2 - Y1)
                    if abs(den) < 1e-9:
                        continue
                    (u1, v1), (u2, v2), (u3, v3) = src
                    a0 = ((u2 - u1) * (Y3 - Y1) - (u3 - u1) * (Y2 - Y1)) / den
                    a1 = ((u3 - u1) * (X2 - X1) - (u2 - u1) * (X3 - X1)) / den
                    b0 = ((v2 - v1) * (Y3 - Y1) - (v3 - v1) * (Y2 - Y1)) / den
                    b1 = ((v3 - v1) * (X2 - X1) - (v2 - v1) * (X3 - X1)) / den
                    tri = im.transform((W, H), Image.AFFINE, (a0, a1, u1 - a0 * X1 - a1 * Y1, b0, b1, v1 - b0 * X1 - b1 * Y1),
                                       resample=Image.BILINEAR)
                    mk = Image.new('L', (W, H), 0)
                    ImageDraw.Draw(mk).polygon(dst, fill=255)
                    layer.paste(tri, (0, 0), ImageChops.multiply(tri.split()[3], mk))
                if clip:
                    r, g, b, al = layer.split()
                    layer = Image.merge('RGBA', (r, g, b, ImageChops.multiply(al, clip[0])))
                canvas.alpha_composite(layer)
            if clip and slot == clip[1]:
                clip = None
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
