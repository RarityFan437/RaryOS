#pragma once
#include <stdint.h>

#define PAGE_PRESENT       (1ULL << 0)
#define PAGE_WRITABLE      (1ULL << 1)
#define PAGE_USER          (1ULL << 2)
#define PAGE_CACHE_DISABLE (1ULL << 4)
#define PAGE_PS            (1ULL << 7)

void vmm_map_page(uint64_t virt, uint64_t phys, uint64_t flags);
void vmm_map_huge_2mb(uint64_t virt, uint64_t phys, uint64_t flags);
void vmm_map_huge_1gb(uint64_t virt, uint64_t phys, uint64_t flags);
