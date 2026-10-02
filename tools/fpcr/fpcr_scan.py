# Scan executable sections of arm64 ELF .so for MSR FPCR / MRS FPCR / MSR FPSR (deps: pyelftools). Usage: fpcr_scan.py lib1.so ...
import sys, array
from elftools.elf.elffile import ELFFile
for path in sys.argv[1:]:
    with open(path,'rb') as f:
        elf=ELFFile(f); total=0; hits={'msr fpcr':0,'mrs fpcr':0,'msr fpsr':0}
        for sec in elf.iter_sections():
            if not (sec['sh_flags'] & 0x4): continue
            d=sec.data(); d=d[:len(d)//4*4]
            a=array.array('I'); a.frombytes(d); total+=len(a)
            for w in a:
                k=w & 0xFFFFFFE0
                if k==0xD51B4400: hits['msr fpcr']+=1
                elif k==0xD53B4400: hits['mrs fpcr']+=1
                elif k==0xD51B4420: hits['msr fpsr']+=1
        print(path.split('/')[-1], 'words', total, hits)
