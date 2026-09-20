#pragma once
#include <stdint.h>

void vmm_map_page(uint64_t virt, uint64_t phys, uint64_t flags);
