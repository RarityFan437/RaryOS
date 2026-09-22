#pragma once
#include <stdint.h>

#define MULTIBOOT_INFO_MEMORY          0x00000001
#define MULTIBOOT_INFO_BOOTDEV         0x00000002
#define MULTIBOOT_INFO_CMDLINE         0x00000004
#define MULTIBOOT_INFO_MODS            0x00000008
#define MULTIBOOT_INFO_AOUT_SYMS       0x00000010
#define MULTIBOOT_INFO_ELF_SHDR        0x00000020
#define MULTIBOOT_INFO_MEM_MAP         0x00000040
#define MULTIBOOT_INFO_DRIVE_INFO      0x00000080
#define MULTIBOOT_INFO_CONFIG_TABLE    0x00000100
#define MULTIBOOT_INFO_BOOT_LOADER_NAME 0x00000200
#define MULTIBOOT_INFO_APM_TABLE       0x00000400
#define MULTIBOOT_INFO_VBE_INFO        0x00000800
#define MULTIBOOT_INFO_FRAMEBUFFER_INFO 0x00001000

#define MULTIBOOT_BOOTLOADER_MAGIC     0x2BADB002

typedef struct {
    uint32_t tabsize;
    uint32_t strsize;
    uint32_t addr;
    uint32_t pad;
} __attribute__((packed)) multiboot_aout_symbol_table_t;

typedef struct {
    uint32_t num;
    uint32_t size;
    uint32_t addr;
    uint32_t shndx;
} __attribute__((packed)) multiboot_elf_section_header_table_t;

typedef struct {
    uint32_t mod_start;
    uint32_t mod_end;
    uint32_t cmdline;
    uint32_t pad;
} __attribute__((packed)) multiboot_module_t;

typedef struct {
    uint32_t size;
    uint64_t addr;
    uint64_t len;
    uint32_t type;
} __attribute__((packed)) multiboot_memory_map_t;

typedef struct {
    uint32_t size;
    uint8_t  drive_number;
    uint8_t  drive_mode;
    uint16_t drive_cylinders;
    uint8_t  drive_heads;
    uint8_t  drive_sectors;
    uint16_t drive_ports;
} __attribute__((packed)) multiboot_drive_t;

typedef struct {
    uint16_t version;
    uint16_t cseg;
    uint32_t offset;
    uint16_t cseg_16;
    uint16_t dseg;
    uint16_t flags;
    uint16_t cseg_len;
    uint16_t cseg_16_len;
    uint16_t dseg_len;
} __attribute__((packed)) multiboot_apm_info_t;

typedef struct {
    uint32_t flags;

    uint32_t mem_lower;
    uint32_t mem_upper;

    uint32_t boot_device;

    uint32_t cmdline;

    uint32_t mods_count;
    uint32_t mods_addr;

    union {
        multiboot_aout_symbol_table_t aout_sym;
        multiboot_elf_section_header_table_t elf_sec;
    } u;

    uint32_t mmap_length;
    uint32_t mmap_addr;

    uint32_t drives_length;
    uint32_t drives_addr;

    uint32_t config_table;

    uint32_t boot_loader_name;

    uint32_t apm_table;

    uint32_t vbe_control_info;
    uint32_t vbe_mode_info;
    uint16_t vbe_mode;
    uint16_t vbe_interface_seg;
    uint16_t vbe_interface_off;
    uint16_t vbe_interface_len;

    uint64_t framebuffer_addr;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t  framebuffer_bpp;
#define MULTIBOOT_FRAMEBUFFER_TYPE_INDEXED 0
#define MULTIBOOT_FRAMEBUFFER_TYPE_RGB     1
#define MULTIBOOT_FRAMEBUFFER_TYPE_EGA     2
    uint8_t  framebuffer_type;
    
    union {
        struct {
            uint32_t framebuffer_palette_addr;
            uint16_t framebuffer_palette_num_colors;
        };
        struct {
            uint8_t framebuffer_red_field_position;
            uint8_t framebuffer_red_mask_size;
            uint8_t framebuffer_green_field_position;
            uint8_t framebuffer_green_mask_size;
            uint8_t framebuffer_blue_field_position;
            uint8_t framebuffer_blue_mask_size;
        };
    };
} __attribute__((packed)) multiboot_info_t;
