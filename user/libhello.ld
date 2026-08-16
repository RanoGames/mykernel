ENTRY(lib_greet)

SECTIONS
{
    . = 0x0;

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
