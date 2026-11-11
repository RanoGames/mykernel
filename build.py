#!/usr/bin/env python3
"""
Native Windows / Cross-platform build script for mykernel.
Builds the kernel, userland blobs, and boots in QEMU without needing WSL or MSYS2.
All build tools and outputs reside on D: to conserve space on C:.
"""

import os
import sys
import shutil
import subprocess
from pathlib import Path

ROOT_DIR = Path(__file__).resolve().parent
SRC_DIR = ROOT_DIR / "src"
USER_DIR = ROOT_DIR / "user"
BUILD_DIR = ROOT_DIR / "build"
BOOT_DIR = ROOT_DIR / "boot"

TOOLCHAIN_PATHS = [
    Path(r"D:\i686-elf-tools\bin"),
    Path(r"D:\toolchain\bin"),
    Path(r"C:\i686-elf-tools\bin"),
]

QEMU_PATHS = [
    Path(r"D:\qemu\qemu-system-i386.exe"),
    Path(r"C:\Program Files\qemu\qemu-system-i386.exe"),
]

def find_tool(name: str) -> str:
    p = shutil.which(name)
    if p:
        return p
    for t_dir in TOOLCHAIN_PATHS:
        candidate = t_dir / f"{name}.exe"
        if candidate.exists():
            return str(candidate)
        candidate = t_dir / name
        if candidate.exists():
            return str(candidate)
    return name

def find_qemu() -> str:
    p = shutil.which("qemu-system-i386")
    if p:
        return p
    for q in QEMU_PATHS:
        if q.exists():
            return str(q)
    return "qemu-system-i386"

def run_cmd(cmd, desc=None, cwd=None):
    if desc:
        print(f"[{desc}] {' '.join(str(c) for c in cmd)}")
    res = subprocess.run(cmd, cwd=cwd)
    if res.returncode != 0:
        print(f"ERROR: command failed with code {res.returncode}")
        sys.exit(res.returncode)

def main():
    action = sys.argv[1] if len(sys.argv) > 1 else "all"

    if action == "clean":
        if BUILD_DIR.exists():
            shutil.rmtree(BUILD_DIR)
            print("Cleaned build directory.")
        return

    # Check compiler
    gcc = find_tool("i686-elf-gcc")
    as_tool = find_tool("i686-elf-as")
    objcopy = find_tool("i686-elf-objcopy")
    ld_tool = find_tool("i686-elf-ld")

    has_cross = shutil.which(gcc) is not None or Path(gcc).exists()
    if not has_cross:
        print(f"Warning: {gcc} not found. Trying fallback to gcc...")
        gcc = find_tool("gcc")
        as_tool = find_tool("as")
        objcopy = find_tool("objcopy")
        ld_tool = find_tool("ld")

    print(f"Using compiler:  {gcc}")
    print(f"Using assembler: {as_tool}")
    print(f"Using objcopy:   {objcopy}")

    BUILD_DIR.mkdir(parents=True, exist_ok=True)

    cflags = [
        "-std=gnu99", "-ffreestanding", "-fno-stack-protector",
        "-fno-pic", "-fno-pie", "-fno-asynchronous-unwind-tables",
        "-Wall", "-Wextra", "-O2", f"-I{SRC_DIR}"
    ]
    user_cflags = [
        "-std=gnu99", "-ffreestanding", "-fno-stack-protector",
        "-fno-pic", "-fno-pie", "-fno-asynchronous-unwind-tables",
        "-Wall", "-Wextra", "-O2", f"-I{USER_DIR / 'include'}"
    ]
    user_pic_cflags = [
        "-std=gnu99", "-ffreestanding", "-fno-stack-protector",
        "-fPIC", "-fno-asynchronous-unwind-tables",
        "-Wall", "-Wextra", "-O2", f"-I{USER_DIR / 'include'}"
    ]

    extra_arch = []
    link_extra = []
    as_extra = []
    if "i686-elf" not in gcc:
        extra_arch = ["-m32"]
        link_extra = ["-no-pie", "-static"]
        as_extra = ["--32"]

    # 1. Compile User Lib & Executables
    print("\n--- Building User Space ---")
    user_lib_sources = ["stdio.c", "string.c", "malloc.c"]
    user_lib_objs = []
    for s in user_lib_sources:
        src = USER_DIR / "lib" / s
        obj = BUILD_DIR / f"user_{Path(s).stem}.o"
        user_lib_objs.append(obj)
        run_cmd([gcc] + user_cflags + extra_arch + ["-c", str(src), "-o", str(obj)], f"CC {s}")

    # hello.elf & blob
    hello_elf = BUILD_DIR / "hello.elf"
    run_cmd([gcc] + user_cflags + extra_arch + [
        "-T", str(USER_DIR / "linker.ld"), "-nostdlib", "-Wl,--build-id=none",
        "-o", str(hello_elf), str(USER_DIR / "hello.c")
    ] + [str(o) for o in user_lib_objs], "LINK hello.elf")
    # Must run objcopy with relative path 'build/hello.elf' so symbols match _binary_build_hello_elf_*
    run_cmd([objcopy, "-O", "elf32-i386", "-B", "i386", "-I", "binary", "build/hello.elf", "build/hello_blob.o"], "OBJCOPY hello_blob.o", cwd=ROOT_DIR)

    # libhello.so & blob
    libhello_so = BUILD_DIR / "libhello.so"
    run_cmd([gcc] + user_pic_cflags + extra_arch + [
        "-shared", "-nostdlib", "-Wl,--build-id=none",
        "-T", str(USER_DIR / "libhello.ld"), "-Wl,-soname,libhello.so",
        "-o", str(libhello_so), str(USER_DIR / "libhello.c")
    ], "LINK libhello.so")
    run_cmd([objcopy, "-O", "elf32-i386", "-B", "i386", "-I", "binary", "build/libhello.so", "build/libhello_blob.o"], "OBJCOPY libhello_blob.o", cwd=ROOT_DIR)

    # dynhello.elf & blob
    dynhello_elf = BUILD_DIR / "dynhello.elf"
    run_cmd([gcc] + user_pic_cflags + extra_arch + [
        "-nostdlib", "-Wl,--build-id=none",
        "-T", str(USER_DIR / "dynhello.ld"),
        "-Wl,-dynamic-linker,/lib/ld-mykernel.so",
        "-o", str(dynhello_elf), str(USER_DIR / "dynhello.c"), str(libhello_so)
    ], "LINK dynhello.elf")
    run_cmd([objcopy, "-O", "elf32-i386", "-B", "i386", "-I", "binary", "build/dynhello.elf", "build/dynhello_blob.o"], "OBJCOPY dynhello_blob.o", cwd=ROOT_DIR)

    # 2. Assemble Kernel ASM files
    print("\n--- Assembling Kernel Sources ---")
    asm_files = ["boot.s", "gdt_flush.s", "idt_load.s", "isr.s", "context_switch.s", "setjmp.s"]
    asm_objs = []
    for s in asm_files:
        src = SRC_DIR / s
        obj = BUILD_DIR / f"{Path(s).stem}.o"
        asm_objs.append(obj)
        run_cmd([as_tool] + as_extra + [str(src), "-o", str(obj)], f"AS {s}")

    # 3. Compile Kernel C files
    print("\n--- Compiling Kernel C Sources ---")
    c_files = [
        "kernel.c", "gdt.c", "serial.c", "vga.c", "idt.c", "irq.c",
        "keyboard.c", "shell.c", "fs.c", "vfs.c", "calc.c", "syscall.c",
        "elf.c", "process.c", "ata.c", "fat32.c", "part.c", "install.c",
        "kmalloc.c", "mlang.c", "timer.c", "sched.c", "dyld.c", "gfx.c",
        "mouse.c", "snake.c", "pong.c", "sound.c", "pci.c", "ac97.c",
        "vbe.c", "settings.c", "platform.c", "mkdraw.c", "mkdisp.c",
        "desktop.c", "power.c", "net.c"
    ]
    c_objs = []
    for c in c_files:
        src = SRC_DIR / c
        obj = BUILD_DIR / f"{Path(c).stem}.o"
        c_objs.append(obj)
        run_cmd([gcc] + cflags + extra_arch + ["-c", str(src), "-o", str(obj)], f"CC {c}")

    # 4. Link Kernel
    print("\n--- Linking Kernel ---")
    kernel_bin = BUILD_DIR / "mykernel.bin"
    boot_obj = BUILD_DIR / "boot.o"
    other_asm = [o for o in asm_objs if o != boot_obj]
    hello_blob = BUILD_DIR / "hello_blob.o"
    libhello_blob = BUILD_DIR / "libhello_blob.o"
    dynhello_blob = BUILD_DIR / "dynhello_blob.o"
    all_objs = [boot_obj] + other_asm + c_objs + [hello_blob, libhello_blob, dynhello_blob]

    link_cmd = [
        gcc, "-T", str(SRC_DIR / "linker.ld"),
        "-o", str(kernel_bin)
    ] + extra_arch + link_extra + [
        "-Wl,--build-id=none", "-ffreestanding", "-O2", "-nostdlib"
    ] + [str(o) for o in all_objs] + ["-lgcc"]

    run_cmd(link_cmd, "LINK mykernel.bin")
    print(f"\n[SUCCESS] Kernel built successfully: {kernel_bin} ({kernel_bin.stat().st_size} bytes)")

    # 5. Bootloader & disk image (optional / if requested)
    if action in ("disk", "all-disk"):
        print("\n--- Building Bootloader & disk.img ---")
        mbr_bin = BUILD_DIR / "mbr.bin"
        mbr_o = BUILD_DIR / "mbr.o"
        mbr_elf = BUILD_DIR / "mbr.elf"
        run_cmd([as_tool] + as_extra + [str(BOOT_DIR / "mbr.s"), "-o", str(mbr_o)], "AS mbr.s")
        run_cmd([ld_tool, "-m", "elf_i386", "-Ttext", "0x7C00", "-o", str(mbr_elf), str(mbr_o)], "LD mbr.elf")
        run_cmd([objcopy, "-O", "binary", str(mbr_elf), str(mbr_bin)], "OBJCOPY mbr.bin")
        mbr_data = mbr_bin.read_bytes()
        mbr_bin.write_bytes(mbr_data[:512].ljust(512, b"\x00"))

        stage2_bin = BUILD_DIR / "stage2.bin"
        stage2_o = BUILD_DIR / "stage2.o"
        stage2_elf = BUILD_DIR / "stage2.elf"
        run_cmd([as_tool] + as_extra + [str(BOOT_DIR / "stage2.s"), "-o", str(stage2_o)], "AS stage2.s")
        run_cmd([ld_tool, "-m", "elf_i386", "-Ttext", "0x7E00", "-o", str(stage2_elf), str(stage2_o)], "LD stage2.elf")
        run_cmd([objcopy, "-O", "binary", str(stage2_elf), str(stage2_bin)], "OBJCOPY stage2.bin")
        s2_data = stage2_bin.read_bytes()
        stage2_bin.write_bytes(s2_data[:16384].ljust(16384, b"\x00"))

        raw_bin = BUILD_DIR / "mykernel.raw"
        run_cmd([objcopy, "-O", "binary", str(kernel_bin), str(raw_bin)], "OBJCOPY mykernel.raw")

        disk_img = ROOT_DIR / "disk.img"
        run_cmd([sys.executable, str(BOOT_DIR / "mkdisk.py"), str(kernel_bin), str(mbr_bin), str(stage2_bin), str(disk_img), str(raw_bin)], "MKDISK disk.img")

    # 6. Run if requested
    if action in ("run", "run-disk"):
        qemu = find_qemu()
        print(f"\n--- Launching QEMU ({qemu}) ---")
        if action == "run-disk":
            disk_img = ROOT_DIR / "disk.img"
            qemu_cmd = [
                qemu, "-hda", str(disk_img), "-m", "128",
                "-vga", "std", "-global", "VGA.vgamem_mb=32", "-net", "nic,model=rtl8139", "-net", "user"
            ]
        else:
            qemu_cmd = [
                qemu, "-kernel", str(kernel_bin), "-m", "128",
                "-vga", "std", "-global", "VGA.vgamem_mb=32", "-net", "nic,model=rtl8139", "-net", "user",
                "-audiodev", "dsound,id=spk", "-machine", "pcspk-audiodev=spk"
            ]
        print(f"Command: {' '.join(qemu_cmd)}")
        subprocess.run(qemu_cmd)

if __name__ == "__main__":
    main()
