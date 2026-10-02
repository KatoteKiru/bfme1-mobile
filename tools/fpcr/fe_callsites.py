# List BL call sites of given imported functions (e.g. fesetround,fesetenv) with context (deps: pyelftools, capstone). Usage: fe_callsites.py lib.so fesetround,fegetround
import sys, struct, array
from elftools.elf.elffile import ELFFile
from elftools.elf.relocation import RelocationSection
import capstone
md = capstone.Cs(capstone.CS_ARCH_ARM64, capstone.CS_MODE_ARM); md.detail=True
path, names = sys.argv[1], sys.argv[2].split(',')
f=open(path,'rb'); elf=ELFFile(f)
# map GOT slot -> symbol via .rela.plt
got2sym={}
for sec in elf.iter_sections():
    if isinstance(sec, RelocationSection) and sec.name in ('.rela.plt',):
        st=elf.get_section(sec['sh_link'])
        for r in sec.iter_relocations():
            got2sym[r['r_offset']]=st.get_symbol(r['r_info_sym']).name
plt=elf.get_section_by_name('.plt'); pd=plt.data(); pb=plt['sh_addr']
# decode plt stubs: adrp x16; ldr x17,[x16,#off]; add; br
plt2sym={}
ins=list(md.disasm(pd,pb))
for i in range(len(ins)-1):
    a,b=ins[i],ins[i+1]
    if a.mnemonic=='adrp' and a.op_str.startswith('x16') and b.mnemonic=='ldr' and 'x17' in b.op_str:
        page=a.operands[1].imm; off=b.operands[1].mem.disp
        s=got2sym.get(page+off)
        if s: plt2sym[a.address]=s
want={a:s for a,s in plt2sym.items() if s in names}
print('plt targets:', {hex(a):s for a,s in want.items()})
text=elf.get_section_by_name('.text'); td=text.data(); tb=text['sh_addr']
arr=array.array('I'); arr.frombytes(td[:len(td)//4*4])
for idx,w in enumerate(arr):
    if (w>>26)==0b100101:  # BL
        imm=w & 0x3FFFFFF
        if imm & (1<<25): imm-= (1<<26)
        tgt=tb+idx*4+imm*4
        if tgt in want:
            s=max(0,idx-10)
            print('--- call', want[tgt], 'at', hex(tb+idx*4))
            for i in md.disasm(td[s*4:(idx+4)*4], tb+s*4):
                t=plt2sym.get(i.operands[0].imm) if i.mnemonic=='bl' and i.operands else None
                print(' ',hex(i.address), i.mnemonic, i.op_str, ('<'+t+'>') if t else '')
