#!/bin/bash
set -e
cd "$(dirname "$0")"
mkdir -p ../build
as --32 mbr.s -o ../build/mbr.o
ld -m elf_i386 -Ttext 0x7C00 -o ../build/mbr.elf ../build/mbr.o
objcopy -O binary ../build/mbr.elf ../build/mbr.bin
# pad/truncate to 512
dd if=../build/mbr.bin of=../build/mbr.bin.pad bs=512 count=1 conv=sync 2>/dev/null
mv ../build/mbr.bin.pad ../build/mbr.bin

as --32 stage2.s -o ../build/stage2.o
ld -m elf_i386 -Ttext 0x7E00 -o ../build/stage2.elf ../build/stage2.o
objcopy -O binary ../build/stage2.elf ../build/stage2.bin
# pad to 32 sectors
python3 - <<'PY'
from pathlib import Path
b=Path('../build/stage2.bin').read_bytes()
b=b[:16384].ljust(16384,b'\0')
Path('../build/stage2.bin').write_bytes(b)
print('mbr',Path('../build/mbr.bin').stat().st_size,'stage2',len(b))
PY
