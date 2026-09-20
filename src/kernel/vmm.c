#include <stdint.h>
#include <string.h>
#include "vmm.h"
#include "pmm.h"

#define PAGE_PRESENT       (1ULL << 0)
#define PAGE_WRITABLE      (1ULL << 1)
#define PAGE_CACHE_DISABLE (1ULL << 4)

#define PML4_INDEX(addr) (((addr) >> 39) & 0x1FF)
#define PDPT_INDEX(addr) (((addr) >> 30) & 0x1FF)
#define PD_INDEX(addr)   (((addr) >> 21) & 0x1FF)
#define PT_INDEX(addr)   (((addr) >> 12) & 0x1FF)

extern uint64_t pml4[512]; 

void vmm_map_page(uint64_t virt, uint64_t phys, uint64_t flags) {
    size_t pml4_idx = PML4_INDEX(virt);
    if (!(pml4[pml4_idx] & PAGE_PRESENT)) {
        uint64_t* new_table = (uint64_t*)pmm_alloc_page();
        memset(new_table, 0, 4096);
        pml4[pml4_idx] = (uint64_t)new_table | PAGE_PRESENT | PAGE_WRITABLE;
    }
    uint64_t* pdpt = (uint64_t*)(pml4[pml4_idx] & ~0xFFF);

    size_t pdpt_idx = PDPT_INDEX(virt);
    if (!(pdpt[pdpt_idx] & PAGE_PRESENT)) {
        uint64_t* new_table = (uint64_t*)pmm_alloc_page();
        memset(new_table, 0, 4096);
        pdpt[pdpt_idx] = (uint64_t)new_table | PAGE_PRESENT | PAGE_WRITABLE;
    }
    uint64_t* pd = (uint64_t*)(pdpt[pdpt_idx] & ~0xFFF);

    size_t pd_idx = PD_INDEX(virt);
    if (!(pd[pd_idx] & PAGE_PRESENT)) {
        uint64_t* new_table = (uint64_t*)pmm_alloc_page();
        memset(new_table, 0, 4096);
        pd[pd_idx] = (uint64_t)new_table | PAGE_PRESENT | PAGE_WRITABLE;
    }
    uint64_t* pt = (uint64_t*)(pd[pd_idx] & ~0xFFF);

    size_t pt_idx = PT_INDEX(virt);
    pt[pt_idx] = (phys & ~0xFFF) | flags;
    
    asm volatile("invlpg (%0)" ::"r"(virt) : "memory");
}
