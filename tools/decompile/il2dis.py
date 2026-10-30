"""Desensamblado anotado de metodos IL2CPP (ARM64): llamadas a otros metodos, campos del objeto, cadenas y tipos.

    IL2CPP_METADATA=... IL2CPP_SO=... python3 il2dis.py 'SnakeController::HandleMovement' [FULL=1 para todo]
Es lo que se uso para leer la logica original (docs/DESCOMPILACION.md).
"""
import sys, os, pickle
sys.argv_saved=sys.argv; sys.argv=['x']
exec(open(os.path.dirname(os.path.abspath(__file__))+'/il2cpp.py').read().split('if __name__')[0])
sys.argv=sys.argv_saved
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM
def find_mr():
    # Il2CppMetadataRegistration: fieldOffsetsCount y typeDefinitionsSizesCount valen el numero de tipos
    pat=struct.pack('<Q',len(TYPES)); o=SO.find(pat)
    while o>=0:
        if o%8==0 and SO[o+16:o+24]==pat: return o2v(o)-0x50
        o=SO.find(pat,o+1)
    raise SystemExit('no se encuentra Il2CppMetadataRegistration')
MR=find_mr()
def q(v): return struct.unpack_from('<Q',SO,v2o(v))[0]
def mr(i): return ptr(MR+8*i)
TYPES_PTR=mr(7); NTYPES=q(MR+6*8)
FOFF=mr(11)
SPECS=table('methodSpecs','<iii') if 'methodSpecs' in SEC else []
def il2type(i):
    p=ptr(TYPES_PTR+8*i); data=q(p) if p not in REL else ptr(p); bits=struct.unpack_from('<I',SO,v2o(p+8))[0]
    return data,(bits>>16)&0xff
PRIM={1:'void',2:'bool',3:'char',4:'sbyte',5:'byte',6:'short',7:'ushort',8:'int',9:'uint',10:'long',11:'ulong',12:'float',13:'double',14:'string',24:'IntPtr',28:'object'}
def typestr(i,depth=0):
    data,t=il2type(i)
    if t in PRIM: return PRIM[t]
    if t in (17,18): return tname(data)
    if t==29: # szarray
        return 'arr' if depth>2 else '[]'
    if t==21: return 'generic'
    if t in(19,30): return 'T'
    return 't%d'%t
CACHE=os.environ.get('IL2CPP_CACHE','/tmp/il2cpp_methods.pkl')
if os.path.exists(CACHE):
    ADDR,TYPEINFO=pickle.load(open(CACHE,'rb'))
else:
    ADDR={}; TYPEINFO={}
    for im in IMAGES:
        nm=s(im[0])
        try: mp=module_methods(nm)
        except Exception: continue
        for ti in range(im[2],im[2]+im[3]):
            t=TYPES[ti]; tn=tname(ti)
            fl=[]
            fo=ptr(FOFF+8*ti) if True else 0
            for k in range(t[18]):
                fd=FIELDS[t[8]+k]
                off=None
                if fo:
                    try: off=struct.unpack_from('<i',SO,v2o(fo+4*k))[0]
                    except Exception: pass
                fl.append((s(fd[0]),off,fd[1]))
            mlist=[]
            for k in range(t[16]):
                m=METHODS[t[9]+k]; rid=m[6]&0xffffff
                a=mp[rid-1] if 0<rid<=len(mp) else 0
                if a: ADDR.setdefault(a,tn+'::'+s(m[0]))
                mlist.append((s(m[0]),a,[s(PARAMS[m[4]+j][0]) for j in range(m[10])]))
            TYPEINFO[tn]=(ti,fl,mlist)
    pickle.dump((ADDR,TYPEINFO),open(CACHE,'wb'))
SORTED=sorted(ADDR)
def usage(v):
    if not v&1: return None
    ut=(v>>29)&7; idx=(v>>1)&0x0FFFFFFF
    try:
        if ut==1: return 'TypeInfo '+typestr(idx)
        if ut==2: return 'Type '+typestr(idx)
        if ut==3: m=METHODS[idx]; return 'MethodDef %s::%s'%(tname(m[1]) if 0<=m[1]<len(TYPES) else '?',s(m[0]))
        if ut==4:
            fr=table('fieldRefs','<ii')[idx]; return 'FieldRef'
        if ut==5:
            ln,di=struct.unpack_from('<II',MD,SEC['stringLiteral'][0]+8*idx)
            o=SEC['stringLiteralData'][0]+di; return 'STR "%s"'%MD[o:o+ln].decode('utf8','replace')
        if ut==6: return 'MethodRef#%d'%idx
    except Exception as e: return 'usage?%d,%d'%(ut,idx)
    return 'usage %d %d'%(ut,idx)
def fields_of(cls):
    if cls not in TYPEINFO: return {}
    ti,fl,_=TYPEINFO[cls]
    return {off:n for n,off,_ in fl if off is not None}
COMPACT=os.environ.get('FULL')!='1'
DIE=''
THIS0=False
def disasm(addr, cls=None, maxn=3000):
    i=bisect.bisect_right(SORTED,addr)
    end=SORTED[i] if i<len(SORTED) else addr+0x400
    end=min(end, addr+4*maxn)
    code=SO[v2o(addr):v2o(addr)+(end-addr)]
    md=Cs(CS_ARCH_ARM64,CS_MODE_ARM)
    fmap=fields_of(cls) if cls else {}
    adrp={}
    out=[]
    for ins in md.disasm(code,addr):
        c=''
        ops=ins.op_str
        if ins.mnemonic=='adrp':
            r,imm=ops.split(', #'); adrp[r]=(int(imm,16),ins.address)
        elif ins.mnemonic in('ldr','add','ldrb','str') and '[' in ops or ins.mnemonic=='add':
            pass
        if ins.mnemonic in ('ldr','add') :
            parts=[p.strip(' []!') for p in ops.replace('[',',').replace(']',',').split(',') if p.strip(' []!')]
            if len(parts)>=3 and parts[1] in adrp and parts[2].startswith('#') and ins.address-adrp[parts[1]][1]<=24:
                tgt=adrp[parts[1]][0]+int(parts[2][1:],16)
                if ins.mnemonic=='ldr':
                    try:
                        v=ptr(tgt) if tgt in REL else q(tgt)
                        u=usage(v)
                        if not u and v not in ADDR:
                            try:
                                v2=ptr(v) if v in REL else q(v); u=usage(v2)
                                if not u and v2 in ADDR: u='&'+ADDR[v2]
                            except Exception: pass
                        c=u or ('-> %s'%ADDR.get(v,hex(v)))
                    except Exception: c='[%x]'%tgt
                else:
                    c='&%x %s'%(tgt, ADDR.get(tgt,''))
        if ins.mnemonic in ('bl','b') and ops.startswith('#'):
            t=int(ops[1:],16)
            if t in ADDR: c=ADDR[t]
            elif ins.mnemonic=='bl':
                c=thunk(t)
        if not c and fmap and '#0x' in ops and ('[x19' in ops or (THIS0 and '[x0' in ops)):
            try:
                off=int(ops.split('#0x')[1].split(']')[0].split('!')[0],16)
                if off in fmap: c=(c+' ' if c else '')+'.'+fmap[off]
            except Exception: pass
        if COMPACT:
            if ins.mnemonic=='bl' and c.startswith('->') and '#0x1f' in c: continue
            if ins.mnemonic=='bl' and not c: continue
            if 'TypeInfo' in c and ins.mnemonic=='ldr' and 'x0, [x0' in ops: continue
            if 'MethodRef' in c or (c.startswith('STR') and ops.startswith('x0, [x0')) : continue
            keep = c or ins.mnemonic in ('fcmp','cmp','fmov','ret') or ins.mnemonic.startswith('b.') or ins.mnemonic in ('cbz','cbnz','tbz','tbnz','b','csel','cset','fcsel')
            if not keep: continue
            if ins.mnemonic in ('cbz','cbnz') and ops.endswith(DIE): continue
        out.append('%08x  %-6s %-34s %s'%(ins.address,ins.mnemonic,ops,('; '+c) if c else ''))
    return '\n'.join(out)
_th={}
def thunk(t):
    # funciones envoltorio: suelen ser un salto a otro metodo o una funcion de runtime de il2cpp
    if t in _th: return _th[t]
    r=''
    try:
        md=Cs(CS_ARCH_ARM64,CS_MODE_ARM)
        for ins in md.disasm(SO[v2o(t):v2o(t)+16],t):
            if ins.mnemonic=='b' and ins.op_str.startswith('#'):
                r='-> '+ADDR.get(int(ins.op_str[1:],16),ins.op_str); break
    except Exception: pass
    _th[t]=r; return r
def find(cls, meth):
    ti,fl,ml=TYPEINFO[cls]
    return [a for n,a,_ in ml if n==meth]
if __name__=='__main__':
  for arg in sys.argv[1:]:
    cls,meth=arg.split('::')
    for a in find(cls,meth)[:3]:
        print('==== %s::%s @%x'%(cls,meth,a))
        print(disasm(a,cls))
        print()
