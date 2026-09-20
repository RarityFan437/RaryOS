#pragma once
#include <stdint.h>
#include <stddef.h>
#include "multiboot.h"

#define FB_HW_MIRROR 0

void fb_init(multiboot_info_t* mbd);
int  fb_available(void);

uint32_t fb_width(void);
uint32_t fb_height(void);
uint32_t fb_pitch_pixels(void);

void fb_put_pixel(uint32_t x, uint32_t y, uint32_t rgb);
void fb_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t rgb);
void fb_clear(uint32_t rgb);
void fb_scroll_up(uint32_t pixels);
