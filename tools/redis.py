import pefile,capstone,sys,json,re
class Img:
    def __init__(s,path):
        s.p=pefile.PE(path); s.base=s.p.OPTIONAL_HEADER.ImageBase; s.d=s.p.get_memory_mapped_image()
        s.md=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_32); s.md.detail=False
    def b(s,va,n): return s.d[va-s.base:va-s.base+n]
    def u32(s,va): import struct; return struct.unpack('<I',s.b(va,4))[0]
    def cstr(s,va):
        if not (s.base<=va<s.base+len(s.d)): return None
        o=va-s.base; e=s.d.find(b'\0',o,o+200)
        if e<0: return None
        t=s.d[o:e]
        if len(t)>=3 and all(32<=c<127 or c in (9,10) for c in t): return t.decode()
    def func(s,va,maxn=3000,stop_ret=True):
        out=[];
        for i in s.md.disasm(s.b(va,maxn*8),va):
            out.append(i)
            if len(out)>=maxn: break
            if stop_ret and i.mnemonic in('ret','jmp') and i.mnemonic=='ret': break
        return out
    def strs(s,ins):
        r=[]
        for i in ins:
            for m in re.findall(r'0x[0-9a-f]{6,8}',i.op_str):
                t=s.cstr(int(m,16))
                if t: r.append(t)
        return r
    def show(s,va,n=200):
        for i in s.func(va,n):
            c=''
            for m in re.findall(r'0x[0-9a-f]{6,8}',i.op_str):
                t=s.cstr(int(m,16))
                if t: c+='  ; "'+t[:60].replace('\n','\n')+'"'
            print(f"{i.address:08x}  {i.mnemonic} {i.op_str}{c}")

def find_str(img,s):
    i=img.d.find(s.encode()+b'\0'); return img.base+i if i>=0 else None
def find_imm_refs(img,va,lo=None,hi=None):
    import struct
    pat=struct.pack('<I',va); res=[]; o=0
    while True:
        o=img.d.find(pat,o)
        if o<0: break
        res.append(img.base+o); o+=1
    return res
def func_start(img,va,maxback=0x4000):
    # heuristic: look back for CC/90 padding followed by function start
    o=va-img.base
    for k in range(o,o-maxback,-1):
        if img.d[k-1] in (0xcc,0xc3,0x90) and (k%16==0) : return img.base+k
    return None

def find_calls(img,target,start=None,end=None):
    import struct
    res=[]; d=img.d
    t0=0x1000; t1=0x1000+0x18c4000 if start is None else end
    o=t0
    while True:
        o=d.find(b'\xe8',o,t1)
        if o<0: break
        rel=struct.unpack('<i',d[o+1:o+5])[0]
        if img.base+o+5+rel==target: res.append(img.base+o)
        o+=1
    return res
