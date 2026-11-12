#!/usr/bin/env python3
"""Build hybrid bootable ISO: MBR + stage2 + kernel + ISO 9660 El Torito.
Compatible with Rufus USB writing and QEMU / VirtualBox CD-ROM (-cdrom).
"""
import struct, sys, pathlib

def elf_entry(data: bytes) -> int:
    if data[:4] != b'\x7fELF':
        return 0x10000C
    # ELF32 little-endian e_entry at 0x18
    return struct.unpack_from('<I', data, 24)[0]

def make_pvd(total_2k_sectors: int) -> bytes:
    pvd = bytearray(2048)
    pvd[0] = 1 # Primary Volume Descriptor
    pvd[1:6] = b'CD001'
    pvd[6] = 1 # Version
    pvd[8:40] = b'MYKERNEL'.ljust(32)
    pvd[40:72] = b'MYKERNEL_OS'.ljust(32)
    # Volume Space Size (both endian)
    struct.pack_into('<I', pvd, 80, total_2k_sectors)
    struct.pack_into('>I', pvd, 84, total_2k_sectors)
    # Volume Set Size = 1
    struct.pack_into('<H', pvd, 120, 1)
    struct.pack_into('>H', pvd, 122, 1)
    # Volume Sequence Number = 1
    struct.pack_into('<H', pvd, 124, 1)
    struct.pack_into('>H', pvd, 126, 1)
    # Logical Block Size = 2048
    struct.pack_into('<H', pvd, 128, 2048)
    struct.pack_into('>H', pvd, 130, 2048)
    # Path Table Size = 10
    struct.pack_into('<I', pvd, 132, 10)
    struct.pack_into('>I', pvd, 136, 10)
    # LBA of Path Table L = 20, M = 21
    struct.pack_into('<I', pvd, 140, 20)
    struct.pack_into('>I', pvd, 148, 21)

    # Root Directory Record (34 bytes at offset 156)
    root = bytearray(34)
    root[0] = 34 # Length of Directory Record
    root[1] = 0 # Extended attribute record length
    struct.pack_into('<I', root, 2, 22) # Location of extent (LBA 22)
    struct.pack_into('>I', root, 6, 22)
    struct.pack_into('<I', root, 10, 2048) # Data length
    struct.pack_into('>I', root, 14, 2048)
    root[25] = 2 # File flags: Directory
    root[32] = 1 # Length of file identifier
    root[33] = 0 # File identifier: 0x00 for root
    pvd[156:156 + 34] = root

    pvd[190:318] = b'MYKERNEL'.ljust(128)
    pvd[318:446] = b'MYKERNEL_PREPARER'.ljust(128)
    pvd[446:574] = b'MYKERNEL_APP'.ljust(128)
    pvd[881] = 1 # File structure version
    return bytes(pvd)

def make_boot_record(boot_catalog_sector: int) -> bytes:
    brvd = bytearray(2048)
    brvd[0] = 0 # Boot Record Volume Descriptor
    brvd[1:6] = b'CD001'
    brvd[6] = 1 # Version
    brvd[7:39] = b'EL TORITO SPECIFICATION'.ljust(32, b'\0')
    # Pointer to Boot Catalog (LBA in 2048-byte sectors)
    struct.pack_into('<I', brvd, 71, boot_catalog_sector)
    return bytes(brvd)

def make_terminator() -> bytes:
    term = bytearray(2048)
    term[0] = 255 # Volume Descriptor Set Terminator
    term[1:6] = b'CD001'
    term[6] = 1
    return bytes(term)

def make_boot_catalog(load_rba: int) -> bytes:
    cat = bytearray(2048)
    # Validation Entry (32 bytes)
    cat[0] = 1 # Header ID
    cat[1] = 0 # Platform ID (x86)
    cat[4:28] = b'MyKernel'.ljust(24, b'\0')
    cat[30] = 0x55
    cat[31] = 0xAA
    csum = 0
    for i in range(16):
        if i != 14: # checksum is word 14
            w = struct.unpack_from('<H', cat, i * 2)[0]
            csum = (csum + w) & 0xFFFF
    chk = (-csum) & 0xFFFF
    struct.pack_into('<H', cat, 28, chk)

    # Initial/Default Entry (32 bytes at offset 32)
    cat[32] = 0x88 # Bootable
    cat[33] = 0x04 # Boot media type = Hard Disk Emulation (512-byte sectors)
    struct.pack_into('<H', cat, 34, 0x0000) # Load segment (0 = default 0x7C0)
    cat[36] = 0x0C # System type (FAT32)
    struct.pack_into('<H', cat, 38, 1) # Sector count = 1
    struct.pack_into('<I', cat, 40, load_rba) # Load RBA = 0 (MBR at sector 0)
    return bytes(cat)

def main():
    if len(sys.argv) < 5:
        print("usage: mkiso.py kernel.elf mbr.bin stage2.bin out.iso [kernel.raw]")
        sys.exit(1)
    kpath, mbrpath, s2path, outpath = sys.argv[1:5]
    rawpath = sys.argv[5] if len(sys.argv) > 5 else None

    kelf = pathlib.Path(kpath).read_bytes()
    mbr = pathlib.Path(mbrpath).read_bytes()
    stage2 = pathlib.Path(s2path).read_bytes()

    if rawpath and pathlib.Path(rawpath).exists():
        kernel = pathlib.Path(rawpath).read_bytes()
    elif kelf[:4] == b'\x7fELF':
        kernel = kelf
    else:
        kernel = kelf

    entry_addr = elf_entry(kelf)
    size = 64 * 1024 * 1024 # 64 MB
    total_2k = size // 2048
    img = bytearray(size)

    # 1. Sector 0 (512 bytes): MBR
    m = bytearray(512)
    m[:min(446, len(mbr))] = mbr[:min(446, len(mbr))]
    m[446] = 0x80 # Bootable
    m[446 + 4] = 0x0C # FAT32 LBA
    struct.pack_into('<I', m, 446 + 8, 2048)
    struct.pack_into('<I', m, 446 + 12, 65536)
    m[510], m[511] = 0x55, 0xAA
    img[0:512] = m

    # 2. Stage2 at LBA 1 (offset 512)
    s2 = stage2.ljust(32 * 512, b'\0')[:32 * 512]
    img[512:512 + len(s2)] = s2

    # 3. ISO 9660 descriptors (in 2048-byte sectors)
    # Sector 16: PVD
    img[16 * 2048:17 * 2048] = make_pvd(total_2k)
    # Sector 17: Boot Record
    img[17 * 2048:18 * 2048] = make_boot_record(19)
    # Sector 18: Terminator
    img[18 * 2048:19 * 2048] = make_terminator()
    # Sector 19: Boot Catalog (El Torito)
    img[19 * 2048:20 * 2048] = make_boot_catalog(0)

    # 4. FAT32 BPB at LBA 2048
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

    # 5. Kernel metadata at LBA 8191
    ksects = (len(kernel) + 511) // 512
    meta = bytearray(512)
    struct.pack_into('<I', meta, 0, 0x4B524E4C)  # KRNL
    struct.pack_into('<I', meta, 4, ksects)
    struct.pack_into('<I', meta, 8, entry_addr)
    img[8191 * 512:8191 * 512 + 512] = meta

    # 6. Kernel raw binary at LBA 8192
    img[8192 * 512:8192 * 512 + len(kernel)] = kernel

    pathlib.Path(outpath).write_bytes(img)
    print(f"mykernel.iso: {size} bytes ({total_2k} 2K sectors), kernel {len(kernel)} B ({ksects} sec) @ LBA8192 entry={hex(entry_addr)}")

if __name__ == '__main__':
    main()
