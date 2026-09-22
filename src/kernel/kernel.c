#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "idt.h"
#include "irq.h"
#include "input.h"
#include "stdio.h"
#include "string.h"
#include "multiboot.h"
#include "multiboot2.h"
#include "usb.hpp"
#include "malloc.h"
#include "pmm.h"
#include "shell.h"
#include "acpi.h"
#include "lapic.h"
#include "fb.h"
#include "font8.h"
#include "vmm.h"
#include "pci.hpp"

#define RARYOS_VERSION "v0.2.8"

#define PAGE_PRESENT (1ULL << 0)
#define PAGE_WRITABLE (1ULL << 1)
#define PAGE_CACHE_DISABLE (1ULL << 4)

void init_idt(void);
void pic_remap(void);
void init_pit(void);
void free(void* ptr);

extern int usb_irq_active;

volatile uint64_t pit_ticks = 0;

enum vga_color {
    VGA_COLOR_BLACK = 0,
    VGA_COLOR_BLUE = 1,
    VGA_COLOR_GREEN = 2,
    VGA_COLOR_CYAN = 3,
    VGA_COLOR_RED = 4,
    VGA_COLOR_MAGENTA = 5,
    VGA_COLOR_BROWN = 6,
    VGA_COLOR_LIGHT_GREY = 7,
    VGA_COLOR_DARK_GREY = 8,
    VGA_COLOR_LIGHT_BLUE = 9,
    VGA_COLOR_LIGHT_GREEN = 10,
    VGA_COLOR_LIGHT_CYAN = 11,
    VGA_COLOR_LIGHT_RED = 12,
    VGA_COLOR_LIGHT_MAGENTA = 13,
    VGA_COLOR_LIGHT_BROWN = 14,
    VGA_COLOR_WHITE = 15,
};

static inline uint8_t vga_entry_color(enum vga_color fg, enum vga_color bg) {
    return fg | bg << 4;
}

size_t terminal_row;
size_t terminal_column;
uint8_t terminal_color;

static const uint32_t vga_palette[16] = {
    0x000000, 0x0000AA, 0x00AA00, 0x00AAAA,
    0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
    0x555555, 0x5555FF, 0x55FF55, 0x55FFFF,
    0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF,
};

static uint32_t term_fg = 0xAAAAAA;
static uint32_t term_bg = 0x000000;

static uint16_t* const VGA_TEXT_BUFFER = (uint16_t*)0xB8000;
static const size_t VGA_W = 80;
static const size_t VGA_H = 25;

extern char kernel_end[];
static char* heap_end = kernel_end;
static char* heap_limit = kernel_end;

static multiboot_memory_map_t g_mmap_buffer[128];
static multiboot_info_t       g_mb_info;

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

void terminal_initialize(void) {
    if (fb_available()) {
        fb_clear(term_bg);
    } else {
        for (size_t i = 0; i < VGA_W * VGA_H; i++) {
            VGA_TEXT_BUFFER[i] = (uint16_t)' ' | (uint16_t)(0x07 << 8);
        }
    }
    terminal_row = 0;
    terminal_column = 0;
    terminal_color = vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
}

void terminal_setcolor(uint8_t color) {
    terminal_color = color;
    term_fg = vga_palette[color & 0x0F];
    term_bg = vga_palette[(color >> 4) & 0x0F];
}


#define FB_SCALE 2 
#define CHAR_H (8 * FB_SCALE)
#define CHAR_W (8 * FB_SCALE)

static void draw_char_fb(int cx, int cy, char c) {
    if ((unsigned char)c < 32 || (unsigned char)c > 126) c = '?';
    
    size_t char_index = (unsigned char)c - 32;
    const uint8_t* glyph = font_data[char_index];

    uint32_t px = (uint32_t)cx;
    uint32_t py = (uint32_t)cy;

    for (int row = 0; row < 8; row++) {
        uint8_t bits = glyph[row];

        for (int col = 0; col < 8; col++) {
            uint32_t color = (bits & ((uint8_t)1 << (7 - col))) ? term_fg : term_bg;
            
            fb_fill_rect(px + (uint32_t)col * FB_SCALE,
                         py + (uint32_t)row * FB_SCALE,
                         FB_SCALE, FB_SCALE, color);
        }
    }
}




void terminal_putentryat(char c, uint8_t color, size_t x, size_t y) {
    if (fb_available()) {
        (void)color;
        draw_char_fb((int)x, (int)y, c);
    } else {
        if (x >= VGA_W || y >= VGA_H) return;
        VGA_TEXT_BUFFER[y * VGA_W + x] = (uint16_t)(uint8_t)c | (uint16_t)(color << 8);
    }
}

void clear_terminal(void) {
    if (fb_available()) fb_clear(term_bg);
    else for (size_t i = 0; i < VGA_W * VGA_H; i++)
        VGA_TEXT_BUFFER[i] = (uint16_t)' ' | (uint16_t)(0x07 << 8);
    terminal_row = 0;
    terminal_column = 0;
}

static void scroll_fb(void) {
    fb_scroll_up(CHAR_H);
    terminal_row = fb_height() / CHAR_H - 1;
    terminal_column = 0;
}

static void scroll_vga(void) {
    for (size_t y = 1; y < VGA_H; y++)
        for (size_t x = 0; x < VGA_W; x++)
            VGA_TEXT_BUFFER[(y - 1) * VGA_W + x] = VGA_TEXT_BUFFER[y * VGA_W + x];
    for (size_t x = 0; x < VGA_W; x++)
        VGA_TEXT_BUFFER[(VGA_H - 1) * VGA_W + x] = (uint16_t)' ' | (uint16_t)(0x07 << 8);
    terminal_row = VGA_H - 1;
    terminal_column = 0;
}

void terminal_putchar(char c) {
    if (fb_available()) {
        if (c == '\n') {
            terminal_column = 0;
            terminal_row++;
            if ((terminal_row + 1) * CHAR_H > fb_height()) scroll_fb();
            return;
        }
        if (c == '\b') {
            if (terminal_column >= CHAR_W) {
                terminal_column -= CHAR_W;
            } else {
                terminal_column = 0;
            }
            draw_char_fb(terminal_column, terminal_row * CHAR_H, ' ');
            return;
        }
        
        int current_y_pixels = terminal_row * CHAR_H;
        draw_char_fb(terminal_column, current_y_pixels, c);
        
        terminal_column += CHAR_W;
        
        if (terminal_column + CHAR_W > fb_width()) {
            terminal_column = 0;
            terminal_row++;
            if ((terminal_row + 1) * CHAR_H > fb_height()) scroll_fb();
        }
        return;
    }


    if (c == '\n') {
        terminal_column = 0;
        terminal_row++;
        if (terminal_row == VGA_H) scroll_vga();
        return;
    }
    if (c == '\b') {
        if (terminal_column > 0) terminal_column--;
        else if (terminal_row > 0) {
            terminal_row--;
            terminal_column = VGA_W - 1;
        }
        VGA_TEXT_BUFFER[terminal_row * VGA_W + terminal_column] =
            (uint16_t)' ' | (uint16_t)(0x07 << 8);
        return;
    }
    VGA_TEXT_BUFFER[terminal_row * VGA_W + terminal_column] =
        (uint16_t)(uint8_t)c | (uint16_t)(terminal_color << 8);
    terminal_column++;
    if (terminal_column == VGA_W) {
        terminal_column = 0;
        terminal_row++;
        if (terminal_row == VGA_H) scroll_vga();
    }
}

void terminal_write(const char* data, size_t size) {
    for (size_t i = 0; i < size; i++) terminal_putchar(data[i]);
}

void terminal_write_string(const char* data) {
    terminal_write(data, strlen(data));
}

static inline void outb_main(uint16_t port, uint8_t val) {
    asm volatile ( "outb %b0, %w1" : : "a"(val), "Nd"(port) : "memory" );
}

void init_pit(void) {
    uint32_t divisor = 1193182 / 100;
    outb_main(0x43, 0x36);
    outb_main(0x40, (uint8_t)(divisor & 0xFF));
    outb_main(0x40, (uint8_t)((divisor >> 8) & 0xFF));
}

void pit_handler(struct regs* r) {
    (void)r;
    pit_ticks++;
}

extern void ps2_keyboard_handler(struct regs* r);

void kernel_main(uint32_t magic, uint32_t mb2_info_addr) {
    (void)magic;

    convert_mb2_to_mb1(mb2_info_addr);

    fb_init(&g_mb_info);
    terminal_initialize();
    input_init();

    irq_init();
    pic_remap();
    init_pit();
    init_idt();

    irq_register(32, pit_handler, "PIT");
    irq_register(33, ps2_keyboard_handler, "PS/2");

    outb_main(0x21, 0xFC);
    asm volatile("sti");

    printf("========================================\n");
    printf("      Welcome to RaryOS %s        \n", RARYOS_VERSION);
    printf("========================================\n\n");

    uintptr_t start_of_bitmap = (uintptr_t)kernel_end;
    pmm_init(&g_mb_info, start_of_bitmap);

    uintptr_t max_memory = g_mb_info.mem_upper * 1024;
    size_t bitmap_size = (max_memory / 4096) / 8;

    heap_end = kernel_end + bitmap_size;
    heap_limit = heap_end;

    init_heap(&g_mb_info);

    char* loader_name = "Unknown Bootloader";
    if (g_mb_info.flags & MULTIBOOT_INFO_BOOT_LOADER_NAME)
        loader_name = (char*)(uintptr_t)g_mb_info.boot_loader_name;

    usb_init();

    printf("Loader Name : %s\n", loader_name);

    if (g_mb_info.flags & MULTIBOOT_INFO_MEM_MAP) {
        uint64_t total_ram = 0;
        multiboot_memory_map_t* mmap = (multiboot_memory_map_t*)(uintptr_t)g_mb_info.mmap_addr;
        uint32_t mmap_end = g_mb_info.mmap_addr + g_mb_info.mmap_length;
        while ((uint32_t)(uintptr_t)mmap < mmap_end) {
            if (mmap->type == 1) total_ram += mmap->len;
            mmap = (multiboot_memory_map_t*)((uintptr_t)mmap + mmap->size + 4);
        }
        printf("Available RAM: %d MB\n", (int)(total_ram / 1024 / 1024));
    } else if (g_mb_info.flags & MULTIBOOT_INFO_MEMORY) {
        printf("Available RAM: %d MB\n", g_mb_info.mem_upper / 1024);
    }

    printf("\n----------------------------------------\n");

    acpi_init();
    lapic_timer_init(100);

    printf("\n----------------------------------------\n");
    printf("Type 'help' for available commands.\n\n");

    shell_run();
}