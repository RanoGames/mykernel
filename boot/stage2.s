/* stage2.s — BIOS load kernel to low mem, PM copy to 1MB, jump
 * Protocol: LBA 8191 = magic 'KRNL' + sector_count; kernel @ LBA 8192
 * Linked at 0x7E00
 */
.code16
.global _stage2_start
.section .text
_stage2_start:
    cli
    xor %ax, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss
    mov $0x9000, %sp
    mov %dl, drive

    mov $msg_s2, %si
    call puts

    /* --- meta at LBA 8191 → 0x0500 --- */
    movw $0x0500, dap_off
    movw $0x0000, dap_seg
    movl $8191, dap_lba
    call disk_read1
    jc err_disk

    cmpl $0x4B524E4C, 0x0500
    jne err_magic
    movl 0x0504, %eax
    test %eax, %eax
    jz err_disk
    cmpl $512, %eax
    jbe 1f
    movl $512, %eax
1:  movl %eax, ksects
    movl 0x0508, %eax
    test %eax, %eax
    jnz 2f
    movl $0x10000C, %eax     /* default: after Multiboot hdr */
2:  movl %eax, kentry

    mov $msg_load, %si
    call puts

    /* --- load kernel sectors to 0x20000 (ES=0x2000) --- */
    movl $0, cur
load_rm:
    movl cur, %eax
    cmpl ksects, %eax
    jae do_pm

    /* buffer: physical 0x20000 + cur*512 → seg:off */
    movl %eax, %ebx
    shll $9, %ebx            /* *512 */
    addl $0x20000, %ebx
    movl %ebx, %ecx
    shrl $4, %ecx            /* segment */
    movw %cx, dap_seg
    andl $0xF, %ebx
    movw %bx, dap_off

    movl cur, %eax
    addl $8192, %eax
    movl %eax, dap_lba
    call disk_read1
    jc err_disk

    incl cur
    jmp load_rm

do_pm:
    mov $msg_pm, %si
    call puts
    cli
    lgdt gdt_desc
    mov %cr0, %eax
    or $1, %eax
    mov %eax, %cr0
    ljmp $0x08, $pm32

.code32
pm32:
    mov $0x10, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs
    mov %ax, %ss
    mov $0x90000, %esp

    /* copy ksects*512 bytes from 0x20000 → 0x100000 */
    movl $0x20000, %esi
    movl $0x100000, %edi
    movl ksects, %ecx
    shll $9, %ecx            /* bytes */
    shrl $2, %ecx            /* dwords */
    rep movsl

    /* Multiboot-ish handoff */
    movl $0x2BADB002, %eax
    xorl %ebx, %ebx
    movl kentry, %ecx
    jmp *%ecx

.code16
disk_read1:
    movb $0x42, %ah
    movb drive, %dl
    movw $dap, %si
    int $0x13
    ret

puts:
    lodsb
    test %al, %al
    jz 1f
    movb $0x0E, %ah
    movb $0x00, %bh
    int $0x10
    jmp puts
1:  ret

err_magic:
    mov $msg_magic, %si
    call puts
    jmp halt
err_disk:
    mov $msg_disk, %si
    call puts
halt:
    cli
    hlt
    jmp halt

drive:  .byte 0
.align 4
ksects: .long 0
kentry: .long 0x10000C
cur:    .long 0

.align 4
dap:
    .byte 0x10, 0
    .word 1                 /* 1 sector */
dap_off:
    .word 0
dap_seg:
    .word 0
dap_lba:
    .long 0
    .long 0

msg_s2:    .asciz "stage2 "
msg_load:  .asciz "load "
msg_pm:    .asciz "pm "
msg_magic: .asciz "noKRNL"
msg_disk:  .asciz "diskERR"

.align 8
gdt:
    .quad 0
    .quad 0x00CF9A000000FFFF
    .quad 0x00CF92000000FFFF
gdt_desc:
    .word 23
    .long gdt
