#pragma once
#include <stdint.h>
#include <stddef.h>
#include "multiboot.h"

#define PAGE_SIZE 4096

#ifdef __cplusplus
extern "C" {
#endif

void pmm_init(multiboot_info_t* mbd, uintptr_t kernel_end_phys);

void* pmm_alloc_page(void);

void pmm_free_page(void* page_phys);

void pmm_init_region(uintptr_t base, size_t size);
void pmm_deinit_region(uintptr_t base, size_t size);

#ifdef __cplusplus
}
#endif
