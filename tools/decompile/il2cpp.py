"""Lector minimo de IL2CPP (metadata v31 + libil2cpp.so arm64): nombres de clases, campos, metodos y direcciones.

    IL2CPP_METADATA=.../global-metadata.dat IL2CPP_SO=.../libil2cpp.so python3 il2cpp.py Assembly-CSharp.dll
Escribe cada clase con sus campos y la direccion de cada metodo en libil2cpp.so (como Il2CppDumper).
"""
import struct, bisect, sys, json
from elftools.elf.elffile import ELFFile
import os
# rutas: variables de entorno IL2CPP_METADATA (global-metadata.dat) e IL2CPP_SO (libil2cpp.so de arm64)
MD=open(os.environ['IL2CPP_METADATA'],'rb').read()
SO_PATH=os.environ['IL2CPP_SO']
SO=open(SO_PATH,'rb').read()
H=struct.unpack_from('<2I62I',MD,0)
names='stringLiteral stringLiteralData string events properties methods parameterDefaultValues fieldDefaultValues fieldAndParameterDefaultValueData fieldMarshaledSizes parameters fields genericParameters genericParameterConstraints genericContainers nestedTypes interfaces vtableMethods interfaceOffsets typeDefinitions images assemblies fieldRefs referencedAssemblies attributeData attributeDataRange unresolvedIndirectCallParameterTypes unresolvedIndirectCallParameterRanges windowsRuntimeTypeNames windowsRuntimeStrings exportedTypeDefinitions'.split()
SEC={n:(H[2+2*i],H[3+2*i]) for i,n in enumerate(names)}
def s(idx):
    o=SEC['string'][0]+idx; e=MD.index(b'\0',o); return MD[o:e].decode('utf8','replace')
def table(n,fmt):
    o,sz=SEC[n]; st=struct.Struct(fmt); return [st.unpack_from(MD,o+i*st.size) for i in range(sz//st.size)]
METHODS=table('methods','<IiiIiiIHHHH')   # name,declType,retType,retParamTok,paramStart,genCont,token,flags,iflags,slot,pcount
TYPES=table('typeDefinitions','<iiiiiiiIiiiiiiii8HII')
FIELDS=table('fields','<Iii')
PARAMS=table('parameters','<IIi')
IMAGES=table('images','<IiIIiIiIii')
# tipos: (name,ns,...,fieldStart=8,methodStart=9, counts: method=16, prop=17, field=18)
def tname(i):
    t=TYPES[i]; n=s(t[0]); ns=s(t[1])
    if t[3]>=0: pass
    return (ns+'.' if ns else '')+n
# ELF: relocaciones relativas
elf=ELFFile(open(SO_PATH,'rb'))
REL={}
for sec in elf.iter_sections():
    if sec.name in ('.rela.dyn',):
        for r in sec.iter_relocations():
            if r['r_info_type']==1027: REL[r['r_offset']]=r['r_addend']
SEGS=[(seg['p_vaddr'],seg['p_offset'],seg['p_filesz']) for seg in elf.iter_segments() if seg['p_type']=='PT_LOAD']
def v2o(v):
    for va,off,sz in SEGS:
        if va<=v<va+sz: return v-va+off
    return None
def o2v(o):
    for va,off,sz in SEGS:
        if off<=o<off+sz: return o-off+va
    return None
def ptr(v):
    if v in REL: return REL[v]
    o=v2o(v); return struct.unpack_from('<Q',SO,o)[0]
# buscar los Il2CppCodeGenModule: puntero a "Assembly-CSharp.dll"
REVREL={}
for k,v in REL.items(): REVREL.setdefault(v,[]).append(k)
def find_module(name):
    o=SO.find(name.encode()+b'\0')
    while o>=0:
        va=o2v(o)
        for loc in REVREL.get(va,[]):
            cnt=struct.unpack_from('<Q',SO,v2o(loc+8))[0]
            if 0<cnt<200000: return loc
        o=SO.find(name.encode()+b'\0',o+1)
def module_methods(name):
    m=find_module(name)
    cnt=struct.unpack_from('<Q',SO,v2o(m+8))[0]
    arr=ptr(m+16)
    return [ptr(arr+8*i) for i in range(cnt)]
def image(name):
    for im in IMAGES:
        if s(im[0])==name: return im
def dump(img_name):
    im=image(img_name)
    mp=module_methods(img_name)
    out=[]
    for ti in range(im[2],im[2]+im[3]):
        t=TYPES[ti]
        tn=tname(ti)
        if t[3]>=0: tn='<nested>'+tn
        fl=[s(FIELDS[t[8]+k][0]) for k in range(t[18])] if t[8]>=0 else []
        ms=[]
        for k in range(t[16]):
            m=METHODS[t[9]+k]
            rid=m[6]&0xffffff
            addr=mp[rid-1] if 0<rid<=len(mp) else 0
            ps=[s(PARAMS[m[4]+j][0]) for j in range(m[10])]
            ms.append((s(m[0]),ps,addr))
        out.append((tn,fl,ms))
    return out
if __name__=='__main__':
    res=dump(sys.argv[1])
    for tn,fl,ms in res:
        print('class',tn); print('  fields:',', '.join(fl))
        for n,ps,a in ms: print('   0x%08x %s(%s)'%(a,n,', '.join(ps)))
