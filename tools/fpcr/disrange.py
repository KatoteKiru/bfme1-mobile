# Disassemble arm64 .text range (deps: pyelftools, capstone). Usage: disrange.py lib.so 0xSTART 0xEND
import sys
from elftools.elf.elffile import ELFFile
import capstone
md=capstone.Cs(capstone.CS_ARCH_ARM64, capstone.CS_MODE_ARM)
p,a,b=sys.argv[1],int(sys.argv[2],16),int(sys.argv[3],16)
e=ELFFile(open(p,'rb')); t=e.get_section_by_name('.text'); d=t.data(); base=t['sh_addr']
for i in md.disasm(d[a-base:b-base], a): print(hex(i.address), i.mnemonic, i.op_str)
