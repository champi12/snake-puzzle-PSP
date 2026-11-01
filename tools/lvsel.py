"""lvsel.py - graficos del selector de niveles (los de Apple Worm) para la pantalla NIVELES de Snake Puzzle

Solo la interfaz de esa pantalla: ventana, casilla de nivel, estrellas (encendidas y apagadas), candado, flechas y el
boton de aspectos. Se sacan del APK de Apple Worm del usuario; si no esta, el juego usa su propio selector.

    python3 tools/lvsel.py apple-worm.xapk data/   ->  data/lvsel.spr y data/lvsel.idx (orden fijo de src/ui.c)
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

UI = ['window_lev_sel', 'place_level', 'star_left', 'star_centre', 'star_right', 'star_left_gray', 'star_centre_gray',
      'star_right_gray', 'icon_lock', 'arrow_left_norm', 'arrow_right_norm', 'skins_norm']


def export(src, out):
    import UnityPy
    from unityload import unpack
    import extract as ex
    data_dir, _ = unpack(src)
    env = UnityPy.load(data_dir)
    found = {}
    for f in env.files.values():
        files = [f] + list(getattr(f, 'files', {}).values()) if isinstance(getattr(f, 'files', None), dict) else [f]
        for ff in files:
            for o in (getattr(ff, 'objects', None) or {}).values():
                if o.type.name != 'Sprite':
                    continue
                try:
                    n = o.peek_name()
                except Exception:
                    continue
                if n in UI and n not in found:
                    found[n] = o
    sheet = ex.SpriteSheet(ex.FMT_T8)
    idx = [sheet.add_unity(found[n], scale=48, maxpx=200) if n in found else -1 for n in UI]
    if idx[UI.index('window_lev_sel')] < 0 or idx[UI.index('place_level')] < 0:
        raise SystemExit('no encuentro los graficos del selector de niveles en ' + src)
    os.makedirs(out, exist_ok=True)
    sheet.write(os.path.join(out, 'lvsel.spr'))
    with open(os.path.join(out, 'lvsel.idx'), 'wb') as f:
        f.write(struct.pack('<H', len(idx)) + struct.pack('<%dh' % len(idx), *idx))
    print('  selector de niveles: %d graficos' % sum(1 for i in idx if i >= 0))


if __name__ == '__main__':
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)
    export(sys.argv[1], sys.argv[2])
