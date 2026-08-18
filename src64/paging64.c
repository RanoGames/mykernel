/* paging64.c — virt walk, NX, W^X via 4 KiB pages */

#include "paging64.h"

extern uint64_t pml4[], pdpt[], pdpt_hh[], pd0[], pd1[], pd2[], pd3[], pd_hh[];
extern uint32_t g_nx_enabled;

extern char _text_start[], _text_end[];
extern char _rodata_start[], _rodata_end[];
extern char _data_start[], _data_end[];
extern char _bss_start[], _bss_end[];
extern char _kernel_virtual_start[], _kernel_virtual_end[];

/* One PT: 512 × 4 KiB = 2 MiB covering KERNEL_VMA + 0 */
static uint64_t pt_hh0[512] __attribute__((aligned(4096)));

static volatile uint16_t* const VGA = (volatile uint16_t*)0xB8000;
static int prow = 18, pcol = 0;

static void pputc(char c) {
    if (c == '\n') { pcol = 0; if (++prow >= 25) prow = 14; return; }
    if (prow < 25 && pcol < 80)
        VGA[prow * 80 + pcol++] = (uint16_t)(0x0E00 | (uint8_t)c);
}
static void pputs(const char* s) { while (*s) pputc(*s++); }
static void phex(uint64_t v) {
    const char* h = "0123456789ABCDEF";
    pputs("0x");
    for (int i = 60; i >= 0; i -= 4) pputc(h[(v >> i) & 0xF]);
}

static uint64_t virt_to_table_ptr(uint64_t phys) {
    return phys + KERNEL_VMA;
}

void paging_load_cr3(uint64_t pml4_phys) {
    __asm__ volatile ("mov %0, %%cr3" : : "r"(pml4_phys) : "memory");
}

uint64_t paging_read_efer(void) {
    uint32_t lo, hi;
    __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(0xC0000080));
    return ((uint64_t)hi << 32) | lo;
}

int paging_nx_available(void) {
    uint32_t eax = 0x80000001, ebx, ecx, edx;
    __asm__ volatile ("cpuid" : "+a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx));
    return (edx >> 20) & 1;
}

void paging_pte_set_nx(uint64_t* pte) { if (pte) *pte |= PAGE_NX; }
void paging_pte_clear_nx(uint64_t* pte) { if (pte) *pte &= ~PAGE_NX; }
int paging_pte_is_nx(uint64_t pte) { return (pte & PAGE_NX) != 0; }

uint64_t paging_virt_to_phys(uint64_t va) {
    uint64_t e = pml4[pml4_index(va)];
    if (!(e & PAGE_PRESENT)) return 0;
    uint64_t* pdpt_t = (uint64_t*)virt_to_table_ptr(e & PTE_ADDR_MASK);

    e = pdpt_t[pdpt_index(va)];
    if (!(e & PAGE_PRESENT)) return 0;
    if (e & PAGE_LARGE)
        return (e & 0x000FFFFFC0000000ULL) | (va & 0x3FFFFFFFULL);

    uint64_t* pd_t = (uint64_t*)virt_to_table_ptr(e & PTE_ADDR_MASK);
    e = pd_t[pd_index(va)];
    if (!(e & PAGE_PRESENT)) return 0;
    if (e & PAGE_LARGE)
        return (e & 0x000FFFFFFFE00000ULL) | (va & 0x1FFFFFULL);

    uint64_t* pt_t = (uint64_t*)virt_to_table_ptr(e & PTE_ADDR_MASK);
    e = pt_t[pt_index(va)];
    if (!(e & PAGE_PRESENT)) return 0;
    return (e & PTE_ADDR_MASK) | (va & 0xFFFULL);
}

static int in_range(uint64_t va, void* a, void* b) {
    return va >= (uint64_t)(uintptr_t)a && va < (uint64_t)(uintptr_t)b;
}

/* Flags for W^X: never WRITE+exec together when NX available */
static uint64_t flags_for_va(uint64_t va, int nx_on) {
    uint64_t f = PAGE_PRESENT;
    if (in_range(va, _text_start, _text_end)) {
        /* RX: present, no write, no NX */
        return f;
    }
    if (in_range(va, _rodata_start, _rodata_end)) {
        f |= nx_on ? PAGE_NX : 0; /* R, NX */
        return f;
    }
    /* data, bss, stack, gaps: RW + NX */
    f |= PAGE_WRITE;
    if (nx_on) f |= PAGE_NX;
    return f;
}

void paging_apply_w_xor_x(void) {
    int nx = paging_nx_available() && g_nx_enabled;
    /* Cover phys 0..2MiB with 4K pages at HH */
    for (int i = 0; i < 512; i++) {
        uint64_t phys = (uint64_t)i * PAGE_SIZE_4K;
        uint64_t va = KERNEL_VMA + phys;
        uint64_t flags = flags_for_va(va, nx);
        /* pages outside kernel image: still map RW+NX for tables/stack */
        if (va < (uint64_t)(uintptr_t)_kernel_virtual_start ||
            va >= (uint64_t)(uintptr_t)_kernel_virtual_end) {
            flags = PAGE_PRESENT | PAGE_WRITE | (nx ? PAGE_NX : 0);
            /* leave low trampoline area executable if no section match and phys < 2M in identity use */
            if (phys >= 0x100000 && phys < 0x101000 && !nx)
                flags = PAGE_PRESENT;
        }
        pt_hh0[i] = (phys & PTE_ADDR_MASK) | flags;
    }

    /* Ensure .text pages explicitly RX */
    for (uint64_t va = (uint64_t)(uintptr_t)_text_start & ~(PAGE_SIZE_4K - 1);
         va < (uint64_t)(uintptr_t)_text_end;
         va += PAGE_SIZE_4K) {
        unsigned idx = (unsigned)((va - KERNEL_VMA) / PAGE_SIZE_4K);
        if (idx < 512) {
            uint64_t phys = va - KERNEL_VMA;
            pt_hh0[idx] = (phys & PTE_ADDR_MASK) | PAGE_PRESENT; /* RX */
        }
    }

    /* .rodata: R + NX */
    for (uint64_t va = (uint64_t)(uintptr_t)_rodata_start & ~(PAGE_SIZE_4K - 1);
         va < (uint64_t)(uintptr_t)_rodata_end;
         va += PAGE_SIZE_4K) {
        unsigned idx = (unsigned)((va - KERNEL_VMA) / PAGE_SIZE_4K);
        if (idx < 512) {
            uint64_t phys = va - KERNEL_VMA;
            uint64_t f = PAGE_PRESENT | (nx ? PAGE_NX : 0);
            pt_hh0[idx] = (phys & PTE_ADDR_MASK) | f;
        }
    }

    /* .data + .bss: RW + NX */
    for (uint64_t va = (uint64_t)(uintptr_t)_data_start & ~(PAGE_SIZE_4K - 1);
         va < (uint64_t)(uintptr_t)_bss_end;
         va += PAGE_SIZE_4K) {
        unsigned idx = (unsigned)((va - KERNEL_VMA) / PAGE_SIZE_4K);
        if (idx < 512) {
            uint64_t phys = va - KERNEL_VMA;
            uint64_t f = PAGE_PRESENT | PAGE_WRITE | (nx ? PAGE_NX : 0);
            pt_hh0[idx] = (phys & PTE_ADDR_MASK) | f;
        }
    }

    /* Point PD_HH[0] at PT instead of 2MiB page */
    uint64_t pt_phys = (uint64_t)(uintptr_t)pt_hh0 - KERNEL_VMA;
    pd_hh[0] = (pt_phys & PTE_ADDR_MASK) | PAGE_PRESENT | PAGE_WRITE; /* no LARGE */

    /* Reload CR3 — flush TLB */
    uint64_t cr3_phys = (uint64_t)(uintptr_t)pml4 - KERNEL_VMA;
    paging_load_cr3(cr3_phys);
}

void paging_dump_pml4(void) {
    pputs("PML4 present:\n");
    for (int i = 0; i < 512; i++) {
        if (!(pml4[i] & PAGE_PRESENT)) continue;
        pputs(" ["); phex((uint64_t)i); pputs("]->"); phex(pml4[i] & PTE_ADDR_MASK);
        if (i == 0) pputs(" id");
        if (i == 511) pputs(" HH");
        pputc('\n');
    }
    uint64_t efer = paging_read_efer();
    pputs("EFER="); phex(efer);
    pputs(" NXE="); phex((efer & EFER_NXE) != 0);
    pputs(" LME="); phex((efer & EFER_LME) != 0);
    pputc('\n');
    pputs("CPUID.NX="); phex((uint64_t)paging_nx_available());
    pputs(" g_nx="); phex(g_nx_enabled);
    pputc('\n');

    /* Sample W^X PTEs after apply */
    uint64_t tva = (uint64_t)(uintptr_t)_text_start;
    uint64_t dva = (uint64_t)(uintptr_t)_data_start;
    unsigned ti = (unsigned)((tva - KERNEL_VMA) / PAGE_SIZE_4K);
    unsigned di = (unsigned)((dva - KERNEL_VMA) / PAGE_SIZE_4K);
    if (ti < 512 && di < 512) {
        pputs("text PTE NX="); phex(paging_pte_is_nx(pt_hh0[ti]));
        pputs(" W="); phex((pt_hh0[ti] & PAGE_WRITE) != 0);
        pputc('\n');
        pputs("data PTE NX="); phex(paging_pte_is_nx(pt_hh0[di]));
        pputs(" W="); phex((pt_hh0[di] & PAGE_WRITE) != 0);
        pputc('\n');
    }
}
