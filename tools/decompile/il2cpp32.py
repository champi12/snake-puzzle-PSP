"""Lector minimo de IL2CPP para libil2cpp.so de 32 bits (armeabi-v7a), metadata v31.

La version 14.2.9 de Snake Puzzle solo trae armeabi-v7a (la 2.3.4 traia ARM64: ver il2cpp.py e il2dis.py).

    IL2CPP_METADATA=.../global-metadata.dat IL2CPP_SO=.../libil2cpp.so python3 il2cpp32.py 'Clase::Metodo' ...
Desensambla cada metodo (ARM o Thumb segun la direccion) anotando llamadas, campos y cadenas.
"""
import bisect
import os
import pickle
import struct
import sys

from elftools.elf.elffile import ELFFile

MD = open(os.environ['IL2CPP_METADATA'], 'rb').read()
SO_PATH = os.environ['IL2CPP_SO']
SO = open(SO_PATH, 'rb').read()
H = struct.unpack_from('<2I62I', MD, 0)
NAMES = ('stringLiteral stringLiteralData string events properties methods parameterDefaultValues fieldDefaultValues '
         'fieldAndParameterDefaultValueData fieldMarshaledSizes parameters fields genericParameters '
         'genericParameterConstraints genericContainers nestedTypes interfaces vtableMethods interfaceOffsets '
         'typeDefinitions images assemblies fieldRefs referencedAssemblies attributeData attributeDataRange '
         'unresolvedIndirectCallParameterTypes unresolvedIndirectCallParameterRanges windowsRuntimeTypeNames '
         'windowsRuntimeStrings exportedTypeDefinitions').split()
SEC = {n: (H[2 + 2 * i], H[3 + 2 * i]) for i, n in enumerate(NAMES)}


def s(idx):
    o = SEC['string'][0] + idx
    return MD[o:MD.index(b'\0', o)].decode('utf8', 'replace')


def table(n, fmt):
    o, sz = SEC[n]
    st = struct.Struct(fmt)
    return [st.unpack_from(MD, o + i * st.size) for i in range(sz // st.size)]


METHODS = table('methods', '<IiiIiiIHHHH')
TYPES = table('typeDefinitions', '<iiiiiiiIiiiiiiii8HII')
FIELDS = table('fields', '<Iii')
PARAMS = table('parameters', '<IIi')
IMAGES = table('images', '<IiIIiIiIii')
FIELD_DEFAULTS = table('fieldDefaultValues', '<iii')


def tname(i):
    t = TYPES[i]
    ns = s(t[1])
    return (ns + '.' if ns else '') + s(t[0])


def string_literal(idx):
    ln, di = struct.unpack_from('<II', MD, SEC['stringLiteral'][0] + 8 * idx)
    o = SEC['stringLiteralData'][0] + di
    return MD[o:o + ln].decode('utf8', 'replace')


elf = ELFFile(open(SO_PATH, 'rb'))
SEGS = [(g['p_vaddr'], g['p_offset'], g['p_filesz']) for g in elf.iter_segments() if g['p_type'] == 'PT_LOAD']


def v2o(v):
    for va, off, sz in SEGS:
        if va <= v < va + sz:
            return v - va + off
    return None


def o2v(o):
    for va, off, sz in SEGS:
        if off <= o < off + sz:
            return o - off + va
    return None


def u32(v):
    o = v2o(v)
    return struct.unpack_from('<I', SO, o)[0] if o is not None else None


def find_module(name):
    """Il2CppCodeGenModule {const char *name; uint32 count; Il2CppMethodPointer *methods; ...}"""
    o = SO.find(name.encode() + b'\0')
    while o >= 0:
        va = struct.pack('<I', o2v(o))
        p = SO.find(va)
        while p >= 0:
            if p % 4 == 0:
                cnt = struct.unpack_from('<I', SO, p + 4)[0]
                arr = struct.unpack_from('<I', SO, p + 8)[0]
                if 0 < cnt < 200000 and v2o(arr) is not None:
                    return o2v(p)
            p = SO.find(va, p + 1)
        o = SO.find(name.encode() + b'\0', o + 1)
    return None


def module_methods(name):
    m = find_module(name)
    if m is None:
        return []
    cnt = u32(m + 4)
    arr = u32(m + 8)
    return [u32(arr + 4 * i) for i in range(cnt)]


def find_mr():
    """Il2CppMetadataRegistration (32 bits): fieldOffsetsCount (+0x28) y typeDefinitionsSizesCount (+0x30)"""
    pat = struct.pack('<I', len(TYPES))
    o = SO.find(pat)
    while o >= 0:
        if o % 4 == 0 and SO[o + 8:o + 12] == pat:
            mr = o2v(o) - 0x28
            if mr is not None:
                tc = u32(mr + 6 * 4)
                if tc and 1000 < tc < 400000:
                    return mr
        o = SO.find(pat, o + 1)
    raise SystemExit('no se encuentra Il2CppMetadataRegistration')


MR = find_mr()
TYPES_PTR = u32(MR + 7 * 4)
FOFF = u32(MR + 11 * 4)
PRIM = {1: 'void', 2: 'bool', 3: 'char', 4: 'sbyte', 5: 'byte', 6: 'short', 7: 'ushort', 8: 'int', 9: 'uint',
        10: 'long', 11: 'ulong', 12: 'float', 13: 'double', 14: 'string', 24: 'IntPtr', 28: 'object'}


def typestr(i):
    p = u32(TYPES_PTR + 4 * i)
    data = u32(p)
    t = (u32(p + 4) >> 16) & 0xff
    if t in PRIM:
        return PRIM[t]
    if t in (17, 18):
        return tname(data)
    if t == 29:
        return '[]'
    return 't%d' % t


CACHE = os.environ.get('IL2CPP_CACHE', '/tmp/il2cpp32_methods.pkl')
if os.path.exists(CACHE):
    ADDR, TYPEINFO = pickle.load(open(CACHE, 'rb'))
else:
    ADDR, TYPEINFO = {}, {}
    for im in IMAGES:
        nm = s(im[0])
        mp = module_methods(nm)
        for ti in range(im[2], im[2] + im[3]):
            t = TYPES[ti]
            tn = tname(ti)
            fl = []
            fo = u32(FOFF + 4 * ti)
            for k in range(t[18]):
                fd = FIELDS[t[8] + k]
                off = None
                if fo:
                    try:
                        off = struct.unpack_from('<i', SO, v2o(fo + 4 * k))[0]
                    except Exception:
                        pass
                fl.append((s(fd[0]), off, fd[1], t[8] + k))
            ml = []
            for k in range(t[16]):
                m = METHODS[t[9] + k]
                rid = m[6] & 0xffffff
                a = mp[rid - 1] if 0 < rid <= len(mp) else 0
                if a:
                    ADDR.setdefault(a & ~1, tn + '::' + s(m[0]))
                ml.append((s(m[0]), a, [s(PARAMS[m[4] + j][0]) for j in range(m[10])]))
            TYPEINFO[tn] = (ti, fl, ml)
    pickle.dump((ADDR, TYPEINFO), open(CACHE, 'wb'))
SORTED = sorted(ADDR)


def usage(v):
    """valor inicial de una variable de metadatos de IL2CPP (se resuelve en tiempo de ejecucion)"""
    if v is None or not v & 1:
        return None
    ut = (v >> 29) & 7
    idx = (v >> 1) & 0x0FFFFFFF
    try:
        if ut == 1:
            return 'TypeInfo ' + typestr(idx)
        if ut == 2:
            return 'Type ' + typestr(idx)
        if ut == 3:
            m = METHODS[idx]
            return 'MethodDef %s::%s' % (tname(m[1]), s(m[0]))
        if ut == 4:
            return 'FieldRef#%d' % idx
        if ut == 5:
            return 'STR "%s"' % string_literal(idx)
        if ut == 6:
            return 'MethodRef#%d' % idx
    except Exception:
        return None
    return None


def fields_of(cls):
    if cls not in TYPEINFO:
        return {}
    return {off: n for n, off, _, _ in TYPEINFO[cls][1] if off is not None}


def static_fields_of(cls):
    """los campos estaticos tienen el offset dentro del bloque de estaticos (los dos mapas se mezclan)"""
    return fields_of(cls)


def describe_ptr(v):
    if v is None:
        return None
    u = usage(v)
    if u:
        return u
    if (v & ~1) in ADDR:
        return '&' + ADDR[v & ~1]
    w = u32(v) if v2o(v) is not None else None
    u = usage(w)
    if u:
        return u
    if w is not None and (w & ~1) in ADDR:
        return '&' + ADDR[w & ~1]
    return None


def disasm(addr, cls=None, maxn=4000, full=False):
    from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM, CS_MODE_THUMB
    thumb = addr & 1
    addr &= ~1
    i = bisect.bisect_right(SORTED, addr)
    end = SORTED[i] if i < len(SORTED) else addr + 0x400
    end = min(end, addr + 4 * maxn)
    code = SO[v2o(addr):v2o(addr) + (end - addr)]
    md = Cs(CS_ARCH_ARM, CS_MODE_THUMB if thumb else CS_MODE_ARM)
    md.detail = False
    fmap = fields_of(cls) if cls else {}
    reg = {}
    out = []
    pcoff = 4 if thumb else 8
    for ins in md.disasm(code, addr):
        c = ''
        ops = ins.op_str
        mn = ins.mnemonic
        parts = [p.strip(' []!') for p in ops.replace('[', ',').replace(']', ',').split(',') if p.strip(' []!')]
        pcval = ((ins.address + pcoff) & ~3) if thumb else ins.address + pcoff
        try:
            if mn.startswith('ldr') and len(parts) >= 2 and parts[1] == 'pc' and (len(parts) == 2 or parts[2].startswith('#')):
                off = int(parts[2][1:], 0) if len(parts) > 2 else 0
                val = u32(pcval + off)
                reg[parts[0]] = val
                d = describe_ptr(val)
                if d:
                    c = d
            elif mn in ('movw',) and parts[1].startswith('#'):
                reg[parts[0]] = int(parts[1][1:], 0)
            elif mn == 'movt' and parts[0] in reg and parts[1].startswith('#'):
                reg[parts[0]] = (reg[parts[0]] & 0xffff) | (int(parts[1][1:], 0) << 16)
            elif mn.startswith('add') and len(parts) == 3 and parts[1] == 'pc' and parts[2] in reg:
                v = (ins.address + pcoff + reg[parts[2]]) & 0xffffffff
                reg[parts[0]] = v
                c = describe_ptr(v) or ''
            elif mn.startswith('add') and len(parts) == 2 and parts[1] == 'pc' and parts[0] in reg:
                v = (ins.address + pcoff + reg[parts[0]]) & 0xffffffff
                reg[parts[0]] = v
                c = describe_ptr(v) or ''
            elif mn.startswith('ldr') and len(parts) == 3 and parts[1] == 'pc' and parts[2] in reg:
                a = (ins.address + pcoff + reg[parts[2]]) & 0xffffffff
                v = u32(a)
                reg[parts[0]] = v
                c = describe_ptr(v) or describe_ptr(a) or ''
            elif mn.startswith('ldr') and len(parts) == 3 and parts[1] in reg and parts[2].startswith('#') and not parts[1].startswith('s'):
                pass
            elif mn.startswith('vmov') and '#' in ops:
                pass
            else:
                if parts and parts[0] in reg and mn not in ('cmp', 'tst', 'str', 'strb', 'push', 'vstr') and not mn.startswith('b'):
                    reg.pop(parts[0], None)
        except Exception:
            pass
        if mn in ('bl', 'blx', 'b', 'b.w', 'bl.w') and ops.startswith('#'):
            t = int(ops[1:], 16)
            if t in ADDR:
                c = ADDR[t]
            elif mn.startswith('bl'):
                c = thunk(t, mn == 'blx' and not thumb or (thumb and mn != 'blx'))
        if not c and fmap and '#' in ops and '[r' in ops:
            try:
                off = int(ops.split('#')[1].split(']')[0], 0)
                if off in fmap and off >= 8:
                    c = '.' + fmap[off]
            except Exception:
                pass
        if not full:
            keep = c or mn in ('cmp', 'vcmp', 'vcmpe', 'bx', 'pop', 'movw', 'mov') and '#' in ops or mn.startswith('b') or mn.startswith('cb') or mn.startswith('it') or mn.startswith('vmov')
            if not keep:
                continue
        out.append('%08x  %-7s %-36s %s' % (ins.address, mn, ops, ('; ' + c) if c else ''))
    return '\n'.join(out)


_th = {}


def thunk(t, thumb):
    if t in _th:
        return _th[t]
    r = ''
    try:
        from capstone import Cs, CS_ARCH_ARM, CS_MODE_ARM, CS_MODE_THUMB
        md = Cs(CS_ARCH_ARM, CS_MODE_THUMB if thumb else CS_MODE_ARM)
        for ins in md.disasm(SO[v2o(t):v2o(t) + 16], t):
            if ins.mnemonic in ('b', 'b.w') and ins.op_str.startswith('#'):
                r = '-> ' + ADDR.get(int(ins.op_str[1:], 16), ins.op_str)
                break
    except Exception:
        pass
    _th[t] = r
    return r


def find(cls, meth):
    ti, fl, ml = TYPEINFO[cls]
    return [a for n, a, _ in ml if n == meth]


def field_default(field_index):
    """valor por defecto de un campo constante (const) o un array estatico inicializado"""
    for fi, ti, di in FIELD_DEFAULTS:
        if fi == field_index:
            return ti, SEC['fieldAndParameterDefaultValueData'][0] + di
    return None


if __name__ == '__main__':
    for arg in sys.argv[1:]:
        cls, meth = arg.split('::')
        for a in find(cls, meth)[:3]:
            print('==== %s::%s @%x%s' % (cls, meth, a & ~1, ' (thumb)' if a & 1 else ''))
            print(disasm(a, cls, full=os.environ.get('FULL') == '1'))
            print()
