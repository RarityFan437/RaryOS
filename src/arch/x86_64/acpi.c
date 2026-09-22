#include "acpi.h"
#include "io.h"
#include "stdio.h"

struct rsdp {
    char     signature[8];
    uint8_t  checksum;
    char     oem_id[6];
    uint8_t  revision;
    uint32_t rsdt_address;
    uint32_t length;
    uint64_t xsdt_address;
    uint8_t  extended_checksum;
    uint8_t  reserved[3];
} __attribute__((packed));

struct sdt_header {
    char     signature[4];
    uint32_t length;
    uint8_t  revision;
    uint8_t  checksum;
    char     oem_id[6];
    char     oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} __attribute__((packed));

static uint32_t pm1a_cnt_blk = 0;
static uint32_t pm1b_cnt_blk = 0;
static uint8_t  slp_typa    = 5;
static uint8_t  slp_typb    = 5;
static int      have_s5     = 0;
static int      is_qemu     = 0;

static int memcmp_(const void* a, const void* b, int n) {
    const uint8_t* x = (const uint8_t*)a;
    const uint8_t* y = (const uint8_t*)b;
    for (int i = 0; i < n; i++) if (x[i] != y[i]) return x[i] - y[i];
    return 0;
}

static uint8_t acpi_checksum(const void* data, uint32_t length) {
    const uint8_t* p = (const uint8_t*)data;
    uint8_t sum = 0;
    for (uint32_t i = 0; i < length; i++) sum += p[i];
    return sum;
}

static struct rsdp* find_rsdp(void) {
    uint16_t ebda_seg;
    asm volatile("movw 0x40E, %0" : "=r"(ebda_seg));

    if (ebda_seg) {
        uintptr_t ebda = (uintptr_t)ebda_seg << 4;
        for (uintptr_t p = ebda; p < ebda + 1024; p += 16) {
            if (memcmp_((void*)p, "RSD PTR ", 8) == 0 &&
                acpi_checksum((void*)p, 20) == 0)
                return (struct rsdp*)p;
        }
    }
    for (uintptr_t p = 0xE0000; p < 0x100000; p += 16) {
        if (memcmp_((void*)p, "RSD PTR ", 8) == 0 &&
            acpi_checksum((void*)p, 20) == 0)
            return (struct rsdp*)p;
    }
    return 0;
}

static int parse_int_element(const uint8_t* p, uint32_t length, uint32_t* off, uint8_t* out) {
    if (*off >= length) return 0;

    uint8_t op = p[*off];

    if (op == 0x00) { *out = 0;    (*off)++; return 1; }
    if (op == 0x01) { *out = 1;    (*off)++; return 1; }
    if (op == 0xFF) { *out = 0xFF; (*off)++; return 1; }
    if (op == 0x0A) {
        if (*off + 1 >= length) return 0;
        *out = p[*off + 1];
        *off += 2;
        return 1;
    }
    if (op == 0x0B) {
        if (*off + 2 >= length) return 0;
        *out = p[*off + 1];
        *off += 3;
        return 1;
    }
    if (op == 0x0C) {
        if (*off + 4 >= length) return 0;
        *out = p[*off + 1];
        *off += 5;
        return 1;
    }
    if (op == 0x0E) {
        if (*off + 8 >= length) return 0;
        *out = p[*off + 1];
        *off += 9;
        return 1;
    }
    if (op >= 0x02) {
        *out = op;
        (*off)++;
        return 1;
    }
    return 0;
}

static int find_s5(const uint8_t* dsdt, uint32_t length,
                   uint8_t* out_a, uint8_t* out_b)
{
    for (uint32_t i = 0; i + 4 < length; i++) {
        if (dsdt[i]   != '_' || dsdt[i+1] != 'S' ||
            dsdt[i+2] != '5' || dsdt[i+3] != '_') continue;

        uint32_t off = i + 4;

        if (off < length && dsdt[off] == 0x08) off++;

        if (off < length && dsdt[off] == 0x14) {
            uint8_t pkglen = dsdt[off + 1];
            uint32_t pkglen_size = 1;
            if ((pkglen & 0xC0) == 0x40) pkglen_size = 2;
            else if ((pkglen & 0xC0) == 0x80) pkglen_size = 3;
            else if ((pkglen & 0xC0) == 0xC0) pkglen_size = 4;
            off += 1 + pkglen_size + 1;

            for (uint32_t k = 0; k < 16 && off + k < length; k++) {
                if (dsdt[off + k] == 0x12) { off += k; break; }
            }
        }

        if (off >= length || dsdt[off] != 0x12) continue;
        off++;

        uint8_t pkglen = dsdt[off];
        uint32_t pkglen_size = 1;
        if ((pkglen & 0xC0) == 0x40) pkglen_size = 2;
        else if ((pkglen & 0xC0) == 0x80) pkglen_size = 3;
        else if ((pkglen & 0xC0) == 0xC0) pkglen_size = 4;
        off += pkglen_size;

        if (off >= length) continue;
        uint8_t num = dsdt[off];
        off++;
        if (num < 2) continue;

        uint8_t vals[4] = {0, 0, 0, 0};
        for (uint32_t e = 0; e < 4 && e < num; e++) {
            if (!parse_int_element(dsdt, length, &off, &vals[e])) break;
        }

        *out_a = vals[0];
        *out_b = vals[1];
        return 1;
    }
    return 0;
}

static void try_table_s5(struct sdt_header* hdr) {
    if (have_s5) return;

    uint8_t a = 0, b = 0;
    if (!find_s5((uint8_t*)hdr, hdr->length, &a, &b)) return;

    if (a == 0 && b == 0) {
        slp_typa = 5;
        slp_typb = 5;
        have_s5  = 1;
        return;
    }

    slp_typa = a;
    slp_typb = b;
    have_s5  = 1;
}

static void detect_qemu(struct sdt_header* sdt) {
    if (memcmp_(sdt->oem_id, "BOCHS ", 6) == 0 ||
        memcmp_(sdt->oem_id, "QEMU  ", 6) == 0) {
        is_qemu = 1;
    }
}

void acpi_init(void) {
    struct rsdp* rsdp = find_rsdp();
    if (!rsdp) return;

    uint32_t sdt_phys;
    uint32_t entry_size;
    if (rsdp->revision >= 2 && rsdp->xsdt_address) {
        sdt_phys   = (uint32_t)(rsdp->xsdt_address & 0xFFFFFFFFu);
        entry_size = 8;
    } else {
        sdt_phys   = rsdp->rsdt_address;
        entry_size = 4;
    }

    struct sdt_header* sdt = (struct sdt_header*)(uintptr_t)sdt_phys;
    if (memcmp_(sdt->signature, "RSDT", 4) != 0 &&
        memcmp_(sdt->signature, "XSDT", 4) != 0) return;

    detect_qemu(sdt);

    uint32_t entries = (sdt->length - sizeof(struct sdt_header)) / entry_size;
    uint8_t* base = (uint8_t*)sdt + sizeof(struct sdt_header);

    uint32_t dsdt_phys = 0;

    for (uint32_t i = 0; i < entries; i++) {
        uint64_t table_phys;
        if (entry_size == 8) table_phys = *(uint64_t*)(base + i * 8);
        else                 table_phys = *(uint32_t*)(base + i * 4);

        struct sdt_header* hdr = (struct sdt_header*)(uintptr_t)table_phys;

        if (memcmp_(hdr->signature, "FACP", 4) == 0) {
            uint8_t* fadt = (uint8_t*)hdr;

            pm1a_cnt_blk = *(uint32_t*)(fadt + 0x40);
            pm1b_cnt_blk = *(uint32_t*)(fadt + 0x44);

            uint32_t dsdt32 = *(uint32_t*)(fadt + 0x28);
            uint64_t x_dsdt = 0;

            if (hdr->length >= 0x94) {
                uint32_t lo = *(uint32_t*)(fadt + 0x8C);
                uint32_t hi = *(uint32_t*)(fadt + 0x90);
                x_dsdt = ((uint64_t)hi << 32) | lo;
            }

            dsdt_phys = (uint32_t)(x_dsdt ? x_dsdt : dsdt32);
        }
        else if (memcmp_(hdr->signature, "SSDT", 4) == 0) {
            try_table_s5(hdr);
        }
    }

    if (dsdt_phys) {
        struct sdt_header* dsdt = (struct sdt_header*)(uintptr_t)dsdt_phys;
        if (memcmp_(dsdt->signature, "DSDT", 4) == 0) {
            try_table_s5(dsdt);
        }
    }

    if (!have_s5) {
        slp_typa = 5;
        slp_typb = 5;
    }
}

void acpi_shutdown(void) {
    if (pm1a_cnt_blk != 0) {
        uint16_t val_a = (uint16_t)(((uint16_t)slp_typa << 10) | (1u << 13));
        outw((uint16_t)pm1a_cnt_blk, val_a);

        if (pm1b_cnt_blk) {
            uint16_t val_b = (uint16_t)(((uint16_t)slp_typb << 10) | (1u << 13));
            outw((uint16_t)pm1b_cnt_blk, val_b);
        }
    } else {
        uint16_t candidates[] = { 0x604, 0x4004, 0xB004 };
        for (int i = 0; i < 3; i++) {
            uint16_t v = (uint16_t)(((uint16_t)slp_typa << 10) | (1u << 13));
            outw(candidates[i], v);
        }
    }

    for (volatile int i = 0; i < 100000000; i++) { }

    if (is_qemu) {
        outl(0x501, 0);
    }

    for (;;) asm volatile("hlt");
}

void acpi_reboot(void) {
    for (int i = 0; i < 100000; i++) {
        if ((inb(0x64) & 0x02) == 0) break;
    }
    outb(0x64, 0xFE);

    struct { uint16_t limit; uint64_t base; } __attribute__((packed)) null_idt = {0, 0};
    asm volatile("lidt %0" : : "m"(null_idt));
    asm volatile("int $0");
    for (;;) asm volatile("hlt");
}