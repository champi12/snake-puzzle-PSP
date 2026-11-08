"""Carga de los datos de Unity del APK de Snake Puzzle (Unity 2022.3, IL2CPP).

El juego no trae Assembly-CSharp.dll: el C# esta compilado a ARM64 (libil2cpp.so). Los MonoBehaviour se guardan
sin typetree, asi que la estructura de cada script se genera a partir de libil2cpp.so + global-metadata.dat
(TypeTreeGeneratorAPI), igual que hace el propio Unity al cargar la escena.
"""
import os
import shutil
import tempfile
import zipfile
import atexit

import UnityPy
from UnityPy.helpers.TypeTreeGenerator import TypeTreeGenerator
from UnityPy.helpers.TypeTreeNode import TypeTreeNode

UNITY_VERSION = '2022.3.67f2'


def _unzip_members(zf, prefix, dest):
    for n in zf.namelist():
        if n.startswith(prefix) and not n.endswith('/'):
            p = os.path.join(dest, n)
            os.makedirs(os.path.dirname(p), exist_ok=True)
            with zf.open(n) as a, open(p, 'wb') as b:
                shutil.copyfileobj(a, b)


def unpack(src):
    """Acepta el .xapk, el APK base (con el split de arm64 al lado) o una carpeta ya descomprimida.
    Devuelve (carpeta assets/bin/Data, ruta de libil2cpp.so)."""
    if os.path.isdir(src):
        data = os.path.join(src, 'assets', 'bin', 'Data')
        for arch in ('arm64', 'armeabi'):
            for root, _, files in os.walk(src):
                if 'libil2cpp.so' in files and arch in root:
                    return data, os.path.join(root, 'libil2cpp.so')
        raise SystemExit('No encuentro lib/*/libil2cpp.so en ' + src)
    tmp = tempfile.mkdtemp(prefix='snake_apk_')
    atexit.register(shutil.rmtree, tmp, True)
    apks = []
    with zipfile.ZipFile(src) as z:
        names = z.namelist()
        if any(n.endswith('.apk') for n in names):         # XAPK: zip con varios APK dentro
            for n in names:
                if n.endswith('.apk'):
                    p = os.path.join(tmp, os.path.basename(n))
                    with z.open(n) as a, open(p, 'wb') as b:
                        shutil.copyfileobj(a, b)
                    apks.append(p)
        else:
            apks.append(src)
            d = os.path.dirname(os.path.abspath(src))
            for f in os.listdir(d):                           # split de arm64 o armeabi-v7a junto al APK base
                if f.endswith('.apk') and ('arm64' in f or 'armeabi' in f):
                    apks.append(os.path.join(d, f))
    out = os.path.join(tmp, 'x')
    for a in apks:
        with zipfile.ZipFile(a) as z:
            _unzip_members(z, 'assets/bin/Data/', out)
            _unzip_members(z, 'lib/arm64-v8a/libil2cpp.so', out)
            _unzip_members(z, 'lib/armeabi-v7a/libil2cpp.so', out)
    data = os.path.join(out, 'assets', 'bin', 'Data')
    # 2.3.4 traia ARM64; 14.2.9 solo trae armeabi-v7a (los dos sirven para generar la estructura de los scripts)
    so = os.path.join(out, 'lib', 'arm64-v8a', 'libil2cpp.so')
    if not os.path.exists(so):
        so = os.path.join(out, 'lib', 'armeabi-v7a', 'libil2cpp.so')
    if not os.path.exists(os.path.join(data, 'data.unity3d')):
        raise SystemExit('El APK no tiene assets/bin/Data/data.unity3d: no parece Snake Puzzle')
    if not os.path.exists(so):
        raise SystemExit('Falta libil2cpp.so (usa el .xapk completo o pon config.arm64_v8a.apk / config.armeabi_v7a.apk al lado)')
    return data, so


class Game:
    def __init__(self, src):
        self.data_dir, so = unpack(src)
        self.env = UnityPy.load(self.data_dir)
        self.files = {}
        for k, f in self.env.files.items():
            if hasattr(f, 'objects') and f.objects:
                self.files[os.path.basename(k)] = f
            sub = getattr(f, 'files', None)
            if isinstance(sub, dict) and not hasattr(f, 'objects'):
                for k2, f2 in sub.items():
                    if hasattr(f2, 'objects'):
                        self.files[str(k2).split('/')[-1]] = f2
        self.res = self.files['resources.assets']
        ver = UNITY_VERSION
        for f in self.files.values():
            v = getattr(f, 'unity_version', '')
            if v and v[0].isdigit():
                ver = v
                break
        self.unity_version = ver
        self.gen = TypeTreeGenerator(ver)
        with open(so, 'rb') as a, open(os.path.join(self.data_dir, 'Managed', 'Metadata', 'global-metadata.dat'), 'rb') as b:
            self.gen.load_il2cpp(a.read(), b.read())
        self._nodes = {}
        self._scripts = {}
        self.tags, self.layers = self._tag_manager()
        self.container = self._resources()

    def _tag_manager(self):
        for o in self.files['globalgamemanagers'].objects.values():
            if o.type.name == 'TagManager':
                t = o.read_typetree()
                return t['tags'], t['layers']
        return [], []

    def _resources(self):
        """Rutas de Resources.Load -> lista de objetos (una ruta puede tener Texture2D y Sprite)."""
        out = {}
        for o in self.files['globalgamemanagers'].objects.values():
            if o.type.name == 'ResourceManager':
                for path, ptr in o.read_typetree()['m_Container']:
                    obj = self.resolve(o.assets_file, ptr)
                    if obj is not None:
                        out.setdefault(path, []).append(obj)
        return out

    def get(self, path, typ=None):
        for o in self.container.get(path, []):
            if typ is None or o.type.name == typ:
                return o
        return None

    def tag_name(self, t):
        return self.tags[t - 20000] if t >= 20000 and t - 20000 < len(self.tags) else ''

    # ---- referencias ----
    def resolve(self, af, ptr):
        fid, pid = ptr['m_FileID'], ptr['m_PathID']
        if pid == 0:
            return None
        if fid == 0:
            tgt = af
        else:
            tgt = self.files.get(af.externals[fid - 1].path.split('/')[-1])
            if tgt is None:
                return None
        return tgt.objects.get(pid)

    # ---- MonoBehaviour ----
    def script_of(self, obj):
        r = obj.reader
        r.Position = obj.byte_start
        r.read_int(); r.read_long(); r.read_u_byte(); r.align_stream()
        ptr = {'m_FileID': r.read_int(), 'm_PathID': r.read_long()}
        k = (id(obj.assets_file), ptr['m_FileID'], ptr['m_PathID'])
        if k not in self._scripts:
            s = self.resolve(obj.assets_file, ptr)
            if s is None:
                self._scripts[k] = None
            else:
                ms = s.read()
                self._scripts[k] = (ms.m_AssemblyName.replace('.dll', ''), ms.m_Namespace, ms.m_ClassName)
        return self._scripts[k]

    def nodes(self, asm, full):
        k = (asm, full)
        if k not in self._nodes:
            base = self.gen.get_nodes(asm, full)
            self._nodes[k] = TypeTreeNode.from_list(
                [TypeTreeNode(b.m_Level, b.m_Type, b.m_Name, 0, 0, m_MetaFlag=b.m_MetaFlag) for b in base])
        return self._nodes[k]

    def mono(self, obj):
        sc = self.script_of(obj)
        if not sc:
            return None, None
        full = (sc[1] + '.' + sc[2]) if sc[1] else sc[2]
        try:
            return sc[2], obj.read_typetree(self.nodes(sc[0], full))
        except Exception:
            return sc[2], None

    # ---- jerarquia ----
    def go(self, obj):
        """GameObject con sus componentes leidos (los MonoBehaviour por clase)."""
        t = obj.read_typetree()
        g = {'obj': obj, 'name': t['m_Name'], 'active': bool(t['m_IsActive']), 'layer': t['m_Layer'],
             'tag': self.tag_name(t['m_Tag']), 'mb': {}, 'comp': {}, 'children': []}
        for c in t['m_Component']:
            co = self.resolve(obj.assets_file, c['component'])
            if co is None:
                continue
            tn = co.type.name
            if tn == 'MonoBehaviour':
                cl, d = self.mono(co)
                if cl:
                    g['mb'][cl] = d
            else:
                try:
                    g['comp'].setdefault(tn, co.read_typetree())
                except Exception:
                    pass
        tr = g['comp'].get('Transform') or g['comp'].get('RectTransform')
        g['tr'] = tr
        for ch in (tr['m_Children'] if tr else []):
            cto = self.resolve(obj.assets_file, ch)
            if cto is None:
                continue
            gp = cto.read_typetree()['m_GameObject']
            gobj = self.resolve(cto.assets_file, gp)
            if gobj is not None:
                g['children'].append(self.go(gobj))
        return g
