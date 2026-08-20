#!/usr/bin/env python3
"""Build bootable disk.img: MBR + stage2 + flat kernel @ LBA 8192."""
import struct, sys, pathlib

def elf_entry(data: bytes) -> int:
    if data[:4] != b'\x7fELF':
        return 0x10000C
    # ELF32 little-endian e_entry at 0x18
    return struct.unpack_from('<I', data, 24)[0]

def main():
    if len(sys.argv) < 5:
        print("usage: mkdisk.py kernel.elf mbr.bin stage2.bin out.img [kernel.raw]")
        sys.exit(1)
    kpath, mbrpath, s2path, outpath = sys.argv[1:5]
    rawpath = sys.argv[5] if len(sys.argv) > 5 else None

    kelf = pathlib.Path(kpath).read_bytes()
    mbr = pathlib.Path(mbrpath).read_bytes()
    stage2 = pathlib.Path(s2path).read_bytes()

    if rawpath and pathlib.Path(rawpath).exists():
        kernel = pathlib.Path(rawpath).read_bytes()
    elif kelf[:4] == b'\x7fELF':
        print("WARN: embedding ELF as raw — prefer objcopy -O binary")
        kernel = kelf
    else:
        kernel = kelf

    entry_addr = elf_entry(kelf)
    size = 64 * 1024 * 1024
    img = bytearray(size)

    m = bytearray(512)
    m[:min(446, len(mbr))] = mbr[:min(446, len(mbr))]
    m[446] = 0x80
    m[446 + 4] = 0x0C
    struct.pack_into('<I', m, 446 + 8, 2048)
    struct.pack_into('<I', m, 446 + 12, 65536)
    m[510], m[511] = 0x55, 0xAA
    img[0:512] = m

    s2 = stage2.ljust(32 * 512, b'\0')[:32 * 512]
    img[512:512 + len(s2)] = s2

    ksects = (len(kernel) + 511) // 512
    meta = bytearray(512)
    struct.pack_into('<I', meta, 0, 0x4B524E4C)  # KRNL
    struct.pack_into('<I', meta, 4, ksects)
    struct.pack_into('<I', meta, 8, entry_addr)
    img[8191 * 512:8191 * 512 + 512] = meta
    img[8192 * 512:8192 * 512 + len(kernel)] = kernel

    bpb = bytearray(512)
    bpb[0:3] = b'\xEB\x58\x90'
    bpb[3:11] = b'MSWIN4.1'
    struct.pack_into('<H', bpb, 11, 512)
    bpb[13] = 8
    struct.pack_into('<H', bpb, 14, 32)
    bpb[16] = 2
    struct.pack_into('<I', bpb, 32, 65536)
    struct.pack_into('<I', bpb, 36, 256)
    struct.pack_into('<I', bpb, 44, 2)
    bpb[66] = 0x29
    bpb[71:82] = b'MYKERNEL   '
    bpb[82:90] = b'FAT32   '
    bpb[510], bpb[511] = 0x55, 0xAA
    img[2048 * 512:2048 * 512 + 512] = bpb

    pathlib.Path(outpath).write_bytes(img)
    print(f"disk.img: {size} bytes, kernel {len(kernel)} B ({ksects} sec) @ LBA8192 entry={hex(entry_addr)}")

if __name__ == '__main__':
    main()
