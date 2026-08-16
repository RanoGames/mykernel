ENTRY(_start)

SECTIONS
{
    . = 0x400000;

    .interp : { *(.interp) }

    .hash : { *(.hash) }
    .dynsym : { *(.dynsym) }
    .dynstr : { *(.dynstr) }
    .rel.dyn : { *(.rel.dyn) }
    .rel.plt : { *(.rel.plt) }

    .plt : { *(.plt) }

    .text : {
        *(.text)
        *(.text.*)
    }

    .rodata : {
        *(.rodata)
        *(.rodata.*)
    }

    .dynamic : { *(.dynamic) }

    .data : {
        *(.data)
        *(.data.*)
        *(.got)
        *(.got.plt)
    }

    .bss : {
        *(.bss)
        *(.bss.*)
        *(COMMON)
    }
}
