#pragma once
#include <stdint.h>

#define MB2_BOOT_MAGIC  0x36d76289

#define MB2_TAG_END          0
#define MB2_TAG_CMDLINE      1
#define MB2_TAG_BOOTLOADER   2
#define MB2_TAG_MODULE       3
#define MB2_TAG_BASIC_MEM    4
#define MB2_TAG_BOOTDEV      5
#define MB2_TAG_MMAP         6
#define MB2_TAG_VBE          7
#define MB2_TAG_FRAMEBUFFER  8

#define MB2_FB_TYPE_INDEXED  0
#define MB2_FB_TYPE_RGB      1
#define MB2_FB_TYPE_EGA      2

struct mb2_info {
    uint32_t total_size;
    uint32_t reserved;
} __attribute__((packed));

struct mb2_tag {
    uint32_t type;
    uint32_t size;
} __attribute__((packed));

struct mb2_tag_string {
    uint32_t type;
    uint32_t size;
    char     string[];
} __attribute__((packed));

struct mb2_tag_basic_mem {
    uint32_t type;
    uint32_t size;
    uint32_t mem_lower;
    uint32_t mem_upper;
} __attribute__((packed));

struct mb2_tag_mmap {
    uint32_t type;
    uint32_t size;
    uint32_t entry_size;
    uint32_t entry_version;
    uint8_t  entries[];
} __attribute__((packed));

struct mb2_mmap_entry {
    uint64_t addr;
    uint64_t len;
    uint32_t type;
    uint32_t reserved;
} __attribute__((packed));

struct mb2_tag_framebuffer {
    uint32_t type;
    uint32_t size;
    uint64_t addr;
    uint32_t pitch;
    uint32_t width;
    uint32_t height;
    uint8_t  bpp;
    uint8_t  fb_type;
    uint16_t reserved;
} __attribute__((packed));
