/* paging64.h — PML4 + W^X (4 KiB) + NX */
#ifndef PAGING64_H
#define PAGING64_H

#include <stdint.h>

#define KERNEL_VMA          0xFFFFFFFF80000000ULL
#define PAGE_SIZE_4K        0x1000ULL
#define PAGE_SIZE_2M        0x200000ULL

#define PAGE_PRESENT        (1ULL << 0)
#define PAGE_WRITE          (1ULL << 1)
#define PAGE_USER           (1ULL << 2)
#define PAGE_PWT            (1ULL << 3)
#define PAGE_PCD            (1ULL << 4)
#define PAGE_ACCESSED       (1ULL << 5)
#define PAGE_DIRTY          (1ULL << 6)
#define PAGE_LARGE          (1ULL << 7)
#define PAGE_GLOBAL         (1ULL << 8)
#define PAGE_NX             (1ULL << 63)

#define PTE_ADDR_MASK       0x000FFFFFFFFFF000ULL

/* EFER bits (MSR 0xC0000080) */
#define EFER_SCE            (1ULL << 0)
#define EFER_LME            (1ULL << 8)
#define EFER_LMA            (1ULL << 10)
#define EFER_NXE            (1ULL << 11)

static inline unsigned pml4_index(uint64_t va) { return (va >> 39) & 0x1FF; }
static inline unsigned pdpt_index(uint64_t va) { return (va >> 30) & 0x1FF; }
static inline unsigned pd_index(uint64_t va)   { return (va >> 21) & 0x1FF; }
static inline unsigned pt_index(uint64_t va)   { return (va >> 12) & 0x1FF; }

extern uint64_t pml4[512];
extern uint64_t pdpt[512];
extern uint64_t pdpt_hh[512];
extern uint64_t pd0[512], pd1[512], pd2[512], pd3[512];
extern uint64_t pd_hh[512];
extern uint32_t g_nx_enabled;

void paging_dump_pml4(void);
uint64_t paging_virt_to_phys(uint64_t va);
void paging_load_cr3(uint64_t pml4_phys);

int  paging_nx_available(void);
void paging_pte_set_nx(uint64_t* pte);
void paging_pte_clear_nx(uint64_t* pte);
int  paging_pte_is_nx(uint64_t pte);

/* Replace 2MiB HH kernel window with 4KiB W^X mappings */
void paging_apply_w_xor_x(void);

uint64_t paging_read_efer(void);

#endif
