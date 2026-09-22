#include <stdint.h>
#include <string.h>
#include "vmm.h"
#include "pmm.h"

#define PML4_INDEX(addr) (((addr) >> 39) & 0x1FF)
#define PDPT_INDEX(addr) (((addr) >> 30) & 0x1FF)
#define PD_INDEX(addr)   (((addr) >> 21) & 0x1FF)
#define PT_INDEX(addr)   (((addr) >> 12) & 0x1FF)

#define PTE_ADDR_MASK  0x000FFFFFFFFFF000ULL
#define PAGE_2M_MASK   (~(0x200000ULL - 1))
#define PAGE_1G_MASK   (~(0x40000000ULL - 1))

extern uint64_t pml4[512];

static uint64_t* vmm_next_table(uint64_t* table, size_t idx) {
    uint64_t entry = table[idx];

    if (entry & PAGE_PRESENT) {
        if (entry & PAGE_PS) return NULL;
        return (uint64_t*)(entry & PTE_ADDR_MASK);
    }

    uint64_t* new_table = (uint64_t*)pmm_alloc_page();
    if (!new_table) return NULL;

    memset(new_table, 0, 4096);
    table[idx] = ((uint64_t)new_table & PTE_ADDR_MASK)
               | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;

    return new_table;
}

static inline void vmm_flush(uint64_t virt, uint64_t old_entry, uint64_t new_entry) {
    if (old_entry != new_entry) {
        asm volatile("invlpg (%0)" :: "r"(virt) : "memory");
    }
}

void vmm_map_page(uint64_t virt, uint64_t phys, uint64_t flags) {
    uint64_t* pdpt = vmm_next_table(pml4, PML4_INDEX(virt));
    if (!pdpt) return;

    uint64_t* pd = vmm_next_table(pdpt, PDPT_INDEX(virt));
    if (!pd) return;

    uint64_t* pt = vmm_next_table(pd, PD_INDEX(virt));
    if (!pt) return;

    size_t pt_idx = PT_INDEX(virt);
    uint64_t new_entry = (phys & PTE_ADDR_MASK)
                       | PAGE_PRESENT
                       | (flags & ~PAGE_PS);
    uint64_t old_entry = pt[pt_idx];
    pt[pt_idx] = new_entry;

    vmm_flush(virt, old_entry, new_entry);
}

void vmm_map_huge_2mb(uint64_t virt, uint64_t phys, uint64_t flags) {
    if ((virt & ~PAGE_2M_MASK) || (phys & ~PAGE_2M_MASK)) return;

    uint64_t* pdpt = vmm_next_table(pml4, PML4_INDEX(virt));
    if (!pdpt) return;

    uint64_t* pd = vmm_next_table(pdpt, PDPT_INDEX(virt));
    if (!pd) return;

    size_t pd_idx = PD_INDEX(virt);
    uint64_t new_entry = (phys & PTE_ADDR_MASK)
                       | PAGE_PRESENT | PAGE_PS
                       | (flags & ~PAGE_PS);
    uint64_t old_entry = pd[pd_idx];
    pd[pd_idx] = new_entry;

    vmm_flush(virt, old_entry, new_entry);
}

void vmm_map_huge_1gb(uint64_t virt, uint64_t phys, uint64_t flags) {
    if ((virt & ~PAGE_1G_MASK) || (phys & ~PAGE_1G_MASK)) return;

    uint64_t* pdpt = vmm_next_table(pml4, PML4_INDEX(virt));
    if (!pdpt) return;

    size_t pdpt_idx = PDPT_INDEX(virt);
    uint64_t new_entry = (phys & PTE_ADDR_MASK)
                       | PAGE_PRESENT | PAGE_PS
                       | (flags & ~PAGE_PS);
    uint64_t old_entry = pdpt[pdpt_idx];
    pdpt[pdpt_idx] = new_entry;

    vmm_flush(virt, old_entry, new_entry);
}
