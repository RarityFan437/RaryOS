#include "pmm.h"
#include "stdio.h"
#include "string.h"

static uint8_t* pmm_bitmap = NULL;
static size_t   pmm_max_blocks = 0;
static size_t   pmm_bitmap_size = 0;

static inline void pmm_set_bit(size_t bit) {
    pmm_bitmap[bit / 8] |= (1 << (bit % 8));
}

static inline void pmm_clear_bit(size_t bit) {
    pmm_bitmap[bit / 8] &= ~(1 << (bit % 8));
}

static inline int pmm_test_bit(size_t bit) {
    return (pmm_bitmap[bit / 8] & (1 << (bit % 8))) != 0;
}

void pmm_init_region(uintptr_t base, size_t size) {
    size_t align = base / PAGE_SIZE;
    size_t blocks = size / PAGE_SIZE;

    for (size_t i = 0; i < blocks; i++) {
        if (align + i < pmm_max_blocks) {
            pmm_clear_bit(align + i);
        }
    }
}

void pmm_deinit_region(uintptr_t base, size_t size) {
    size_t align = base / PAGE_SIZE;
    size_t blocks = (size + PAGE_SIZE - 1) / PAGE_SIZE;

    for (size_t i = 0; i < blocks; i++) {
        if (align + i < pmm_max_blocks) {
            pmm_set_bit(align + i);
        }
    }
}

void pmm_init(multiboot_info_t* mbd, uintptr_t kernel_end_phys) {
    if (!(mbd->flags & MULTIBOOT_INFO_MEM_MAP)) {
        printf("PMM ERROR: No Multiboot memory map provided!\n");
        return;
    }

    uintptr_t max_memory_addr = 0;
    multiboot_memory_map_t* mmap = (multiboot_memory_map_t*)(uintptr_t)mbd->mmap_addr;
    uint32_t mmap_end_addr = mbd->mmap_addr + mbd->mmap_length;

    while ((uint32_t)(uintptr_t)mmap < mmap_end_addr) {
        if (mmap->type == 1) {
            uintptr_t end_addr = mmap->addr + mmap->len;
            if (end_addr > max_memory_addr) {
                max_memory_addr = end_addr;
            }
        }
        mmap = (multiboot_memory_map_t*)((uintptr_t)mmap + mmap->size + 4);
    }

    pmm_max_blocks = max_memory_addr / PAGE_SIZE;
    pmm_bitmap_size = pmm_max_blocks / 8;

    pmm_bitmap = (uint8_t*)kernel_end_phys;
    
    memset(pmm_bitmap, 0xFF, pmm_bitmap_size);

    mmap = (multiboot_memory_map_t*)(uintptr_t)mbd->mmap_addr;
    while ((uint32_t)(uintptr_t)mmap < mmap_end_addr) {
        if (mmap->type == 1) {
            pmm_init_region(mmap->addr, mmap->len);
        }
        mmap = (multiboot_memory_map_t*)((uintptr_t)mmap + mmap->size + 4);
    }

    pmm_deinit_region(0x00000000, 1024 * 1024);

    uintptr_t pmm_end_phys = kernel_end_phys + pmm_bitmap_size;
    size_t kernel_and_bitmap_size = pmm_end_phys - 0x100000;
    pmm_deinit_region(0x100000, kernel_and_bitmap_size);

    uint64_t usable_ram = 0;
    {
        multiboot_memory_map_t* m2 = (multiboot_memory_map_t*)(uintptr_t)mbd->mmap_addr;
        uint32_t end2 = mbd->mmap_addr + mbd->mmap_length;
        while ((uint32_t)(uintptr_t)m2 < end2) {
            if (m2->type == 1) usable_ram += m2->len;
            m2 = (multiboot_memory_map_t*)((uintptr_t)m2 + m2->size + 4);
        }
    }
}

void* pmm_alloc_page(void) {
    for (size_t i = 0; i < pmm_bitmap_size; i++) {
        if (pmm_bitmap[i] != 0xFF) {
            for (int bit = 0; bit < 8; bit++) {
                size_t global_bit = i * 8 + bit;
                if (!pmm_test_bit(global_bit)) {
                    pmm_set_bit(global_bit);
                    return (void*)(global_bit * PAGE_SIZE);
                }
            }
        }
    }
    return NULL;
}

void pmm_free_page(void* page_phys) {
    uintptr_t addr = (uintptr_t)page_phys;
    size_t global_bit = addr / PAGE_SIZE;

    if (global_bit < pmm_max_blocks) {
        pmm_clear_bit(global_bit);
    }
}
