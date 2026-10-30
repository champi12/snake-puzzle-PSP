"""Ejecuta funciones del juego original (ARM64 de libil2cpp.so) en un emulador de CPU (Unicorn) para sacar su
comportamiento exacto: las llamadas a Unity se sustituyen por funciones falsas que apuntan lo que el juego hace
(que sprite pone, con que giro y que espejos).

    IL2CPP_METADATA=... IL2CPP_SO=... python3 emu.py visuals

Sirve para portar 1 a 1 lo que es dificil de seguir leyendo el ensamblador (por ejemplo SnakeController.UpdateVisuals,
que decide la pieza de cada parte de la serpiente).
"""
import os
import struct
import sys

from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM, UC_HOOK_CODE, UC_HOOK_MEM_UNMAPPED, UcError
from unicorn.arm64_const import *

sys.argv_s = sys.argv
sys.argv = ['x']
__file__ = os.path.join(os.path.dirname(os.path.abspath(sys.argv_s[0])), 'il2dis.py')
exec(open(__file__).read().split("if __name__=='__main__':")[0])
sys.argv = sys.argv_s

HEAP, STACK, MAGIC = 0x60000000, 0x70000000, 0x7f000000


def f2i(x):
    return struct.unpack('<I', struct.pack('<f', x))[0]


def i2f(x):
    return struct.unpack('<f', struct.pack('<I', x & 0xffffffff))[0]


class Emu:
    def __init__(self):
        self.uc = uc = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
        uc.reg_write(UC_ARM64_REG_CPACR_EL1, 0x300000)   # FP/SIMD activado
        top = 0
        for va, off, sz in SEGS:
            top = max(top, va + sz)
        size = (top + 0x200000) & ~0xfff
        uc.mem_map(0, size)
        for va, off, sz in SEGS:
            uc.mem_write(va, SO[off:off + sz])
        for k, v in REL.items():
            uc.mem_write(k, struct.pack('<Q', v))
        uc.mem_map(HEAP, 0x1000000)
        uc.mem_map(STACK, 0x100000)
        uc.mem_map(MAGIC, 0x1000)
        self.heap = HEAP
        self.stubs = {}
        self.log = []
        uc.hook_add(UC_HOOK_CODE, self._code)
        uc.hook_add(UC_HOOK_MEM_UNMAPPED, self._bad)

    def alloc(self, n):
        a = self.heap
        self.heap += (n + 15) & ~15
        return a

    def w64(self, a, v):
        self.uc.mem_write(a, struct.pack('<Q', v))

    def w32(self, a, v):
        self.uc.mem_write(a, struct.pack('<I', v & 0xffffffff))

    def wf(self, a, v):
        self.uc.mem_write(a, struct.pack('<f', v))

    def r64(self, a):
        return struct.unpack('<Q', self.uc.mem_read(a, 8))[0]

    def fake_class(self, statics=b''):
        """Il2CppClass falso: +0xb8 los campos estaticos, +0xe0 'ya inicializada'."""
        c = self.alloc(0x200)
        st = self.alloc(max(len(statics), 16))
        self.uc.mem_write(st, statics)
        self.w64(c + 0xb8, st)
        self.w32(c + 0xe0, 1)
        return c

    def _bad(self, uc, access, addr, size, value, ud):
        pc = uc.reg_read(UC_ARM64_REG_PC)
        print('acceso a memoria no mapeada 0x%x en pc 0x%x (%s)' % (addr, pc, ADDR.get(SORTED[bisect.bisect_right(SORTED, pc) - 1], '?')))
        return False

    def _code(self, uc, addr, size, ud):
        if addr == MAGIC:
            uc.emu_stop()
            return
        f = self.stubs.get(addr)
        if f is None:
            return
        ret = f(self)
        if ret is not None:
            uc.reg_write(UC_ARM64_REG_X0, ret)
        uc.reg_write(UC_ARM64_REG_PC, uc.reg_read(UC_ARM64_REG_X30))

    def reg(self, r):
        return self.uc.reg_read(r)

    def sreg(self, n):
        return i2f(self.uc.reg_read(UC_ARM64_REG_S0 + n))

    def set_s(self, n, v):
        self.uc.reg_write(UC_ARM64_REG_S0 + n, f2i(v))

    def call(self, addr, args):
        uc = self.uc
        for i, v in enumerate(args):
            uc.reg_write(UC_ARM64_REG_X0 + i, v)
        uc.reg_write(UC_ARM64_REG_SP, STACK + 0xf0000)
        uc.reg_write(UC_ARM64_REG_X30, MAGIC)
        uc.mem_write(MAGIC, b'\x1f\x20\x03\xd5' * 4)   # nop
        uc.emu_start(addr, MAGIC + 4, count=2000000)


def slots_of(fn_addr):
    """Ranuras de metadatos (TypeInfo, MethodRef, cadenas) que lee la funcion: adrp + ldr."""
    from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM
    i = bisect.bisect_right(SORTED, fn_addr)
    end = SORTED[i] if i < len(SORTED) else fn_addr + 0x4000
    md = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
    adrp = {}
    out = {}
    for ins in md.disasm(SO[v2o(fn_addr):v2o(end)], fn_addr):
        if ins.mnemonic == 'adrp':
            r, imm = ins.op_str.split(', #')
            adrp[r] = int(imm, 16)
        elif ins.mnemonic in ('ldr', 'ldrb'):
            p = [x.strip(' []!') for x in ins.op_str.replace('[', ',').replace(']', ',').split(',') if x.strip(' []!')]
            if len(p) >= 3 and p[1] in adrp and p[2].startswith('#'):
                a = adrp[p[1]] + int(p[2][1:], 16)
                out[a] = ins.mnemonic
    return out


# ---------------------------------------------------------------- SnakeController.UpdateVisuals
DIRS = {'R': (1, 0), 'L': (-1, 0), 'U': (0, 1), 'D': (0, -1)}
SPRITE_FIELD = {0x10: 'tail', 0x18: 'horizontal', 0x20: 'vertical', 0x28: 'cornerTL(in_L)', 0x30: 'cornerTR(out_R)'}


def run_visuals(cells, head_dir, classic=0):
    """cells: posiciones de las partes (cabeza primero). Devuelve por parte: sprite, flipX, flipY, giro z."""
    e = Emu()
    fn = TYPEINFO['SnakeController'][2]
    addr = [a for n, a, _ in fn if n == 'UpdateVisuals'][0]
    # ranuras de metadatos: clases falsas con sus estaticos
    v2i = struct.pack('<12i', 0, 0, 1, 1, 0, 1, 0, -1, -1, 0, 1, 0)
    v2 = struct.pack('<12f', 0, 0, 1, 1, 0, 1, 0, -1, -1, 0, 1, 0)
    v3 = struct.pack('<24f', 0, 0, 0, 1, 1, 1, 0, 1, 0, 0, -1, 0, -1, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, -1)
    cls = {'Vector2Int': e.fake_class(v2i), 'Vector2': e.fake_class(v2), 'Vector3': e.fake_class(v3)}
    other = e.fake_class(b'\0' * 64)
    for a, kind in slots_of(addr).items():
        if kind == 'ldrb':
            e.uc.mem_write(a, b'\x01')         # guardas "metadatos ya inicializados"
            continue
        try:
            v = e.r64(a)
        except UcError:
            continue
        u = usage(v) or ''
        target = a
        if not u and 0x1000 < v < 0x5000000:
            # la ranura apunta a otra que guarda el metadato (asi lo usa el codigo: ldr x8,[ranura]; ldr x8,[x8])
            try:
                u = usage(e.r64(v)) or ''
                target = v
            except UcError:
                u = ''
        if 'TypeInfo' in u:
            name = u.split()[-1].split('.')[-1]
            e.w64(target, cls.get(name, other))
        elif u:
            e.w64(target, e.alloc(64))          # MethodRef, cadenas: punteros distintos
    # serpiente
    this = e.alloc(0x400)
    e.w32(this + 0x20, classic)
    sets = []
    for k in range(2):
        s = e.alloc(0x40)
        for off in SPRITE_FIELD:
            e.w64(s + off, 0x1000 + k * 0x100 + off)   # "sprites" identificables
        sets.append(s)
    e.w64(this + 0x48, sets[0])
    e.w64(this + 0x50, sets[1])
    dx, dy = DIRS[head_dir]
    e.wf(this + 0xc8, dx)
    e.wf(this + 0xcc, dy)
    parts = []
    for i, (x, y) in enumerate(cells):
        p = e.alloc(0x100)
        e.w32(p + 0x20, 1 if i % 2 else 2)           # colorSetIndex
        parts.append(p)
    lst = e.alloc(0x40)
    e.w32(lst + 0x18, len(parts))
    e.w64(this + 0xb8, lst)
    pos = {p: c for p, c in zip(parts, cells)}
    rec = {p: {} for p in parts}

    def get_item(e):
        return parts[e.reg(UC_ARM64_REG_X1)]

    def same(e):
        return e.reg(UC_ARM64_REG_X0)

    def op_eq(e):
        return 1 if e.reg(UC_ARM64_REG_X0) == e.reg(UC_ARM64_REG_X1) else 0

    def get_pos(e):
        x, y = pos[e.reg(UC_ARM64_REG_X0)]
        e.set_s(0, x)
        e.set_s(1, y)
        e.set_s(2, 0)

    def snap(e):
        pass   # devuelve lo mismo que recibe (s0, s1)

    def to_v2i(e):
        x, y = round(e.sreg(0)), round(e.sreg(1))
        return (x & 0xffffffff) | ((y & 0xffffffff) << 32)

    def recorder(key, kind):
        def f(e):
            o = e.reg(UC_ARM64_REG_X0)
            if kind == 'bool':
                v = e.reg(UC_ARM64_REG_X1) & 1
            elif kind == 'euler':
                v = round(e.sreg(2))
            else:
                v = SPRITE_FIELD.get(e.reg(UC_ARM64_REG_X1) & 0xff, hex(e.reg(UC_ARM64_REG_X1)))
                v += '' if e.reg(UC_ARM64_REG_X1) & 0x100 == 0 else ''
            rec.setdefault(o, {})[key] = v
        return f

    def ts(e):
        e.set_s(0, 1.0)

    S = lambda name: [a for a, n in ADDR.items() if n.endswith(name)]
    for a in S('Component::get_transform'):
        e.stubs[a] = same
    for a in S('Transform::get_position'):
        e.stubs[a] = get_pos
    for a in S('Object::op_Equality'):
        e.stubs[a] = op_eq
    for a in S('SpriteRenderer::set_flipX'):
        e.stubs[a] = recorder('flipX', 'bool')
    for a in S('SpriteRenderer::set_flipY'):
        e.stubs[a] = recorder('flipY', 'bool')
    for a in S('SpriteRenderer::set_sprite'):
        e.stubs[a] = recorder('sprite', 'spr')
    for a in S('Transform::set_eulerAngles'):
        e.stubs[a] = recorder('rotZ', 'euler')
    for a in S('Time::get_timeScale'):
        e.stubs[a] = ts
    e.stubs[S('SnakeController::SnapToGrid')[0]] = snap
    e.stubs[0x33c6f28] = get_item          # List<Transform>.get_Item
    e.stubs[0x226aa50] = same              # GetComponent<T>: la misma parte hace de todo
    e.stubs[0x20214b4] = to_v2i            # Vector2Int.RoundToInt
    e.call(addr, [this, 0, 0])
    return [rec[p] for p in parts]


if __name__ == '__main__':
    if sys.argv[1] == 'visuals':
        import itertools
        D = {'R': (1, 0), 'L': (-1, 0), 'U': (0, 1), 'D': (0, -1)}
        print('# pieza central: llega desde (cola -> parte) y sale hacia (parte -> cabeza)')
        for a, b in itertools.product('RLUD', repeat=2):
            if D[a] == tuple(-v for v in D[b]):
                continue
            # cola -> parte (direccion a) ; parte -> cabeza (direccion b)
            mid = (0, 0)
            tail = (mid[0] - D[a][0], mid[1] - D[a][1])
            head = (mid[0] + D[b][0], mid[1] + D[b][1])
            r = run_visuals([head, mid, tail], b)
            print('cola->parte %s, parte->cabeza %s: cabeza %s | parte %s | cola %s' % (a, b, r[0], r[1], r[2]))
