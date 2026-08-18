/* boot64.s — Multiboot2 + higher-half (VMA 0xFFFFFFFF80000000 + phys) */

.set KERNEL_VMA, 0xFFFFFFFF80000000
.set MB2_MAGIC, 0xE85250D6
.set MB2_ARCH,  0
.set MB2_LEN,   mb2_header_end - mb2_header
.set MB2_CHECK, -(MB2_MAGIC + MB2_ARCH + MB2_LEN)

.macro PHYS dst, sym
    movl $(\sym - KERNEL_VMA), \dst
.endm

.section .multiboot, "a"
.align 8
mb2_header:
    .long MB2_MAGIC
    .long MB2_ARCH
    .long MB2_LEN
    .long MB2_CHECK
    .word 0
    .word 0
    .long 8
mb2_header_end:

.section .text
.code32
.global _start
_start:
    cli
    PHYS %edi, mb2_magic
    movl %eax, (%edi)
    PHYS %edi, mb2_info
    movl %ebx, (%edi)

    /*
     * ========== PML4 setup (4-level, long mode) ==========
     *
     *   CR3 ──► PML4 (512 entries × 8 bytes)
     *              [0]   ──► PDPT     (identity 0–4 GiB)
     *              [511] ──► PDPT_HH  (higher half 0xFFFFFFFF80000000)
     *   PDPT[i] ──► PD   (each covers 1 GiB with 512 × 2 MiB pages)
     *   PD[j]   =  phys | PRESENT | WRITE | LARGE (PS=1, 2 MiB page)
     *
     *   VA bits: [47:39] PML4 | [38:30] PDPT | [29:21] PD | [20:0] offset (2MiB)
     */
    PHYS %edi, pml4
    xor %eax, %eax
    movl $(4096 * 7 / 4), %ecx
    rep stosl

    /* --- PML4[0] → PDPT (identity) --- */
    PHYS %eax, pdpt
    orl $3, %eax                 /* PRESENT|WRITE */
    PHYS %edi, pml4
    movl %eax, (%edi)

    /* --- PML4[511] → PDPT_HH (VA 0xFFFFFFFF_80000000) --- */
    PHYS %eax, pdpt_hh
    orl $3, %eax
    movl %eax, 511 * 8(%edi)

    /* --- PDPT identity: 4 entries → pd0..pd3 (4 × 1 GiB) --- */
    PHYS %edi, pdpt
    PHYS %eax, pd0
    orl $3, %eax
    movl %eax, 0(%edi)
    PHYS %eax, pd1
    orl $3, %eax
    movl %eax, 8(%edi)
    PHYS %eax, pd2
    orl $3, %eax
    movl %eax, 16(%edi)
    PHYS %eax, pd3
    orl $3, %eax
    movl %eax, 24(%edi)

    /* --- PDPT_HH[0] → pd_hh (1 GiB higher-half window) --- */
    PHYS %edi, pdpt_hh
    PHYS %eax, pd_hh
    orl $3, %eax
    movl %eax, (%edi)

    /*
     * --- PD: 2 MiB pages + NX (bit 63) ---
     * 64-bit PDE: low = phys|P|RW|PS (0x83), high bit31 = NX
     * Index 0 (phys 0..2MiB): executable (kernel + stage trampoline)
     * Other indices: NX=1
     */
    PHYS %edi, pd0
    movl $0x83, %eax
    movl $(512 * 4), %ecx
    xorl %ebx, %ebx
id_pd_loop:
    movl %eax, (%edi)
    movl $0, 4(%edi)
    cmpl $0, %ebx
    je id_pd_nx_done
    movl $0x80000000, 4(%edi)
id_pd_nx_done:
    addl $0x200000, %eax
    addl $8, %edi
    incl %ebx
    loop id_pd_loop

    PHYS %edi, pd_hh
    movl $0x83, %eax
    movl $512, %ecx
    xorl %ebx, %ebx
hh_pd_loop:
    movl %eax, (%edi)
    movl $0, 4(%edi)
    cmpl $0, %ebx
    je hh_pd_nx_done
    movl $0x80000000, 4(%edi)
hh_pd_nx_done:
    addl $0x200000, %eax
    addl $8, %edi
    incl %ebx
    loop hh_pd_loop

    /* CR3 = physical address of PML4 */
    PHYS %eax, pml4
    movl %eax, %cr3

    /* Build GDTR on stack (physical) */
    PHYS %eax, gdt64
    subl $8, %esp
    movw $(gdt64_end - gdt64 - 1), (%esp)
    movl %eax, 2(%esp)
    lgdt (%esp)
    addl $8, %esp

    movl %cr4, %eax
    orl $0x20, %eax
    movl %eax, %cr4

    /*
     * EFER (MSR 0xC0000080) — key bits:
     *   bit 0  SCE   — SYSCALL enable (later)
     *   bit 8  LME   — Long Mode Enable (required)
     *   bit 10 LMA   — Long Mode Active (read-only, set by CPU after PG)
     *   bit 11 NXE   — NX Enable (allows PAGE_NX bit 63 in PTEs)
     *
     * CPUID 0x80000001 EDX bit 20 = NX supported by CPU.
     */
    movl $0x80000001, %eax
    cpuid
    movl %edx, %esi              /* save feature flags */
    movl $0xC0000080, %ecx
    rdmsr
    orl $0x100, %eax             /* always LME */
    testl $(1 << 20), %esi       /* EDX.NX */
    jz 1f
    orl $0x800, %eax             /* NXE only if CPU supports NX */
1:
    wrmsr
    /* record NX avail in low memory for C */
    PHYS %edi, g_nx_enabled
    xorl %ebx, %ebx
    testl $(1 << 20), %esi
    jz 2f
    movl $1, %ebx
2:
    movl %ebx, (%edi)

    movl %cr0, %eax
    orl $0x80000001, %eax
    movl %eax, %cr0

    /* far return to phys(_start64_low) in 64-bit CS */
    PHYS %eax, _start64_low
    pushl $0x18
    pushl %eax
    lret

.code64
.global _start64_low
_start64_low:
    movw $0x20, %ax
    movw %ax, %ds
    movw %ax, %es
    movw %ax, %ss
    movw %ax, %fs
    movw %ax, %gs

    movabs $stack_top, %rsp
    movabs $_start64_hh, %rax
    jmpq *%rax

.global _start64_hh
_start64_hh:
    xorq %rbp, %rbp
    movabs $mb2_magic, %rax
    movl (%rax), %edi
    movabs $mb2_info, %rax
    movl (%rax), %esi
    call kernel_main64
3:
    cli
    hlt
    jmp 3b

.section .data
.align 16
gdt64:
    .quad 0
    .quad 0x00CF9A000000FFFF
    .quad 0x00CF92000000FFFF
    .quad 0x00AF9A000000FFFF
    .quad 0x00AF92000000FFFF
gdt64_end:

.align 4
mb2_magic: .long 0
mb2_info:  .long 0
.global g_nx_enabled
g_nx_enabled: .long 0

.section .bss
.align 4096
.global pml4, pdpt, pdpt_hh, pd0, pd1, pd2, pd3, pd_hh
pml4:    .skip 4096
pdpt:    .skip 4096
pdpt_hh: .skip 4096
pd0:     .skip 4096
pd1:     .skip 4096
pd2:     .skip 4096
pd3:     .skip 4096
pd_hh:   .skip 4096
.align 16
    .skip 65536
.global stack_top
stack_top:
