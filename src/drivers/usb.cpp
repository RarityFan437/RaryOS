#include "usb.hpp"
#include "pci.hpp"

extern "C" {
#include "stdio.h"
#include "string.h"
#include "io.h"
#include "irq.h"
#include "input.h"
#include "vmm.h"
#include "pcidf.h"
}

#define PAGE_PRESENT (1ULL << 0)
#define PAGE_WRITABLE (1ULL << 1)
#define PAGE_CACHE_DISABLE (1ULL << 4)

extern "C" void usb_handler_c(struct regs* r);

static uint32_t pci_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
static void     pci_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val);
static uint64_t pci_read_bar64(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, int* is_io);

namespace {
    constexpr uint32_t XHCI_USBCMD_RS      = (1u << 0);
    constexpr uint32_t XHCI_USBCMD_HCRST   = (1u << 1);
    constexpr uint32_t XHCI_USBCMD_INTE    = (1u << 2);
    constexpr uint32_t XHCI_USBSTS_HCH     = (1u << 0);
    constexpr uint32_t XHCI_USBSTS_EINT    = (1u << 3);
    constexpr uint32_t XHCI_USBSTS_CNR     = (1u << 11);
    constexpr uint32_t XHCI_PORTSC_CCS     = (1u << 0);
    constexpr uint32_t XHCI_PORTSC_PP      = (1u << 9);
    constexpr uint32_t XHCI_PORTSC_PR      = (1u << 4);
    constexpr uint32_t XHCI_PORTSC_PRC     = (1u << 21);
    constexpr uint32_t XHCI_PORTSC_CSC     = (1u << 17);
    constexpr uint32_t XHCI_PORTSC_PEC     = (1u << 18);
    constexpr uint32_t XHCI_PORTSC_WRC     = (1u << 19);
    constexpr uint32_t XHCI_PORTSC_OCC     = (1u << 20);
    constexpr uint32_t XHCI_PORTSC_PLC     = (1u << 22);
    constexpr uint32_t XHCI_PORTSC_CEC     = (1u << 23);

    constexpr uint32_t XHCI_TRB_LINK        = 6;
    constexpr uint32_t XHCI_TRB_SETUP       = 2;
    constexpr uint32_t XHCI_TRB_DATA        = 3;
    constexpr uint32_t XHCI_TRB_STATUS      = 4;
    constexpr uint32_t XHCI_TRB_ENABLE_SLOT = 9;
    constexpr uint32_t XHCI_TRB_ADDRESS_DEV = 11;
    constexpr uint32_t XHCI_TRB_CONFIG_EP   = 12;
    constexpr uint32_t XHCI_TRB_NORMAL      = 1;
    constexpr uint32_t XHCI_TRB_XFER_EVT    = 32;
    constexpr uint32_t XHCI_TRB_CMD_CMPL    = 33;
    constexpr uint32_t XHCI_TRB_PORTSC      = 34;

    volatile uint64_t g_usb_isr_count = 0;
    volatile uint64_t g_usb_keys_count = 0;
    volatile uint64_t g_usb_push_count = 0;
    volatile uint64_t g_usb_bad_code_count = 0;
    int g_usb_xfer_pending = 0;

    XhciController static_xhci;
}

static char hid_key_to_ascii(uint8_t k) {
    switch (k) {
        case 0x04: return 'a'; case 0x05: return 'b'; case 0x06: return 'c';
        case 0x07: return 'd'; case 0x08: return 'e'; case 0x09: return 'f';
        case 0x0A: return 'g'; case 0x0B: return 'h'; case 0x0C: return 'i';
        case 0x0D: return 'j'; case 0x0E: return 'k'; case 0x0F: return 'l';
        case 0x10: return 'm'; case 0x11: return 'n'; case 0x12: return 'o';
        case 0x13: return 'p'; case 0x14: return 'q'; case 0x15: return 'r';
        case 0x16: return 's'; case 0x17: return 't'; case 0x18: return 'u';
        case 0x19: return 'v'; case 0x1A: return 'w'; case 0x1B: return 'x';
        case 0x1C: return 'y'; case 0x1D: return 'z';
        case 0x1E: return '1'; case 0x1F: return '2'; case 0x20: return '3';
        case 0x21: return '4'; case 0x22: return '5'; case 0x23: return '6';
        case 0x24: return '7'; case 0x25: return '8'; case 0x26: return '9';
        case 0x27: return '0';
        case 0x28: return '\n'; case 0x29: return 27;   case 0x2A: return '\b';
        case 0x2B: return '\t'; case 0x2C: return ' ';
        case 0x2D: return '-';  case 0x2E: return '=';  case 0x2F: return '[';
        case 0x30: return ']';  case 0x31: return '\\';
        case 0x33: return ';';  case 0x34: return '\''; case 0x35: return '`';
        case 0x36: return ',';  case 0x37: return '.';  case 0x38: return '/';
        default:   return 0;
    }
}

static inline void mmio_wr32(volatile void* p, uint32_t v) {
    *(volatile uint32_t*)p = v;
    asm volatile("mfence" ::: "memory");
}

static inline uint32_t mmio_rd32(volatile void* p) {
    uint32_t v = *(volatile uint32_t*)p;
    asm volatile("mfence" ::: "memory");
    return v;
}

static void lapic_enable(void) {
    uint32_t lo, hi;
    asm volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(0x1B));
    lo |= (1u << 11);
    asm volatile("wrmsr" : : "a"(lo), "d"(hi), "c"(0x1B));

    volatile uint32_t* lapic = (volatile uint32_t*)0xFEE00000;
    uint32_t svr = lapic[0xF0 / 4];
    if ((svr & 0xFF) == 0) svr = (svr & ~0xFFu) | 0xFFu;
    svr |= (1u << 8);
    lapic[0xF0 / 4] = svr;
    lapic[0x80 / 4] = 0;
}

void XhciController::parse_caps(uintptr_t mmio_base) {
    mmio = reinterpret_cast<volatile uint8_t*>(mmio_base);
    uint32_t cap0 = mmio_rd32(mmio + 0x00);
    caplength = cap0 & 0xFF;

    uint32_t cap1 = mmio_rd32(mmio + 0x04);
    max_slots = cap1 & 0xFF;
    max_intrs = (cap1 >> 8) & 0x7FF;
    max_ports = (cap1 >> 24) & 0xFF;

    uint32_t cap2 = mmio_rd32(mmio + 0x08);
    num_scratchpad = (((cap2 >> 21) & 0x1F) << 5) | (cap2 & 0xFF);

    uint32_t dboff  = mmio_rd32(mmio + 0x14);
    uint32_t rtsoff = mmio_rd32(mmio + 0x18);

    op    = mmio + caplength;
    db    = reinterpret_cast<volatile uint32_t*>(mmio + (dboff & ~0x3u));
    rt    = mmio + (rtsoff & ~0x1Fu);
    ports = op + 0x400;
}

void XhciController::halt() {
    mmio_wr32(op + 0x00, 0);
    for (int i = 0; i < 1000000; i++)
        if (mmio_rd32(op + 0x04) & XHCI_USBSTS_HCH) break;
}

void XhciController::reset() {
    for (int i = 0; i < 1000000; i++)
        if (!(mmio_rd32(op + 0x04) & XHCI_USBSTS_CNR)) break;
    halt();
    mmio_wr32(op + 0x00, XHCI_USBCMD_HCRST);
    for (int i = 0; i < 10000000; i++)
        if (!(mmio_rd32(op + 0x00) & XHCI_USBCMD_HCRST)) break;
    for (int i = 0; i < 10000000; i++)
        if (!(mmio_rd32(op + 0x04) & XHCI_USBSTS_CNR)) break;
    halt();
    pagesize = mmio_rd32(op + 0x08);
}

void XhciController::setup_dcbaap() {
    memset(dcbaap, 0, sizeof(dcbaap));
    size_t sp = num_scratchpad > MAX_SCRATCHPAD ? MAX_SCRATCHPAD : num_scratchpad;
    if (sp > 0) {
        for (size_t i = 0; i < sp; i++) {
            memset(scratchpad_data[i], 0, 4096);
            scratchpad_array[i] = reinterpret_cast<uint64_t>(scratchpad_data[i]);
        }
        dcbaap[0] = reinterpret_cast<uint64_t>(scratchpad_array);
    }
    dcbaap_phys = reinterpret_cast<uint64_t>(dcbaap);
    mmio_wr32(op + 0x30, static_cast<uint32_t>(dcbaap_phys));
    mmio_wr32(op + 0x34, static_cast<uint32_t>(dcbaap_phys >> 32));
}

void XhciController::setup_cmd_ring() {
    memset(cmd_ring, 0, sizeof(cmd_ring));
    cmd_ring_phys = reinterpret_cast<uint64_t>(cmd_ring);
    cmd_idx = 0;
    cmd_cycle = 1;

    cmd_ring[RING_TRBS - 1].parameter = cmd_ring_phys;
    cmd_ring[RING_TRBS - 1].control = (XHCI_TRB_LINK << 10) | (1u << 1) | 1u;

    mmio_wr32(op + 0x18, static_cast<uint32_t>(cmd_ring_phys) | 1u);
    mmio_wr32(op + 0x1C, static_cast<uint32_t>(cmd_ring_phys >> 32));
}

void XhciController::setup_evt_ring() {
    memset(evt_ring, 0, sizeof(evt_ring));
    evt_ring_phys = reinterpret_cast<uint64_t>(evt_ring);
    evt_idx = 0;
    evt_cycle = 1;

    erst[0].rsba     = evt_ring_phys;
    erst[0].rssz     = RING_TRBS;
    erst[0].reserved = 0;
    erst_phys        = reinterpret_cast<uint64_t>(&erst[0]);

    volatile uint8_t* ir = rt + 0x20;
    mmio_wr32(ir + 0x08, 1);
    mmio_wr32(ir + 0x18, static_cast<uint32_t>(evt_ring_phys));
    mmio_wr32(ir + 0x1C, static_cast<uint32_t>(evt_ring_phys >> 32));
    mmio_wr32(ir + 0x10, static_cast<uint32_t>(erst_phys));
    mmio_wr32(ir + 0x14, static_cast<uint32_t>(erst_phys >> 32));
}

void XhciController::start() {
    mmio_wr32(op + 0x00, XHCI_USBCMD_RS);
    for (int i = 0; i < 1000000; i++)
        if (!(mmio_rd32(op + 0x04) & XHCI_USBSTS_HCH)) break;
}

void XhciController::evt_advance() {
    evt_idx++;
    if (evt_idx == RING_TRBS) {
        evt_idx = 0;
        evt_cycle ^= 1;
    }
    volatile uint8_t* ir = rt + 0x20;
    uint64_t erdp = evt_ring_phys + static_cast<uint64_t>(evt_idx) * 16;
    mmio_wr32(ir + 0x18, static_cast<uint32_t>(erdp) | (1u << 3));
    mmio_wr32(ir + 0x1C, static_cast<uint32_t>(erdp >> 32));
}

int XhciController::wait_cmd(uint64_t cmd_phys, uint8_t* out_slot) {
    for (int t = 0; t < 20000000; t++) {
        trb_t* e = &evt_ring[evt_idx];
        uint32_t ctrl = e->control;
        if ((ctrl & 1) != evt_cycle) continue;

        uint8_t type = (ctrl >> 10) & 0x3F;
        if (type == XHCI_TRB_CMD_CMPL) {
            uint8_t code = (e->status >> 24) & 0xFF;
            uint8_t slot = (ctrl >> 24) & 0xFF;
            uint64_t p = e->parameter;
            evt_advance();
            if (p == cmd_phys) {
                if (out_slot) *out_slot = slot;
                return static_cast<int>(code);
            }
        } else if (type == XHCI_TRB_PORTSC || type == XHCI_TRB_XFER_EVT) {
            evt_advance();
        }
    }
    return -1;
}

int XhciController::submit_cmd(uint32_t type, uint64_t param, uint32_t status, uint8_t slot_id, uint8_t* out_slot) {
    if (cmd_idx == RING_TRBS - 1) {
        cmd_ring[RING_TRBS - 1].control =
            (XHCI_TRB_LINK << 10) | (1u << 1) | static_cast<uint32_t>(cmd_cycle);
        cmd_idx = 0;
        cmd_cycle ^= 1;
    }
    uint64_t phys = cmd_ring_phys + static_cast<uint64_t>(cmd_idx) * 16;
    cmd_ring[cmd_idx].parameter = param;
    cmd_ring[cmd_idx].status    = status;
    cmd_ring[cmd_idx].control   = (type << 10) | (static_cast<uint32_t>(slot_id) << 24) | cmd_cycle;

    asm volatile("mfence" ::: "memory");
    *db = 0;
    asm volatile("mfence" ::: "memory");

    cmd_idx++;
    return wait_cmd(phys, out_slot);
}

void XhciController::ep0_init() {
    memset(ep0_ring, 0, sizeof(ep0_ring));
    ep0_idx = 0;
    ep0_cycle = 1;
}

int XhciController::wait_xfer(uint64_t trb_phys, uint32_t* out_len) {
    for (int t = 0; t < 20000000; t++) {
        trb_t* e = &evt_ring[evt_idx];
        uint32_t ctrl = e->control;
        if ((ctrl & 1) != evt_cycle) continue;

        uint8_t type = (ctrl >> 10) & 0x3F;
        if (type == XHCI_TRB_XFER_EVT) {
            uint8_t code = (e->status >> 24) & 0xFF;
            uint32_t len = e->status & 0xFFFFFF;
            uint64_t p = e->parameter;
            evt_advance();
            if (p == trb_phys) {
                if (out_len) *out_len = len;
                return static_cast<int>(code);
            }
        } else if (type == XHCI_TRB_PORTSC || type == XHCI_TRB_CMD_CMPL) {
            evt_advance();
        }
    }
    return -1;
}

int XhciController::control_xfer(uint8_t slot, uint8_t bmRequestType, uint8_t bRequest,
                                 uint16_t wValue, uint16_t wIndex, uint16_t wLength,
                                 void* data, int dir_in) {
    uint64_t setup_param =
        static_cast<uint64_t>(bmRequestType) |
        (static_cast<uint64_t>(bRequest) << 8) |
        (static_cast<uint64_t>(wValue)  << 16) |
        (static_cast<uint64_t>(wIndex)  << 32) |
        (static_cast<uint64_t>(wLength) << 48);

    int has_data = (wLength != 0);
    uint32_t trt = has_data ? (dir_in ? 2u : 3u) : 0u;

    int idx = ep0_idx;
    if (idx + 3 >= static_cast<int>(RING_TRBS)) return -1;

    ep0_ring[idx].parameter = setup_param;
    ep0_ring[idx].status    = 8;
    ep0_ring[idx].control   = (XHCI_TRB_SETUP << 10) | (trt << 16) | (1u << 6) | ep0_cycle;
    idx++;

    if (has_data) {
        ep0_ring[idx].parameter = reinterpret_cast<uint64_t>(data);
        ep0_ring[idx].status    = static_cast<uint32_t>(wLength);
        ep0_ring[idx].control   = (XHCI_TRB_DATA << 10) |
                                  (static_cast<uint32_t>(dir_in) << 16) | ep0_cycle;
        idx++;
    }

    ep0_ring[idx].parameter = 0;
    ep0_ring[idx].status    = 0;
    uint32_t status_dir = has_data ? (dir_in ? 0u : 1u) : 1u;
    ep0_ring[idx].control   = (XHCI_TRB_STATUS << 10) | (status_dir << 16) |
                              (1u << 5) | ep0_cycle;
    uint64_t status_phys = reinterpret_cast<uint64_t>(&ep0_ring[idx]);
    idx++;
    ep0_idx = idx;

    asm volatile("mfence" ::: "memory");
    db[slot] = 1;
    asm volatile("mfence" ::: "memory");

    return wait_xfer(status_phys, nullptr);
}

int XhciController::enable_slot() {
    uint8_t slot = 0;
    int rc = submit_cmd(XHCI_TRB_ENABLE_SLOT, 0, 0, 0, &slot);
    if (rc != 1) {
        printf("USB: Enable Slot failed (rc=%d)\n", rc);
        return -1;
    }
    return slot;
}

int XhciController::address_device(uint8_t slot, uint32_t port, uint32_t speed) {
    memset(dev_ctx[slot], 0, 1024);
    memset(input_ctx, 0, 1056);
    ep0_init();

    dcbaap[slot] = reinterpret_cast<uint64_t>(dev_ctx[slot]);

    uint32_t* icc = reinterpret_cast<uint32_t*>(input_ctx);
    icc[1] = (1u << 0) | (1u << 1);

    uint32_t* sc = reinterpret_cast<uint32_t*>(input_ctx + 32);
    sc[0] = (speed << 20) | (1u << 27);
    sc[1] = (port  << 16);

    uint64_t ep0_phys = reinterpret_cast<uint64_t>(ep0_ring);
    uint32_t* ec = reinterpret_cast<uint32_t*>(input_ctx + 64);
    ec[1] = (4u << 3) | (8u << 16);
    ec[2] = static_cast<uint32_t>(ep0_phys & 0xFFFFFFF0u) | 1u;
    ec[3] = static_cast<uint32_t>(ep0_phys >> 32);
    ec[4] = 8;

    int rc = submit_cmd(XHCI_TRB_ADDRESS_DEV,
                        reinterpret_cast<uint64_t>(input_ctx), 0, slot, nullptr);
    if (rc != 1) {
        printf("USB: Address Device failed (rc=%d)\n", rc);
        return -1;
    }
    return 0;
}

int XhciController::get_descriptor(uint8_t slot, uint8_t type, uint8_t idx, void* buf, uint16_t len) {
    return control_xfer(slot, 0x80, 0x06,
                        static_cast<uint16_t>((type << 8) | idx), 0, len, buf, 1);
}

int XhciController::set_configuration(uint8_t slot, uint8_t cfg) {
    return control_xfer(slot, 0x00, 0x09, cfg, 0, 0, nullptr, 0);
}

int XhciController::set_protocol_boot(uint8_t slot, uint8_t iface) {
    return control_xfer(slot, 0x21, 0x0B, 0, iface, 0, nullptr, 0);
}

int XhciController::set_idle(uint8_t slot, uint8_t iface) {
    return control_xfer(slot, 0x21, 0x0A, 0, iface, 0, nullptr, 0);
}

int XhciController::configure_endpoint(uint8_t slot, uint8_t ep_num, uint8_t ep_type,
                                       uint16_t mps, uint8_t interval) {
    memset(input_ctx, 0, 1056);
    uint8_t dci = static_cast<uint8_t>((ep_num * 2) + 1);

    uint32_t* icc = reinterpret_cast<uint32_t*>(input_ctx);
    icc[1] = (1u << 0) | (1u << dci);

    memcpy(input_ctx + 32, dev_ctx[slot], 32);
    uint32_t* sc_new = reinterpret_cast<uint32_t*>(input_ctx + 32);
    uint32_t cur = (sc_new[0] >> 27) & 0x1F;
    if (cur < dci) cur = dci;
    sc_new[0] = (sc_new[0] & ~(0x1Fu << 27)) | (cur << 27);

    uint64_t ring_phys = reinterpret_cast<uint64_t>(intr_ring);
    memset(intr_ring, 0, sizeof(intr_ring));
    intr_idx = 0;
    intr_cycle = 1;

    uint32_t* ec = reinterpret_cast<uint32_t*>(input_ctx + 32 + (dci * 32));
    ec[0] = (static_cast<uint32_t>(interval) << 16);
    ec[1] = (static_cast<uint32_t>(ep_type) << 3) | (static_cast<uint32_t>(mps) << 16);
    ec[2] = static_cast<uint32_t>(ring_phys & 0xFFFFFFF0u) | 1u;
    ec[3] = static_cast<uint32_t>(ring_phys >> 32);
    ec[4] = 8;

    int rc = submit_cmd(XHCI_TRB_CONFIG_EP,
                        reinterpret_cast<uint64_t>(input_ctx), 0, slot, nullptr);
    if (rc != 1) {
        printf("USB: Configure Endpoint failed (rc=%d)\n", rc);
        return -1;
    }
    return 0;
}

int XhciController::queue_interrupt_in_async(uint8_t slot) {
    if (intr_idx >= RING_TRBS - 1) {
        uint64_t ring_phys = reinterpret_cast<uint64_t>(intr_ring);
        intr_ring[RING_TRBS - 1].parameter = ring_phys;
        intr_ring[RING_TRBS - 1].control =
            (XHCI_TRB_LINK << 10) | (1u << 1) | intr_cycle;
        intr_idx = 0;
        intr_cycle ^= 1;
    }
    memset(intr_buf, 0, sizeof(intr_buf));
    intr_ring[intr_idx].parameter = reinterpret_cast<uint64_t>(intr_buf);
    intr_ring[intr_idx].status    = hid_ep_mps;
    intr_ring[intr_idx].control   = (XHCI_TRB_NORMAL << 10) | (1u << 5) | intr_cycle;

    asm volatile("mfence" ::: "memory");
    db[slot] = static_cast<uint32_t>(hid_ep_num * 2 + 1);
    asm volatile("mfence" ::: "memory");

    intr_idx++;
    return 0;
}

void XhciController::port_reset(uint32_t port, uint32_t* out_speed) {
    volatile uint32_t* portsc = reinterpret_cast<volatile uint32_t*>(ports + port * 0x10);
    mmio_wr32(portsc, XHCI_PORTSC_CSC | XHCI_PORTSC_PEC | XHCI_PORTSC_WRC |
                     XHCI_PORTSC_OCC | XHCI_PORTSC_PRC | XHCI_PORTSC_PLC |
                     XHCI_PORTSC_CEC);
    uint32_t val = mmio_rd32(portsc);
    if (!(val & XHCI_PORTSC_PP)) mmio_wr32(portsc, val | XHCI_PORTSC_PP);
    mmio_wr32(portsc, XHCI_PORTSC_PR);
    for (int i = 0; i < 10000000; i++)
        if (mmio_rd32(portsc) & XHCI_PORTSC_PRC) break;
    mmio_wr32(portsc, XHCI_PORTSC_PRC);
    if (out_speed) *out_speed = (mmio_rd32(portsc) >> 10) & 0xF;
}

int XhciController::find_hid_keyboard(const uint8_t* cfg, uint32_t total,
                                      uint8_t* out_iface, uint8_t* out_ep_addr,
                                      uint16_t* out_mps, uint8_t* out_interval) {
    uint32_t off = 0;
    uint8_t cur_iface = 0;
    bool in_hid_iface = false;

    while (off + 2 <= total) {
        uint8_t blen = cfg[off];
        uint8_t btype = cfg[off + 1];
        if (blen == 0) break;

        if (btype == 0x04 && off + 9 <= total) {
            cur_iface = cfg[off + 2];
            if (cfg[off + 5] == 0x03 && cfg[off + 6] == 0x01 && cfg[off + 7] == 0x01) {
                in_hid_iface = true;
                *out_iface = cur_iface;
            } else {
                in_hid_iface = false;
            }
        } else if (btype == 0x05 && off + 7 <= total && in_hid_iface) {
            if ((cfg[off + 3] & 0x03) == 0x03 && (cfg[off + 2] & 0x80)) {
                *out_ep_addr  = cfg[off + 2];
                *out_mps      = static_cast<uint16_t>(cfg[off + 4] | (cfg[off + 5] << 8));
                *out_interval = cfg[off + 6];
                return 0;
            }
        }
        off += blen;
    }
    return -1;
}

void XhciController::process_keys(const uint8_t* report) {
    for (int i = 0; i < 6; i++) {
        uint8_t k = report[2 + i];
        if (k == 0) continue;
        bool seen = false;
        for (int j = 0; j < 6; j++)
            if (prev_keys[j] == k) seen = true;
        if (seen) continue;

        g_usb_keys_count++;
        char c = hid_key_to_ascii(k);
        if (c) {
            input_push(c);
            g_usb_push_count++;
        }
    }
    memcpy(prev_keys, &report[2], 6);
}

int XhciController::setup_keyboard(uint32_t port, uint32_t speed) {
    int slot = enable_slot();
    if (slot < 0) return -1;
    if (address_device(static_cast<uint8_t>(slot), port, speed) != 0) return -1;
    if (get_descriptor(static_cast<uint8_t>(slot), 0x01, 0, ctrl_buf, 18) != 1) return -1;
    if (get_descriptor(static_cast<uint8_t>(slot), 0x02, 0, ctrl_buf, 256) != 1) return -1;

    uint32_t total = static_cast<uint32_t>(ctrl_buf[2] | (ctrl_buf[3] << 8));
    uint8_t iface = 0, ep_addr = 0, interval = 0;
    uint16_t mps = 0;

    if (find_hid_keyboard(ctrl_buf, total, &iface, &ep_addr, &mps, &interval) != 0) {
        printf("USB: no HID keyboard interface\n");
        return -1;
    }

    if (set_configuration(static_cast<uint8_t>(slot), ctrl_buf[5]) != 1) return -1;
    set_protocol_boot(static_cast<uint8_t>(slot), iface);
    set_idle(static_cast<uint8_t>(slot), iface);

    hid_ep_num    = ep_addr & 0x0F;
    hid_ep_mps    = mps;
    hid_interface = iface;

    if (configure_endpoint(static_cast<uint8_t>(slot), hid_ep_num, 7, mps, interval) != 0)
        return -1;

    memset(prev_keys, 0, 6);
    memset(intr_buf, 0, sizeof(intr_buf));
    return slot;
}

int XhciController::setup_msix() {
    uint8_t bus  = pci_bus;
    uint8_t slot = pci_slot;
    uint8_t func = pci_func;

    uint32_t cmd_status = pci_read32(bus, slot, func, 0x04);
    if (!((cmd_status >> 16) & 0x0010)) return -1;

    int cap = -1;
    uint32_t cap_ptr = pci_read32(bus, slot, func, 0x34) & 0xFC;
    int guard = 0;
    while (cap_ptr && guard++ < 64) {
        uint32_t cap_dw = pci_read32(bus, slot, func, static_cast<uint8_t>(cap_ptr));
        uint8_t cap_id = cap_dw & 0xFF;
        uint8_t next   = (cap_dw >> 8) & 0xFC;
        if (cap_id == 0x11) { cap = static_cast<int>(cap_ptr); break; }
        cap_ptr = next;
    }
    if (cap < 0) return -1;

    uint32_t cap_dword = pci_read32(bus, slot, func, static_cast<uint8_t>(cap));
    uint16_t msgctl    = static_cast<uint16_t>((cap_dword >> 16) & 0xFFFF);

    uint16_t table_size = (msgctl & 0x7FF) + 1;

    uint32_t table_off_bir = pci_read32(bus, slot, func, static_cast<uint8_t>(cap + 0x04));
    uint8_t  bir           = table_off_bir & 0x7;
    uint32_t table_offset  = table_off_bir & 0xFFFFFFF8u;

    uint32_t bar_low  = pci_read32(bus, slot, func, static_cast<uint8_t>(0x10 + 4 * bir));
    bool     is_64    = ((bar_low >> 1) & 0x3) == 0x2;
    uint32_t bar_high = 0;
    if (is_64) bar_high = pci_read32(bus, slot, func, static_cast<uint8_t>(0x10 + 4 * (bir + 1)));
    uint64_t bar_addr  = static_cast<uint64_t>(bar_low & 0xFFFFFFF0u) |
                         (static_cast<uint64_t>(bar_high) << 32);

    uint64_t table_addr = bar_addr + table_offset;

    if (table_size < 1) return -1;

    volatile uint32_t* entry = reinterpret_cast<volatile uint32_t*>(table_addr);

    entry[0] = 0xFEE00000u;
    entry[1] = 0;
    entry[2] = USB_IRQ_VECTOR;
    entry[3] = 0;

    msgctl &= 0x3FFFu;
    msgctl |= (1u << 15);

    uint32_t new_dword = (cap_dword & 0x0000FFFFu) | (static_cast<uint32_t>(msgctl) << 16);
    pci_write32(bus, slot, func, static_cast<uint8_t>(cap), new_dword);

    uint32_t verify_dword = pci_read32(bus, slot, func, static_cast<uint8_t>(cap));
    uint16_t verify       = static_cast<uint16_t>((verify_dword >> 16) & 0xFFFF);

    if (!(verify & (1u << 15))) return -1;

    return 0;
}

void XhciController::enable_interrupter() {
    volatile uint8_t* ir = rt + 0x20;

    uint32_t cmd = mmio_rd32(op + 0x00);
    cmd |= XHCI_USBCMD_RS | XHCI_USBCMD_INTE;
    mmio_wr32(op + 0x00, cmd);

    mmio_wr32(op + 0x04, XHCI_USBSTS_EINT);
    mmio_wr32(ir + 0x04, 0);

    mmio_wr32(ir + 0x00, 1u);
    mmio_wr32(ir + 0x00, 2u);

    uint32_t iman_after = mmio_rd32(ir + 0x00);
    if (!(iman_after & 2u)) {
        printf("USB: IMAN.IE not set (IMAN=%x)\n", iman_after);
    }
}

void XhciController::run(uintptr_t mmio_base, uint8_t bus, uint8_t slot, uint8_t func) {
    pci_bus = bus; pci_slot = slot; pci_func = func;

    parse_caps(mmio_base);
    reset();
    mmio_wr32(op + 0x38, max_slots);
    setup_dcbaap();
    setup_cmd_ring();
    setup_evt_ring();
    start();

    for (uint32_t p = 0; p < max_ports; p++) {
        volatile uint32_t* portsc = reinterpret_cast<volatile uint32_t*>(ports + p * 0x10);
        uint32_t val = mmio_rd32(portsc);
        if (!(val & XHCI_PORTSC_PP)) {
            mmio_wr32(portsc, val | XHCI_PORTSC_PP);
        }
    }

    for (volatile int i = 0; i < 10000000; i++) { }

    uint32_t speed = 0, found_port = 0;
    bool found = false;
    for (uint32_t p = 0; p < max_ports; p++) {
        volatile uint32_t* portsc = reinterpret_cast<volatile uint32_t*>(ports + p * 0x10);
        uint32_t val = mmio_rd32(portsc);
        mmio_wr32(portsc, XHCI_PORTSC_CSC | XHCI_PORTSC_PEC | XHCI_PORTSC_WRC |
                         XHCI_PORTSC_OCC | XHCI_PORTSC_PRC | XHCI_PORTSC_PLC |
                         XHCI_PORTSC_CEC | XHCI_PORTSC_PP);
        if (!(val & XHCI_PORTSC_CCS)) continue;
        port_reset(p, &speed);
        found_port = p + 1;
        found = true;
        break;
    }
    if (!found) {
        printf("USB: no device on any port\n");
        return;
    }

    int kbslot = setup_keyboard(found_port, speed);
    if (kbslot < 0) {
        printf("USB: keyboard setup failed\n");
        return;
    }
    usb_active_slot = kbslot;

    lapic_enable();

    if (setup_msix() == 0) {
        if (irq_register(USB_IRQ_VECTOR, usb_handler_c, "xHCI") != 0) {
            printf("USB: IRQ %d already in use\n", USB_IRQ_VECTOR);
            return;
        }
        enable_interrupter();
        g_usb_xfer_pending = 0;
        queue_interrupt_in_async(static_cast<uint8_t>(kbslot));
        g_usb_xfer_pending = 1;
        usb_irq_active = 1;
        printf("USB: keyboard ready (IRQ %d)\n", USB_IRQ_VECTOR);
        return;
    }

    printf("USB: MSI-X unavailable\n");
}

void XhciController::handle_interrupt() {
    g_usb_isr_count++;
    mmio_wr32(op + 0x04, XHCI_USBSTS_EINT);

    volatile uint8_t* ir = rt + 0x20;
    mmio_wr32(ir + 0x00, 1u);

    bool got_xfer = false;
    for (int budget = 0; budget < static_cast<int>(RING_TRBS); budget++) {
        trb_t* e = &evt_ring[evt_idx];
        if (((e->control) & 1) != evt_cycle) break;

        uint8_t type = (e->control >> 10) & 0x3F;
        if (type == XHCI_TRB_XFER_EVT) {
            uint8_t code = (e->status >> 24) & 0xFF;
            uint8_t epid = (e->control >> 16) & 0x1F;
            evt_advance();
            if (code != 1) g_usb_bad_code_count++;
            if (epid == (hid_ep_num * 2 + 1)) {
                process_keys(intr_buf);
                got_xfer = true;
            }
        } else if (type == XHCI_TRB_PORTSC || type == XHCI_TRB_CMD_CMPL) {
            evt_advance();
        } else {
            break;
        }
    }

    g_usb_xfer_pending = 0;
    if (got_xfer && usb_active_slot > 0) {
        queue_interrupt_in_async(static_cast<uint8_t>(usb_active_slot));
        g_usb_xfer_pending = 1;
    }
    mmio_wr32(ir + 0x00, 2u);
}

static uint32_t pci_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t addr = (1u << 31) | (static_cast<uint32_t>(bus) << 16) |
                    (static_cast<uint32_t>(slot) << 11) |
                    (static_cast<uint32_t>(func) << 8) | (offset & 0xFC);
    outl(0xCF8, addr);
    return inl(0xCFC);
}

static void pci_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val) {
    uint32_t addr = (1u << 31) | (static_cast<uint32_t>(bus) << 16) |
                    (static_cast<uint32_t>(slot) << 11) |
                    (static_cast<uint32_t>(func) << 8) | (offset & 0xFC);
    outl(0xCF8, addr);
    outl(0xCFC, val);
}

static uint64_t pci_read_bar64(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, int* is_io) {
    uint32_t bar0 = pci_read32(bus, slot, func, offset);
    *is_io = bar0 & 1;
    if (*is_io) return bar0 & 0xFFFFFFFCu;
    uint64_t low = bar0 & 0xFFFFFFF0u;
    if (((bar0 >> 1) & 0x3) == 0x2)
        return (static_cast<uint64_t>(pci_read32(bus, slot, func, offset + 4)) << 32) | low;
    return low;
}

extern "C" {
void usb_handler_c(struct regs* r) {
    (void)r;
    static_xhci.handle_interrupt();
}

uint64_t usb_get_isr_count()      { return g_usb_isr_count; }
uint64_t usb_get_keys_count()     { return g_usb_keys_count; }
uint64_t usb_get_push_count()     { return g_usb_push_count; }
uint64_t usb_get_bad_code_count() { return g_usb_bad_code_count; }

void usb_init(void) {
    Pci::PciDevice dev;
    
    // Ищем Xhci, если не найден остановка
    if (!Pci::PciScanner::findPciDevice(Pci::DeviceType::Xhci, dev)) {
        printf("USB: no controller found\n");
        return;
    }

    // активируем pci устройство
    uint32_t cmd = pci_read32(dev.bus, dev.slot, dev.func, 0x04);
    pci_write32(dev.bus, dev.slot, dev.func, 0x04, cmd | 0x0007);

    int is_io = 0;
    uint64_t bar = pci_read_bar64(dev.bus, dev.slot, dev.func, 0x10, &is_io);

    if (bar == 0 || is_io) {
        printf("USB: BAR is zero or IO space, cannot init xHCI\n");
        return;
    }

    printf("USB xHCI found: Vendor:\n    %s\n    Device: %s\n", pci_get_vendor_name(dev.vendor_id), pci_get_device_name(dev.vendor_id, dev.device_id));

    // округление вниз до ближайшего блока 4096 байт
    uintptr_t phys_page_start = (uintptr_t)bar & ~0xFFFULL;

    // выделение страниц памяти
    for (size_t i = 0; i < 256; i++) {
        uintptr_t addr = phys_page_start + (i * 4096);
        vmm_map_page(addr, addr, PAGE_PRESENT | PAGE_WRITABLE | PAGE_CACHE_DISABLE);
    }

    // расчет виртуального адреса
    uintptr_t xhci_virtual_address = phys_page_start + ((uintptr_t)bar & 0xFFF);

    // Запуск драйвера
    static_xhci.run(xhci_virtual_address, dev.bus, dev.slot, dev.func);
}
}

