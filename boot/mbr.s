/* mbr.s — BIOS MBR: load stage2 @ LBA1 → 0x7E00, jump */
.code16
.global _mbr_start
.section .text
_mbr_start:
    cli
    xor %ax, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss
    mov $0x7C00, %sp
    sti

    /* DL = BIOS drive */
    mov %dl, drive

    /* Load 32 sectors from LBA 1 to 0x7E00 using DAP (LBA) */
    mov $0x42, %ah
    mov drive, %dl
    mov $dap, %si
    int $0x13
    jc disk_error

    mov drive, %dl
    ljmp $0x0000, $0x7E00

disk_error:
    mov $msg, %si
print:
    lodsb
    test %al, %al
    jz halt
    mov $0x0E, %ah
    int $0x10
    jmp print
halt:
    cli
    hlt
    jmp halt

drive: .byte 0
msg: .asciz "MBR disk error"

.align 4
dap:
    .byte 0x10          /* size */
    .byte 0
    .word 32            /* sector count */
    .word 0x7E00        /* offset */
    .word 0x0000        /* segment */
    .long 1             /* LBA low */
    .long 0             /* LBA high */

/* partition table @ 446 filled by installer; signature @ 510 */
.space 510 - (. - _mbr_start)
.byte 0x55, 0xAA
