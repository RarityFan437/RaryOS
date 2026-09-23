#include "smbios.h"
#include <stdint.h>

#define SMBIOS_START 0xF0000
#define SMBIOS_END   0x100000

struct smbios_ep {
    char     signature[4];
    uint8_t  checksum;
    uint8_t  length;
    uint8_t  major;
    uint8_t  minor;
    uint16_t max_struct_size;
    uint8_t  revision;
    uint8_t  formatted[5];
    char     dmi_sig[5];
    uint8_t  dmi_checksum;
    uint16_t table_length;
    uint32_t table_address;
    uint16_t num_structures;
    uint8_t  bcd_revision;
} __attribute__((packed));

struct smbios_header {
    uint8_t  type;
    uint8_t  length;
    uint16_t handle;
} __attribute__((packed));

static const uint8_t* smbios_table = 0;
static uint16_t smbios_count = 0;

static int checksum_ok(const uint8_t* p, uint8_t len) {
    uint8_t sum = 0;
    for (uint8_t i = 0; i < len; i++) sum += p[i];
    return sum == 0;
}

static const char* smbios_string(const struct smbios_header* hdr, uint8_t idx) {
    if (idx == 0) return 0;
    const char* p = (const char*)hdr + hdr->length;
    uint8_t n = 1;
    while (*p) {
        if (n == idx) return p;
        while (*p) p++;
        p++;
        n++;
    }
    return 0;
}

void smbios_init(void) {
    smbios_table = 0;
    smbios_count = 0;

    for (uintptr_t addr = SMBIOS_START; addr < SMBIOS_END; addr += 16) {
        const uint8_t* p = (const uint8_t*)addr;
        if (p[0] != '_' || p[1] != 'S' || p[2] != 'M' || p[3] != '_') continue;
        const struct smbios_ep* ep = (const struct smbios_ep*)p;
        if (ep->length < 0x1F) continue;
        if (!checksum_ok(p, ep->length)) continue;
        if (ep->table_address < 0x1000) continue;

        smbios_table = (const uint8_t*)(uintptr_t)ep->table_address;
        smbios_count = ep->num_structures;
        return;
    }
}

static const char* find_field(uint8_t type, uint8_t offset) {
    if (!smbios_table) return 0;
    const uint8_t* p = smbios_table;
    for (uint16_t i = 0; i < smbios_count; i++) {
        const struct smbios_header* hdr = (const struct smbios_header*)p;
        if (hdr->type == 127) break;
        if (hdr->length < 4) break;

        if (hdr->type == type && hdr->length > offset) {
            uint8_t idx = p[offset];
            const char* s = smbios_string(hdr, idx);
            if (s) return s;
        }

        p += hdr->length;
        while (*p || *(p + 1)) {
            if (p[0] == 0 && p[1] == 0) break;
            p++;
        }
        p += 2;
    }
    return 0;
}

const char* smbios_get_board_vendor(void) { return find_field(2, 0x04); }
const char* smbios_get_board_name(void)   { return find_field(2, 0x05); }
