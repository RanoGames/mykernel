# MyKernel x86_64 + Multiboot 2

## Status

Parallel **bring-up** tree (`src64/`), not a full port of the 32-bit kernel.

- Multiboot2 header (`0xE85250D6`)
- 32-bit stub → identity map 4 GiB (2 MiB pages) → long mode
- `kernel_main64(magic, info)` walks Multiboot2 tags
- VGA text proof of life

## Build

```bash
make kernel64
qemu-system-x86_64 -kernel build64/mykernel64.bin -m 256
```

Or GRUB2 `multiboot2 /boot/mykernel64.bin`.

## Migration path (full OS)

1. This bootstrap (done)
2. IDT64, exceptions, APIC/PIC
3. PMM/VMM (4-level page tables)
4. Port drivers: serial, keyboard (IO ports same)
5. Syscall via `syscall`/`sysret` (not `int 0x80`)
6. ELF64 loader, FAT32 (pointers → 64-bit)
7. Drop or freeze i386 tree

## Multiboot2 vs Multiboot1

| | MB1 | MB2 |
|--|-----|-----|
| Magic header | `0x1BADB002` | `0xE85250D6` |
| Entry EAX | `0x2BADB002` | `0x36d76289` |
| Info | fixed struct | tagged list |
| 64-bit | awkward | designed for modern loaders |

## UEFI later

UEFI often uses a PE stub or GRUB EFI that still can `multiboot2` load this ELF/binary.

## Higher-half layout

```
KERNEL_VMA = 0xFFFFFFFF80000000

Virt  0xFFFFFFFF80100000  →  Phys 0x00100000   (kernel image)
Virt  0xFFFFFFFF80000000  →  Phys 0x00000000   (1 GiB HH window, 2 MiB pages)

Identity 0–4 GiB kept for early trampoline / MMIO
```

Compiler flags (required for HH):

```
-m64 -mcmodel=kernel -mno-red-zone -ffreestanding -fno-pic -fno-pie
```

`-mcmodel=kernel`: code/data in negative 2 GiB of 64-bit space (fits HH).
