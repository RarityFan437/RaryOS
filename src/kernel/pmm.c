#include "pmm.h"
#include "stdio.h"
#include "string.h"

static uint8_t* pmm_bitmap = NULL;
static size_t   pmm_max_blocks = 0;
static size_t   pmm_bitmap_size = 0;
static size_t   pmm_hint_word = 0;

static inline void pmm_set_bit(size_t bit) {
    pmm_bitmap[bit / 8] |= (1 << (bit % 8));
}

static inline void pmm_clear_bit(size_t bit) {
    pmm_bitmap[bit / 8] &= ~(1 << (bit % 8));
}

static inline int pmm_test_bit(size_t bit) {
    return (pmm_bitmap[bit / 8] & (1 << (bit % 8))) != 0;
}

static inline uintptr_t align_down(uintptr_t x) {
    return x & ~(uintptr_t)(PAGE_SIZE - 1);
}

static inline uintptr_t align_up(uintptr_t x) {
    return (x + PAGE_SIZE - 1) & ~(uintptr_t)(PAGE_SIZE - 1);
}

void pmm_init_region(uintptr_t base, size_t size) {
    uintptr_t start = align_up(base);
    uintptr_t end   = align_down(base + size);
    if (start >= end) return;

    size_t first = start / PAGE_SIZE;
    size_t count = (end - start) / PAGE_SIZE;

    for (size_t i = 0; i < count; i++) {
        size_t bit = first + i;
        if (bit >= pmm_max_blocks) break;
        pmm_clear_bit(bit);
    }
}

void pmm_deinit_region(uintptr_t base, size_t size) {
    uintptr_t start = align_down(base);
    uintptr_t end   = align_up(base + size);
    if (start >= end) return;

    size_t first = start / PAGE_SIZE;
    size_t count = (end - start) / PAGE_SIZE;

    for (size_t i = 0; i < count; i++) {
        size_t bit = first + i;
        if (bit >= pmm_max_blocks) break;
        pmm_set_bit(bit);
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
        if (mmap->size < sizeof(multiboot_memory_map_t) - 4 ||
            ((uint32_t)(uintptr_t)mmap + mmap->size + 4) > mmap_end_addr) {
            printf("PANIC: Multiboot mmap entry size is corrupted or overflows buffer!\n");
            while(1) asm volatile("cli; hlt");
        }

        if (mmap->type == 1) {
            if (mmap->addr > (mmap->addr + mmap->len)) {
                printf("PANIC: Mmap region address integer overflow!\n");
                while(1) asm volatile("cli; hlt");
            }

            uintptr_t end_addr = mmap->addr + mmap->len;
            if (end_addr > max_memory_addr) {
                max_memory_addr = end_addr;
            }
        }
        mmap = (multiboot_memory_map_t*)((uintptr_t)mmap + mmap->size + 4);
    }

    pmm_max_blocks  = align_down(max_memory_addr) / PAGE_SIZE;
    pmm_bitmap_size = (pmm_max_blocks + 7) / 8;
    pmm_bitmap_size = (pmm_bitmap_size + PAGE_SIZE - 1) & ~(size_t)(PAGE_SIZE - 1);

    pmm_bitmap = (uint8_t*)kernel_end_phys;
    uintptr_t pmm_end_phys = kernel_end_phys + pmm_bitmap_size;

    uintptr_t mbd_start  = (uintptr_t)mbd;
    uintptr_t mbd_end    = mbd_start + sizeof(multiboot_info_t);
    uintptr_t mmap_start = (uintptr_t)mbd->mmap_addr;
    uintptr_t mmap_end   = mmap_end_addr;

    memset(pmm_bitmap, 0xFF, pmm_bitmap_size);

    mmap = (multiboot_memory_map_t*)mmap_start;
    while ((uint32_t)(uintptr_t)mmap < mmap_end) {
        if (mmap->type == 1) {
            pmm_init_region(mmap->addr, mmap->len);
        }
        mmap = (multiboot_memory_map_t*)((uintptr_t)mmap + mmap->size + 4);
    }

    pmm_deinit_region(0x00000000, 1024 * 1024);

    size_t kernel_and_bitmap_size = pmm_end_phys - 0x100000;
    pmm_deinit_region(0x100000, kernel_and_bitmap_size);

    pmm_deinit_region(mbd_start, mbd_end - mbd_start);
    pmm_deinit_region(mmap_start, mmap_end - mmap_start);

    if (pmm_max_blocks > 0) {
        size_t last_word = (pmm_max_blocks - 1) / 64;
        size_t last_bit  = (pmm_max_blocks - 1) % 64;
        uint64_t* bm = (uint64_t*)pmm_bitmap;
        if (last_bit < 63) {
            uint64_t keep = (1ULL << (last_bit + 1)) - 1;
            bm[last_word] |= ~keep;
        }
    }

    pmm_hint_word = 0;
}

void* pmm_alloc_page(void) {
    if (pmm_max_blocks == 0) return NULL;

    size_t words = (pmm_max_blocks + 63) / 64;
    uint64_t* bm = (uint64_t*)pmm_bitmap;

    for (int pass = 0; pass < 2; pass++) {
        size_t start = (pass == 0) ? pmm_hint_word : 0;
        size_t stop  = (pass == 0) ? words : pmm_hint_word;

        for (size_t w = start; w < stop; w++) {
            uint64_t word = bm[w];
            if (word == ~0ULL) continue;

            int bit = __builtin_ctzll(~word);
            size_t global_bit = (w << 6) + (size_t)bit;

            bm[w] |= (1ULL << bit);
            pmm_hint_word = w;
            return (void*)(global_bit * PAGE_SIZE);
        }
    }
    return NULL;
}

void pmm_free_page(void* page_phys) {
    uintptr_t addr = (uintptr_t)page_phys;
    size_t global_bit = addr / PAGE_SIZE;

    if (global_bit < pmm_max_blocks) {
        pmm_clear_bit(global_bit);

        size_t word = global_bit / 64;
        if (word < pmm_hint_word) pmm_hint_word = word;
    }
}
