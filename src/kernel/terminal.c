#include <stdint.h>
#include <stddef.h>

#include "font8.h"
#include "fb.h"
#include "string.h"

size_t terminal_row;
size_t terminal_column;
uint8_t terminal_color;

static uint32_t term_fg = 0xAAAAAA;
static uint32_t term_bg = 0x000000;

static const uint32_t vga_palette[16] = {
    0x000000, 0x0000AA, 0x00AA00, 0x00AAAA,
    0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
    0x555555, 0x5555FF, 0x55FF55, 0x55FFFF,
    0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF,
};

// VGA ================================================================================

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

static uint16_t* const VGA_TEXT_BUFFER = (uint16_t*)0xB8000;
static const size_t VGA_W = 80;
static const size_t VGA_H = 25;

static void scroll_vga(void) {
    for (size_t y = 1; y < VGA_H; y++)
        for (size_t x = 0; x < VGA_W; x++)
            VGA_TEXT_BUFFER[(y - 1) * VGA_W + x] = VGA_TEXT_BUFFER[y * VGA_W + x];
    for (size_t x = 0; x < VGA_W; x++)
        VGA_TEXT_BUFFER[(VGA_H - 1) * VGA_W + x] = (uint16_t)' ' | (uint16_t)(0x07 << 8);
    terminal_row = VGA_H - 1;
    terminal_column = 0;
}

//     ================================================================================

// FB  ================================================================================

#define FB_SCALE 2 
#define CHAR_H (8 * FB_SCALE)
#define CHAR_W (8 * FB_SCALE)

static void draw_char_fb(int cx, int cy, char c) {
    if (cx < 0 || cy < 0) return;

    if ((unsigned char)c < 32 || (unsigned char)c > 126) c = '?';
    
    size_t char_index = (unsigned char)c - 32;
    const uint8_t* glyph = font_data[char_index];

    fb_draw_glyph((uint32_t)cx, (uint32_t)cy, glyph, term_fg, term_bg, FB_SCALE);
}

static void scroll_fb(void) {
    fb_scroll_up(CHAR_H);
    terminal_row = fb_height() / CHAR_H - 1;
    terminal_column = 0;
}

//     ================================================================================

// GENERAL  ===========================================================================

void clear_terminal(void) {
    if (fb_available()) fb_clear(term_bg);
    else for (size_t i = 0; i < VGA_W * VGA_H; i++)
        VGA_TEXT_BUFFER[i] = (uint16_t)' ' | (uint16_t)(0x07 << 8);
    terminal_row = 0;
    terminal_column = 0;
}

void terminal_initialize(void) {
    clear_terminal();
    terminal_color = vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
}

void terminal_setcolor(uint8_t color) {
    terminal_color = color;
    term_fg = vga_palette[color & 0x0F];
    term_bg = vga_palette[(color >> 4) & 0x0F];
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
