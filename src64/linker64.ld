/*
 * Higher-half x86_64 — section markers for W^X (4 KiB)
 */
ENTRY(_start)

KERNEL_VMA = 0xFFFFFFFF80000000;

SECTIONS
{
    . = KERNEL_VMA + 1M;

    _kernel_virtual_start = .;
    _kernel_physical_start = . - KERNEL_VMA;

    .multiboot : AT(1M) {
        *(.multiboot)
    }

    .text : AT(ADDR(.text) - KERNEL_VMA) {
        _text_start = .;
        *(.text .text.*)
        _text_end = .;
    }

    .rodata : AT(ADDR(.rodata) - KERNEL_VMA) {
        _rodata_start = .;
        *(.rodata .rodata.*)
        _rodata_end = .;
    }

    .data : AT(ADDR(.data) - KERNEL_VMA) {
        _data_start = .;
        *(.data .data.*)
        _data_end = .;
    }

    .bss : AT(ADDR(.bss) - KERNEL_VMA) {
        _bss_start = .;
        *(COMMON)
        *(.bss .bss.*)
        _bss_end = .;
    }

    _kernel_virtual_end = .;
    _kernel_physical_end = . - KERNEL_VMA;

    PROVIDE(_kernel_vma = KERNEL_VMA);

    /DISCARD/ : {
        *(.note.*)
        *(.comment)
        *(.eh_frame)
        *(.eh_frame_hdr)
    }
}
