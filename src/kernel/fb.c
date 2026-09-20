#include "fb.h"

static uint8_t*  fb_base   = 0;
static uint32_t  fb_w      = 0;
static uint32_t  fb_h      = 0;
static uint32_t  fb_pitch  = 0;
static uint32_t  fb_bpp    = 0;
static int       fb_ok     = 0;

void fb_init(multiboot_info_t* mbd) {
    if (!(mbd->flags & MULTIBOOT_INFO_FRAMEBUFFER_INFO)) return;
    if (mbd->framebuffer_type != MULTIBOOT_FRAMEBUFFER_TYPE_RGB) return;
    if (mbd->framebuffer_bpp != 32) return;

    fb_base  = (uint8_t*)(uintptr_t)mbd->framebuffer_addr;
    fb_w     = mbd->framebuffer_width;
    fb_h     = mbd->framebuffer_height;
    fb_pitch = mbd->framebuffer_pitch;
    fb_bpp   = mbd->framebuffer_bpp;
    fb_ok    = 1;

    fb_clear(0x000000);
}

int fb_available(void) { return fb_ok; }

uint32_t fb_width(void)         { return fb_w; }
uint32_t fb_height(void)        { return fb_h; }
uint32_t fb_pitch_pixels(void)  { return fb_pitch / 4; }

void fb_put_pixel(uint32_t x, uint32_t y, uint32_t rgb) {
    if (x >= fb_w || y >= fb_h) return;
#if FB_HW_MIRROR
    x = fb_w - 1 - x;
#endif
    uint32_t* row = (uint32_t*)(fb_base + y * fb_pitch);
    row[x] = rgb;
}

void fb_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t rgb) {
    if (x >= fb_w || y >= fb_h) return;
    if (x + w > fb_w) w = fb_w - x;
    if (y + h > fb_h) h = fb_h - y;

    for (uint32_t j = 0; j < h; j++) {
        uint32_t* row = (uint32_t*)(fb_base + (y + j) * fb_pitch);
        for (uint32_t i = 0; i < w; i++) {
#if FB_HW_MIRROR
            row[fb_w - 1 - (x + i)] = rgb;
#else
            row[x + i] = rgb;
#endif
        }
    }
}

void fb_clear(uint32_t rgb) {
    fb_fill_rect(0, 0, fb_w, fb_h, rgb);
}

void fb_scroll_up(uint32_t pixels) {
    if (pixels >= fb_h) return;
    uint32_t bytes_to_move = (fb_h - pixels) * fb_pitch;
    uint8_t* dst = fb_base;
    uint8_t* src = fb_base + pixels * fb_pitch;
    for (uint32_t i = 0; i < bytes_to_move; i++) dst[i] = src[i];
    fb_fill_rect(0, fb_h - pixels, fb_w, pixels, 0x000000);
}
