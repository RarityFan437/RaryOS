#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "idt.h"
#include "irq.h"
#include "input.h"
#include "stdio.h"
#include "string.h"
#include "multiboot2.h"
#include "usb.hpp"
#include "malloc.h"
#include "pmm.h"
#include "acpi.h"
#include "lapic.h"
#include "fb.h"
#include "vmm.h"
#include "pci.hpp"
#include "terminal.h"
#include "io.h"
#include "user.h"
#include "effect.h"

#define PAGE_PRESENT (1ULL << 0)
#define PAGE_WRITABLE (1ULL << 1)
#define PAGE_CACHE_DISABLE (1ULL << 4)

void init_idt(void);
void pic_remap(void);

extern int usb_irq_active;

volatile uint64_t pit_ticks = 0;

extern char kernel_end[];
static char* heap_end = kernel_end;
static char* heap_limit = kernel_end;

static multiboot_memory_map_t g_mmap_buffer[128];
static multiboot_info_t       g_mb_info;

const char* version = "0.3.5";
uint64_t g_ram_mb = 0;

void init_heap(multiboot_info_t* mbd) {
    if (!(mbd->flags & MULTIBOOT_INFO_MEM_MAP)) {
        heap_limit = (char*)((uintptr_t)kernel_end + (16 * 1024 * 1024));
        return;
    }

    multiboot_memory_map_t* mmap = (multiboot_memory_map_t*)(uintptr_t)mbd->mmap_addr;
    uint32_t mmap_end_addr = mbd->mmap_addr + mbd->mmap_length;
    uintptr_t kernel_addr = (uintptr_t)&kernel_end;

    while ((uint32_t)(uintptr_t)mmap < mmap_end_addr) {
        if (mmap->type == 1) {
            if (kernel_addr >= mmap->addr && kernel_addr < (mmap->addr + mmap->len)) {
                heap_limit = (char*)(uintptr_t)(mmap->addr + mmap->len);
                break;
            }
        }
        mmap = (multiboot_memory_map_t*)((uintptr_t)mmap + mmap->size + 4);
    }
}

void* sbrk(ptrdiff_t increment) {
    char* prev_heap_end = heap_end;
    uintptr_t current_next = (uintptr_t)(heap_end + increment);
    uintptr_t limit = (uintptr_t)heap_limit;
    uintptr_t start = (uintptr_t)&kernel_end;

    if (current_next > limit || current_next < start) {
        return (void*)-1;
    }
    heap_end += increment;
    return (void*)prev_heap_end;
}

static void convert_mb2_to_mb1(uint32_t mb2_addr) {
    for (size_t i = 0; i < sizeof(g_mb_info); i++) ((uint8_t*)&g_mb_info)[i] = 0;
    g_mb_info.flags = 0;

    if (mb2_addr == 0) return;

    struct mb2_info* info = (struct mb2_info*)(uintptr_t)mb2_addr;
    uint8_t* p = (uint8_t*)(uintptr_t)mb2_addr + 8;
    uint8_t* end = (uint8_t*)(uintptr_t)mb2_addr + info->total_size;

    uint32_t mmap_count = 0;

    while (p < end) {
        struct mb2_tag* tag = (struct mb2_tag*)p;
        if (tag->type == MB2_TAG_END) break;

        if (tag->type == MB2_TAG_BASIC_MEM) {
            struct mb2_tag_basic_mem* m = (struct mb2_tag_basic_mem*)p;
            g_mb_info.mem_lower = m->mem_lower;
            g_mb_info.mem_upper = m->mem_upper;
            g_mb_info.flags |= MULTIBOOT_INFO_MEMORY;
        }
        else if (tag->type == MB2_TAG_MMAP) {
            struct mb2_tag_mmap* m = (struct mb2_tag_mmap*)p;
            uint32_t esz = m->entry_size;
            uint32_t n = (m->size - 16) / esz;
            uint8_t* e = m->entries;

            for (uint32_t i = 0; i < n && mmap_count < 128; i++) {
                struct mb2_mmap_entry* me = (struct mb2_mmap_entry*)e;
                g_mmap_buffer[mmap_count].size = 20;
                g_mmap_buffer[mmap_count].addr = me->addr;
                g_mmap_buffer[mmap_count].len  = me->len;
                g_mmap_buffer[mmap_count].type = me->type;
                mmap_count++;
                e += esz;
            }

            g_mb_info.mmap_length = mmap_count * 24;
            g_mb_info.mmap_addr = (uint32_t)(uintptr_t)g_mmap_buffer;
            g_mb_info.flags |= MULTIBOOT_INFO_MEM_MAP;
        }
        else if (tag->type == MB2_TAG_BOOTLOADER) {
            struct mb2_tag_string* s = (struct mb2_tag_string*)p;
            g_mb_info.boot_loader_name = (uint32_t)(uintptr_t)s->string;
            g_mb_info.flags |= MULTIBOOT_INFO_BOOT_LOADER_NAME;
        }
        else if (tag->type == MB2_TAG_FRAMEBUFFER) {
            struct mb2_tag_framebuffer* f = (struct mb2_tag_framebuffer*)p;
            g_mb_info.framebuffer_addr   = f->addr;
            g_mb_info.framebuffer_pitch  = f->pitch;
            g_mb_info.framebuffer_width  = f->width;
            g_mb_info.framebuffer_height = f->height;
            g_mb_info.framebuffer_bpp    = f->bpp;
            g_mb_info.framebuffer_type   = f->fb_type;
            g_mb_info.flags |= MULTIBOOT_INFO_FRAMEBUFFER_INFO;
        }

        uint32_t aligned = (tag->size + 7) & ~7u;
        p += aligned;
    }
}

void init_pit(void) {
    uint32_t divisor = 1193182 / 100;
    outb(0x43, 0x36);
    outb(0x40, (uint8_t)(divisor & 0xFF));
    outb(0x40, (uint8_t)((divisor >> 8) & 0xFF));
}

void pit_handler(struct regs* r) {
    (void)r;
    pit_ticks++;
}

extern void ps2_keyboard_handler(struct regs* r);

void kernel_main(uint32_t magic, uint32_t mb2_info_addr) {
    // проверка
    if (magic != 0x36d76289) return;

    convert_mb2_to_mb1(mb2_info_addr);

    fb_init(&g_mb_info);
    terminal_initialize();
    input_init();

    irq_init();
    effect_init();
    effect_init_default_handlers();
    pic_remap();
    init_pit();
    init_idt();

    irq_register(32, pit_handler, "PIT");
    irq_register(33, ps2_keyboard_handler, "PS/2");

    outb(0x21, 0xFC);
    asm volatile("sti");

    uintptr_t start_of_bitmap = (uintptr_t)kernel_end;
    pmm_init(&g_mb_info, start_of_bitmap);

    uintptr_t max_memory = g_mb_info.mem_upper * 1024;
    size_t bitmap_size = (max_memory / 4096) / 8;

    uint64_t total_ram = 0;
    if (g_mb_info.flags & MULTIBOOT_INFO_MEM_MAP) {
        multiboot_memory_map_t* mmap = (multiboot_memory_map_t*)(uintptr_t)g_mb_info.mmap_addr;
        uint32_t mmap_end = g_mb_info.mmap_addr + g_mb_info.mmap_length;
        while ((uint32_t)(uintptr_t)mmap < mmap_end) {
            if (mmap->type == 1) total_ram += mmap->len;
            mmap = (multiboot_memory_map_t*)((uintptr_t)mmap + mmap->size + 4);
        }
    }
    g_ram_mb = total_ram / 1024 / 1024;

    heap_end = kernel_end + bitmap_size;
    heap_limit = heap_end;

    init_heap(&g_mb_info);

    smbios_init();

    usb_init();

    acpi_init();
    lapic_timer_init(100);

    jump_to_user();
}