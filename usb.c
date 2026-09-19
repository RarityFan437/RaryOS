#include <stdint.h>
#include <stddef.h>
#include "stdio.h"
#include "idt.h"
#include "irq.h"
#include "input.h"

#define XHCI_MAX_SLOTS       64
#define XHCI_MAX_PORTS       32
#define XHCI_MAX_SCRATCHPAD  64
#define XHCI_RING_TRBS       256

#define XHCI_USBCMD_RS      (1u << 0)
#define XHCI_USBCMD_HCRST   (1u << 1)

#define XHCI_USBSTS_HCH     (1u << 0)
#define XHCI_USBSTS_HSE     (1u << 2)
#define XHCI_USBSTS_EINT    (1u << 3)
#define XHCI_USBSTS_PCD     (1u << 4)
#define XHCI_USBSTS_CNR     (1u << 11)
#define XHCI_USBSTS_HCE     (1u << 12)

#define XHCI_PORTSC_CCS     (1u << 0)
#define XHCI_PORTSC_PED     (1u << 1)
#define XHCI_PORTSC_PR      (1u << 4)
#define XHCI_PORTSC_PP      (1u << 9)
#define XHCI_PORTSC_CSC     (1u << 17)
#define XHCI_PORTSC_PEC     (1u << 18)
#define XHCI_PORTSC_WRC     (1u << 19)
#define XHCI_PORTSC_OCC     (1u << 20)
#define XHCI_PORTSC_PRC     (1u << 21)
#define XHCI_PORTSC_PLC     (1u << 22)
#define XHCI_PORTSC_CEC     (1u << 23)

#define XHCI_TRB_NORMAL       1
#define XHCI_TRB_SETUP        2
#define XHCI_TRB_DATA         3
#define XHCI_TRB_STATUS       4
#define XHCI_TRB_LINK         6
#define XHCI_TRB_ENABLE_SLOT  9
#define XHCI_TRB_ADDRESS_DEV  11
#define XHCI_TRB_CONFIG_EP    12
#define XHCI_TRB_NOOP_CMD     23
#define XHCI_TRB_XFER_EVT     32
#define XHCI_TRB_CMD_CMPL     33
#define XHCI_TRB_PORTSC       34

#define USB_IRQ_VECTOR        64

#define XHCI_USBCMD_INTE    (1u << 2)

typedef struct {
    uint64_t parameter;
    uint32_t status;
    uint32_t control;
} __attribute__((packed)) trb_t;

typedef struct {
    uint64_t rsba;
    uint32_t rssz;
    uint32_t reserved;
} __attribute__((packed)) erst_entry_t;

typedef struct {
    volatile uint8_t*  mmio;
    volatile uint8_t*  op;
    volatile uint32_t* db;
    volatile uint8_t*  rt;
    volatile uint8_t*  ports;

    uint32_t caplength;
    uint32_t max_slots;
    uint32_t max_ports;
    uint32_t max_intrs;
    uint32_t num_scratchpad;
    uint32_t pagesize;

    uint64_t dcbaap_phys;
    uint64_t cmd_ring_phys;
    uint64_t evt_ring_phys;
    uint64_t erst_phys;
    uint64_t bar_phys;

    uint8_t  pci_bus;
    uint8_t  pci_slot;
    uint8_t  pci_func;

    uint32_t cmd_idx;
    uint8_t  cmd_cycle;
    uint32_t evt_idx;
    uint8_t  evt_cycle;
} xhci_t;

static xhci_t xhci_inst;

__attribute__((aligned(64)))   static uint64_t dcbaap[XHCI_MAX_SLOTS + 1];
__attribute__((aligned(64)))   static trb_t    cmd_ring[XHCI_RING_TRBS];
__attribute__((aligned(64)))   static trb_t    evt_ring[XHCI_RING_TRBS];
__attribute__((aligned(64)))   static erst_entry_t erst[1];
__attribute__((aligned(64)))   static uint64_t scratchpad_array[XHCI_MAX_SCRATCHPAD];
__attribute__((aligned(4096))) static uint8_t  scratchpad_data[XHCI_MAX_SCRATCHPAD][4096];

__attribute__((aligned(64)))   static uint8_t  dev_ctx[XHCI_MAX_SLOTS + 1][1024];
__attribute__((aligned(64)))   static uint8_t  input_ctx[1056];
__attribute__((aligned(64)))   static trb_t    ep0_ring[XHCI_RING_TRBS];
__attribute__((aligned(64)))   static trb_t    intr_ring[XHCI_RING_TRBS];
__attribute__((aligned(64)))   static uint8_t  ctrl_buf[512];
__attribute__((aligned(64)))   static uint8_t  intr_buf[64];

static uint32_t ep0_idx;
static uint8_t  ep0_cycle;
static uint32_t intr_idx;
static uint8_t  intr_cycle;

static uint8_t  hid_ep_num;
static uint16_t hid_ep_mps;
static uint8_t  hid_interface;

int usb_irq_active = 0;
static int usb_active_slot = 0;
static volatile int usb_xfer_pending = 0;

static volatile uint64_t usb_isr_count = 0;
static volatile uint64_t usb_keys_count = 0;
static volatile uint64_t usb_push_count = 0;
static volatile uint64_t usb_bad_code_count = 0;

uint64_t usb_get_isr_count(void)  { return usb_isr_count; }
uint64_t usb_get_keys_count(void) { return usb_keys_count; }
uint64_t usb_get_push_count(void) { return usb_push_count; }
uint64_t usb_get_bad_code_count(void) { return usb_bad_code_count; }

static inline void mmio_wr32(volatile void* p, uint32_t v) {
    *(volatile uint32_t*)p = v;
    asm volatile("mfence" ::: "memory");
}

static inline uint32_t mmio_rd32(volatile void* p) {
    uint32_t v = *(volatile uint32_t*)p;
    asm volatile("mfence" ::: "memory");
    return v;
}

static void memzero(void* p, uint32_t n) {
    uint8_t* b = (uint8_t*)p;
    for (uint32_t i = 0; i < n; i++) b[i] = 0;
}

static void memcopy(void* dst, const void* src, uint32_t n) {
    uint8_t* d = (uint8_t*)dst;
    const uint8_t* s = (const uint8_t*)src;
    for (uint32_t i = 0; i < n; i++) d[i] = s[i];
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

static void xhci_parse_caps(xhci_t* x, uintptr_t mmio_base) {
    x->mmio = (volatile uint8_t*)mmio_base;

    uint32_t cap0 = mmio_rd32(x->mmio + 0x00);
    x->caplength = cap0 & 0xFF;

    uint32_t cap1 = mmio_rd32(x->mmio + 0x04);
    x->max_slots = cap1 & 0xFF;
    x->max_intrs = (cap1 >> 8) & 0x7FF;
    x->max_ports = (cap1 >> 24) & 0xFF;

    uint32_t cap2 = mmio_rd32(x->mmio + 0x08);
    uint32_t spb_lo = cap2 & 0xFF;
    uint32_t spb_hi = (cap2 >> 21) & 0x1F;
    x->num_scratchpad = (spb_hi << 5) | spb_lo;

    uint32_t dboff  = mmio_rd32(x->mmio + 0x14);
    uint32_t rtsoff = mmio_rd32(x->mmio + 0x18);

    x->op    = (volatile uint8_t*)((uint8_t*)x->mmio + x->caplength);
    x->db    = (volatile uint32_t*)((uint8_t*)x->mmio + (dboff & ~0x3u));
    x->rt    = (volatile uint8_t*)((uint8_t*)x->mmio + (rtsoff & ~0x1Fu));
    x->ports = (volatile uint8_t*)((uint8_t*)x->op + 0x400);
}

static void xhci_halt(xhci_t* x) {
    mmio_wr32(x->op + 0x00, 0);
    for (int i = 0; i < 1000000; i++) {
        if (mmio_rd32(x->op + 0x04) & XHCI_USBSTS_HCH) break;
    }
}

static void xhci_reset(xhci_t* x) {
    for (int i = 0; i < 1000000; i++) {
        if (!(mmio_rd32(x->op + 0x04) & XHCI_USBSTS_CNR)) break;
    }

    xhci_halt(x);

    mmio_wr32(x->op + 0x00, XHCI_USBCMD_HCRST);

    for (int i = 0; i < 10000000; i++) {
        if (!(mmio_rd32(x->op + 0x00) & XHCI_USBCMD_HCRST)) break;
    }

    for (int i = 0; i < 10000000; i++) {
        if (!(mmio_rd32(x->op + 0x04) & XHCI_USBSTS_CNR)) break;
    }

    xhci_halt(x);

    uint32_t cap1 = mmio_rd32(x->mmio + 0x04);
    x->max_slots = cap1 & 0xFF;
    x->max_ports = (cap1 >> 24) & 0xFF;

    uint32_t cap2 = mmio_rd32(x->mmio + 0x08);
    uint32_t spb_lo = cap2 & 0xFF;
    uint32_t spb_hi = (cap2 >> 21) & 0x1F;
    x->num_scratchpad = (spb_hi << 5) | spb_lo;

    x->pagesize = mmio_rd32(x->op + 0x08);
}

static void xhci_setup_dcbaap(xhci_t* x) {
    for (uint32_t i = 0; i <= x->max_slots; i++) dcbaap[i] = 0;

    uint32_t sp = x->num_scratchpad;
    if (sp > XHCI_MAX_SCRATCHPAD) sp = XHCI_MAX_SCRATCHPAD;

    if (sp > 0) {
        for (uint32_t i = 0; i < sp; i++) {
            memzero(scratchpad_data[i], 4096);
            scratchpad_array[i] = (uint64_t)(uintptr_t)scratchpad_data[i];
        }
        dcbaap[0] = (uint64_t)(uintptr_t)scratchpad_array;
    }

    x->dcbaap_phys = (uint64_t)(uintptr_t)dcbaap;
    mmio_wr32(x->op + 0x30, (uint32_t)x->dcbaap_phys);
    mmio_wr32(x->op + 0x34, (uint32_t)(x->dcbaap_phys >> 32));
}

static void xhci_setup_cmd_ring(xhci_t* x) {
    memzero(cmd_ring, sizeof(cmd_ring));

    x->cmd_ring_phys = (uint64_t)(uintptr_t)cmd_ring;
    x->cmd_idx = 0;
    x->cmd_cycle = 1;

    cmd_ring[XHCI_RING_TRBS - 1].parameter = x->cmd_ring_phys;
    cmd_ring[XHCI_RING_TRBS - 1].control =
        ((uint32_t)XHCI_TRB_LINK << 10) | (1u << 1) | 1u;

    mmio_wr32(x->op + 0x18, (uint32_t)x->cmd_ring_phys | 1u);
    mmio_wr32(x->op + 0x1C, (uint32_t)(x->cmd_ring_phys >> 32));
}

static void xhci_setup_evt_ring(xhci_t* x) {
    memzero(evt_ring, sizeof(evt_ring));

    x->evt_ring_phys = (uint64_t)(uintptr_t)evt_ring;
    x->evt_idx = 0;
    x->evt_cycle = 1;

    erst[0].rsba = x->evt_ring_phys;
    erst[0].rssz = XHCI_RING_TRBS;
    erst[0].reserved = 0;
    x->erst_phys = (uint64_t)(uintptr_t)erst;

    volatile uint8_t* ir = x->rt + 0x20;

    mmio_wr32(ir + 0x08, 1);
    mmio_wr32(ir + 0x18, (uint32_t)x->evt_ring_phys);
    mmio_wr32(ir + 0x1C, (uint32_t)(x->evt_ring_phys >> 32));
    mmio_wr32(ir + 0x10, (uint32_t)x->erst_phys);
    mmio_wr32(ir + 0x14, (uint32_t)(x->erst_phys >> 32));
}

static void xhci_start(xhci_t* x) {
    mmio_wr32(x->op + 0x00, XHCI_USBCMD_RS);
    for (int i = 0; i < 1000000; i++) {
        if (!(mmio_rd32(x->op + 0x04) & XHCI_USBSTS_HCH)) break;
    }
}

static void xhci_evt_advance(xhci_t* x) {
    x->evt_idx++;
    if (x->evt_idx == XHCI_RING_TRBS) {
        x->evt_idx = 0;
        x->evt_cycle ^= 1;
    }

    volatile uint8_t* ir = x->rt + 0x20;
    uint64_t erdp = x->evt_ring_phys + (uint64_t)x->evt_idx * 16;
    mmio_wr32(ir + 0x18, (uint32_t)erdp | (1u << 3));
    mmio_wr32(ir + 0x1C, (uint32_t)(erdp >> 32));
}

static int xhci_wait_cmd(xhci_t* x, uint64_t cmd_phys, uint8_t* out_slot) {
    for (int t = 0; t < 20000000; t++) {
        trb_t* e = &evt_ring[x->evt_idx];
        uint32_t ctrl = e->control;
        uint8_t cyc = ctrl & 1;
        uint8_t type = (ctrl >> 10) & 0x3F;

        if (cyc != x->evt_cycle) continue;

        if (type == XHCI_TRB_CMD_CMPL) {
            uint8_t code = (e->status >> 24) & 0xFF;
            uint8_t slot = (ctrl >> 24) & 0xFF;
            uint64_t p = e->parameter;
            xhci_evt_advance(x);
            if (p == cmd_phys) {
                if (out_slot) *out_slot = slot;
                return (int)code;
            }
        } else if (type == XHCI_TRB_PORTSC) {
            xhci_evt_advance(x);
        } else if (type == XHCI_TRB_XFER_EVT) {
            xhci_evt_advance(x);
        }
    }
    return -1;
}

static int xhci_submit_cmd(xhci_t* x, uint32_t type, uint64_t param, uint32_t status,
                           uint8_t slot_id, uint8_t* out_slot)
{
    if (x->cmd_idx == XHCI_RING_TRBS - 1) {
        cmd_ring[XHCI_RING_TRBS - 1].control =
            ((uint32_t)XHCI_TRB_LINK << 10) | (1u << 1) | (uint32_t)x->cmd_cycle;
        x->cmd_idx = 0;
        x->cmd_cycle ^= 1;
    }

    uint64_t phys = x->cmd_ring_phys + (uint64_t)x->cmd_idx * 16;

    cmd_ring[x->cmd_idx].parameter = param;
    cmd_ring[x->cmd_idx].status = status;
    cmd_ring[x->cmd_idx].control =
        (type << 10) | ((uint32_t)slot_id << 24) | (uint32_t)x->cmd_cycle;

    asm volatile("mfence" ::: "memory");
    *(volatile uint32_t*)(x->db) = 0;
    asm volatile("mfence" ::: "memory");

    x->cmd_idx++;

    return xhci_wait_cmd(x, phys, out_slot);
}

static void xhci_ep0_init(xhci_t* x) {
    memzero(ep0_ring, sizeof(ep0_ring));
    ep0_idx = 0;
    ep0_cycle = 1;
    (void)x;
}

static int xhci_wait_xfer(xhci_t* x, uint64_t trb_phys, uint32_t* out_len) {
    for (int t = 0; t < 20000000; t++) {
        trb_t* e = &evt_ring[x->evt_idx];
        uint32_t ctrl = e->control;
        uint8_t cyc = ctrl & 1;
        uint8_t type = (ctrl >> 10) & 0x3F;

        if (cyc != x->evt_cycle) continue;

        if (type == XHCI_TRB_XFER_EVT) {
            uint8_t code = (e->status >> 24) & 0xFF;
            uint32_t len = e->status & 0xFFFFFF;
            uint64_t p = e->parameter;
            xhci_evt_advance(x);
            if (p == trb_phys) {
                if (out_len) *out_len = len;
                return (int)code;
            }
        } else if (type == XHCI_TRB_PORTSC) {
            xhci_evt_advance(x);
        } else if (type == XHCI_TRB_CMD_CMPL) {
            xhci_evt_advance(x);
        }
    }
    return -1;
}

static int xhci_control_xfer(xhci_t* x, uint8_t slot,
                             uint8_t bmRequestType, uint8_t bRequest,
                             uint16_t wValue, uint16_t wIndex, uint16_t wLength,
                             void* data, int dir_in)
{
    uint64_t setup_param =
        (uint64_t)bmRequestType |
        ((uint64_t)bRequest << 8) |
        ((uint64_t)wValue << 16) |
        ((uint64_t)wIndex << 32) |
        ((uint64_t)wLength << 48);

    int has_data = (wLength != 0);
    uint32_t trt = has_data ? (dir_in ? 2u : 3u) : 0u;

    int idx = ep0_idx;
    trb_t* ring = ep0_ring;
    uint8_t cyc = ep0_cycle;

    if (idx + 3 >= XHCI_RING_TRBS) return -1;

    ring[idx].parameter = setup_param;
    ring[idx].status = 8;
    ring[idx].control = (XHCI_TRB_SETUP << 10) | (trt << 16) | (1u << 6) | cyc;
    idx++;

    if (has_data) {
        ring[idx].parameter = (uint64_t)(uintptr_t)data;
        ring[idx].status = (uint32_t)wLength;
        ring[idx].control = (XHCI_TRB_DATA << 10) | ((uint32_t)dir_in << 16) | cyc;
        idx++;
    }

    ring[idx].parameter = 0;
    ring[idx].status = 0;
    uint32_t status_dir = has_data ? (uint32_t)(dir_in ? 0 : 1) : 1u;
    ring[idx].control = (XHCI_TRB_STATUS << 10) | (status_dir << 16) | (1u << 5) | cyc;
    uint64_t status_phys = (uint64_t)(uintptr_t)&ring[idx];
    idx++;

    ep0_idx = idx;

    asm volatile("mfence" ::: "memory");
    x->db[slot] = 1;
    asm volatile("mfence" ::: "memory");

    return xhci_wait_xfer(x, status_phys, NULL);
}

static int xhci_enable_slot(xhci_t* x) {
    uint8_t slot = 0;
    int rc = xhci_submit_cmd(x, XHCI_TRB_ENABLE_SLOT, 0, 0, 0, &slot);
    if (rc != 1) {
        printf("USB: Enable Slot failed (rc=%d)\n", rc);
        return -1;
    }
    return (int)slot;
}

static int xhci_address_device(xhci_t* x, uint8_t slot, uint32_t port, uint32_t speed) {
    memzero(dev_ctx[slot], 1024);
    memzero(input_ctx, 1056);
    xhci_ep0_init(x);

    uint64_t dev_ctx_phys = (uint64_t)(uintptr_t)dev_ctx[slot];
    dcbaap[slot] = dev_ctx_phys;

    uint32_t* icc = (uint32_t*)input_ctx;
    icc[1] = (1u << 0) | (1u << 1);

    uint8_t* in_slot = input_ctx + 32;
    uint8_t* in_ep0  = input_ctx + 64;

    uint32_t* sc = (uint32_t*)in_slot;
    sc[0] = (speed << 20) | (1u << 27);
    sc[1] = (port << 16);

    uint64_t ep0_phys = (uint64_t)(uintptr_t)ep0_ring;
    uint32_t* ec = (uint32_t*)in_ep0;
    ec[1] = (4u << 3) | (8u << 16);
    ec[2] = (uint32_t)(ep0_phys & 0xFFFFFFF0u) | 1u;
    ec[3] = (uint32_t)(ep0_phys >> 32);
    ec[4] = 8;

    uint64_t in_phys = (uint64_t)(uintptr_t)input_ctx;

    int rc = xhci_submit_cmd(x, XHCI_TRB_ADDRESS_DEV, in_phys, 0, slot, NULL);
    if (rc != 1) {
        printf("USB: Address Device failed (rc=%d)\n", rc);
        return -1;
    }
    return 0;
}

static int xhci_get_descriptor(xhci_t* x, uint8_t slot, uint8_t type, uint8_t idx,
                               void* buf, uint16_t len)
{
    return xhci_control_xfer(x, slot,
        0x80, 0x06,
        (uint16_t)((type << 8) | idx),
        0, len, buf, 1);
}

static int xhci_set_configuration(xhci_t* x, uint8_t slot, uint8_t cfg) {
    return xhci_control_xfer(x, slot, 0x00, 0x09, cfg, 0, 0, NULL, 0);
}

static int xhci_set_protocol_boot(xhci_t* x, uint8_t slot, uint8_t iface) {
    return xhci_control_xfer(x, slot, 0x21, 0x0B, 0, iface, 0, NULL, 0);
}

static int xhci_set_idle(xhci_t* x, uint8_t slot, uint8_t iface) {
    return xhci_control_xfer(x, slot, 0x21, 0x0A, 0, iface, 0, NULL, 0);
}

static int xhci_configure_endpoint(xhci_t* x, uint8_t slot,
                                   uint8_t ep_num, uint8_t ep_type,
                                   uint16_t mps, uint8_t interval)
{
    memzero(input_ctx, 1056);

    uint8_t dci = (uint8_t)((ep_num * 2) + 1);

    uint32_t* icc = (uint32_t*)input_ctx;
    icc[1] = (1u << 0) | (1u << dci);

    uint8_t* in_slot = input_ctx + 32;
    memcopy(in_slot, dev_ctx[slot], 32);

    uint32_t* sc_new = (uint32_t*)in_slot;
    uint32_t cur = (sc_new[0] >> 27) & 0x1F;
    if (cur < dci) cur = dci;
    sc_new[0] = (sc_new[0] & ~(0x1Fu << 27)) | (cur << 27);

    uint8_t* in_ep = input_ctx + 32 + ((uint32_t)dci * 32);

    uint64_t ring_phys = (uint64_t)(uintptr_t)intr_ring;
    memzero(intr_ring, sizeof(intr_ring));
    intr_idx = 0;
    intr_cycle = 1;

    uint32_t* ec = (uint32_t*)in_ep;
    ec[0] = ((uint32_t)interval << 16);
    ec[1] = ((uint32_t)ep_type << 3) | ((uint32_t)mps << 16);
    ec[2] = (uint32_t)(ring_phys & 0xFFFFFFF0u) | 1u;
    ec[3] = (uint32_t)(ring_phys >> 32);
    ec[4] = 8;

    uint64_t in_phys = (uint64_t)(uintptr_t)input_ctx;

    int rc = xhci_submit_cmd(x, XHCI_TRB_CONFIG_EP, in_phys, 0, slot, NULL);
    if (rc != 1) {
        printf("USB: Configure Endpoint failed (rc=%d)\n", rc);
        return -1;
    }
    return 0;
}

static int xhci_queue_interrupt_in_async(xhci_t* x, uint8_t slot) {
    if (intr_idx >= XHCI_RING_TRBS - 1) {
        uint64_t ring_phys = (uint64_t)(uintptr_t)intr_ring;
        intr_ring[XHCI_RING_TRBS - 1].parameter = ring_phys;
        intr_ring[XHCI_RING_TRBS - 1].status = 0;
        intr_ring[XHCI_RING_TRBS - 1].control =
            ((uint32_t)XHCI_TRB_LINK << 10) | (1u << 1) | (uint32_t)intr_cycle;
        intr_idx = 0;
        intr_cycle ^= 1;
    }

    memzero(intr_buf, sizeof(intr_buf));

    intr_ring[intr_idx].parameter = (uint64_t)(uintptr_t)intr_buf;
    intr_ring[intr_idx].status = hid_ep_mps;
    intr_ring[intr_idx].control = (XHCI_TRB_NORMAL << 10) | (1u << 5) | intr_cycle;

    asm volatile("mfence" ::: "memory");
    x->db[slot] = (uint32_t)(hid_ep_num * 2 + 1);
    asm volatile("mfence" ::: "memory");

    intr_idx++;
    return 0;
}

static void xhci_port_reset(xhci_t* x, uint32_t port, uint32_t* out_speed) {
    volatile uint32_t* portsc = (volatile uint32_t*)(x->ports + port * 0x10);

    mmio_wr32(portsc, XHCI_PORTSC_CSC | XHCI_PORTSC_PEC | XHCI_PORTSC_WRC
                   | XHCI_PORTSC_OCC | XHCI_PORTSC_PRC | XHCI_PORTSC_PLC
                   | XHCI_PORTSC_CEC);

    uint32_t val = mmio_rd32(portsc);
    if (!(val & XHCI_PORTSC_PP)) mmio_wr32(portsc, val | XHCI_PORTSC_PP);

    mmio_wr32(portsc, XHCI_PORTSC_PR);

    for (int i = 0; i < 10000000; i++) {
        val = mmio_rd32(portsc);
        if (val & XHCI_PORTSC_PRC) break;
    }

    mmio_wr32(portsc, XHCI_PORTSC_PRC);
    val = mmio_rd32(portsc);

    if (out_speed) *out_speed = (val >> 10) & 0xF;
}

static int xhci_find_hid_keyboard(const uint8_t* cfg, uint32_t total,
                                  uint8_t* out_iface, uint8_t* out_ep_addr,
                                  uint16_t* out_mps, uint8_t* out_interval)
{
    uint32_t off = 0;
    uint8_t cur_iface = 0;
    int in_hid_iface = 0;

    while (off + 2 <= total) {
        uint8_t blen = cfg[off];
        uint8_t btype = cfg[off + 1];
        if (blen == 0) break;

        if (btype == 0x04 && off + 9 <= total) {
            cur_iface = cfg[off + 2];
            uint8_t cls = cfg[off + 5];
            uint8_t sub = cfg[off + 6];
            uint8_t pro = cfg[off + 7];
            if (cls == 0x03 && sub == 0x01 && pro == 0x01) {
                in_hid_iface = 1;
                *out_iface = cur_iface;
            } else {
                in_hid_iface = 0;
            }
        } else if (btype == 0x05 && off + 7 <= total && in_hid_iface) {
            uint8_t ep_addr = cfg[off + 2];
            uint8_t attr = cfg[off + 3];
            uint16_t mps = (uint16_t)(cfg[off + 4] | (cfg[off + 5] << 8));
            uint8_t interval = cfg[off + 6];
            if ((attr & 0x03) == 0x03 && (ep_addr & 0x80)) {
                *out_ep_addr = ep_addr;
                *out_mps = mps;
                *out_interval = interval;
                return 0;
            }
        }

        off += blen;
    }
    return -1;
}

static const char hid_map[256] = {
    [0x04]='a',[0x05]='b',[0x06]='c',[0x07]='d',[0x08]='e',[0x09]='f',[0x0A]='g',
    [0x0B]='h',[0x0C]='i',[0x0D]='j',[0x0E]='k',[0x0F]='l',[0x10]='m',[0x11]='n',
    [0x12]='o',[0x13]='p',[0x14]='q',[0x15]='r',[0x16]='s',[0x17]='t',[0x18]='u',
    [0x19]='v',[0x1A]='w',[0x1B]='x',[0x1C]='y',[0x1D]='z',
    [0x1E]='1',[0x1F]='2',[0x20]='3',[0x21]='4',[0x22]='5',[0x23]='6',[0x24]='7',
    [0x25]='8',[0x26]='9',[0x27]='0',
    [0x28]='\n',[0x29]=27,[0x2A]='\b',[0x2B]='\t',[0x2C]=' ',
    [0x2D]='-',[0x2E]='=',[0x2F]='[',[0x30]=']',[0x31]='\\',
    [0x33]=';',[0x34]='\'',[0x35]='`',[0x36]=',',[0x37]='.',[0x38]='/'
};

static uint8_t prev_keys[6];

static void xhci_process_keys(const uint8_t* report) {
    for (int i = 0; i < 6; i++) {
        uint8_t k = report[2 + i];
        if (k == 0) continue;
        int seen = 0;
        for (int j = 0; j < 6; j++) if (prev_keys[j] == k) seen = 1;
        if (seen) continue;
        usb_keys_count++;
        char c = hid_map[k];
        if (c) {
            input_push(c);
            usb_push_count++;
        }
    }
    memcopy(prev_keys, &report[2], 6);
}

static inline void outl(uint16_t port, uint32_t val) {
    asm volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint32_t inl(uint16_t port) {
    uint32_t ret;
    asm volatile("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

static uint32_t pci_read32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t addr = (1u << 31) | ((uint32_t)bus << 16) | ((uint32_t)slot << 11)
                  | ((uint32_t)func << 8) | (offset & 0xFC);
    outl(0xCF8, addr);
    return inl(0xCFC);
}

static void pci_write32(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val) {
    uint32_t addr = (1u << 31) | ((uint32_t)bus << 16) | ((uint32_t)slot << 11)
                  | ((uint32_t)func << 8) | (offset & 0xFC);
    outl(0xCF8, addr);
    outl(0xCFC, val);
}

static uint64_t pci_read_bar64(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, int* is_io) {
    uint32_t bar0 = pci_read32(bus, slot, func, offset);
    *is_io = bar0 & 1;
    if (*is_io) return (uint64_t)(bar0 & 0xFFFFFFFCu);
    uint32_t type = (bar0 >> 1) & 0x3;
    uint64_t low = (uint64_t)(bar0 & 0xFFFFFFF0u);
    if (type == 0x2) {
        uint32_t bar1 = pci_read32(bus, slot, func, offset + 4);
        return ((uint64_t)bar1 << 32) | low;
    }
    return low;
}

static int pci_find_msix_cap(uint8_t bus, uint8_t slot, uint8_t func) {
    uint32_t cmd_status = pci_read32(bus, slot, func, 0x04);
    if (!((cmd_status >> 16) & 0x0010)) return -1;

    uint32_t cap_ptr = pci_read32(bus, slot, func, 0x34) & 0xFC;
    int guard = 0;
    while (cap_ptr && guard++ < 64) {
        uint32_t cap = pci_read32(bus, slot, func, cap_ptr);
        uint8_t cap_id = cap & 0xFF;
        uint8_t next = (cap >> 8) & 0xFC;
        if (cap_id == 0x11) return (int)cap_ptr;
        cap_ptr = next;
    }
    return -1;
}

static int xhci_setup_msix(xhci_t* x) {
    uint8_t bus = x->pci_bus;
    uint8_t slot = x->pci_slot;
    uint8_t func = x->pci_func;

    int cap = pci_find_msix_cap(bus, slot, func);
    if (cap < 0) return -1;

    uint32_t cap_dword = pci_read32(bus, slot, func, (uint8_t)cap);
    uint16_t msgctl = (uint16_t)((cap_dword >> 16) & 0xFFFF);

    uint16_t table_size = (msgctl & 0x7FF) + 1;

    uint32_t table_off_bir = pci_read32(bus, slot, func, (uint8_t)(cap + 0x04));
    uint8_t  bir = table_off_bir & 0x7;
    uint32_t table_offset = table_off_bir & 0xFFFFFFF8u;

    uint32_t bar_low = pci_read32(bus, slot, func, (uint8_t)(0x10 + 4 * bir));
    int is_64 = ((bar_low >> 1) & 0x3) == 0x2;
    uint32_t bar_high = 0;
    if (is_64) bar_high = pci_read32(bus, slot, func, (uint8_t)(0x10 + 4 * (bir + 1)));
    uint64_t bar_addr = (uint64_t)(bar_low & 0xFFFFFFF0u) | ((uint64_t)bar_high << 32);

    uint64_t table_addr = bar_addr + table_offset;

    if (table_size < 1) return -1;

    volatile uint32_t* entry = (volatile uint32_t*)(uintptr_t)table_addr;

    entry[0] = 0xFEE00000u;
    entry[1] = 0;
    entry[2] = USB_IRQ_VECTOR;
    entry[3] = 0;

    msgctl &= 0x3FFFu;
    msgctl |= (1u << 15);

    uint32_t new_dword = (cap_dword & 0x0000FFFFu) | ((uint32_t)msgctl << 16);
    pci_write32(bus, slot, func, (uint8_t)cap, new_dword);

    uint32_t verify_dword = pci_read32(bus, slot, func, (uint8_t)cap);
    uint16_t verify = (uint16_t)((verify_dword >> 16) & 0xFFFF);

    if (!(verify & (1u << 15))) return -1;

    return 0;
}

static void xhci_enable_interrupter(xhci_t* x) {
    volatile uint8_t* ir = x->rt + 0x20;

    uint32_t cmd = mmio_rd32(x->op + 0x00);
    cmd |= XHCI_USBCMD_RS | XHCI_USBCMD_INTE;
    mmio_wr32(x->op + 0x00, cmd);

    mmio_wr32(x->op + 0x04, XHCI_USBSTS_EINT);
    mmio_wr32(ir + 0x04, 0);

    mmio_wr32(ir + 0x00, 1u);
    mmio_wr32(ir + 0x00, 2u);

    uint32_t iman_after = mmio_rd32(ir + 0x00);
    if (!(iman_after & 2u)) {
        printf("USB: IMAN.IE not set (IMAN=%x)\n", iman_after);
    }
}

void usb_handler_c(struct regs* r) {
    (void)r;
    xhci_t* x = &xhci_inst;

    usb_isr_count++;

    mmio_wr32(x->op + 0x04, XHCI_USBSTS_EINT);

    volatile uint8_t* ir = x->rt + 0x20;
    mmio_wr32(ir + 0x00, 1u);

    int got_xfer = 0;

    for (int budget = 0; budget < XHCI_RING_TRBS; budget++) {
        trb_t* e = &evt_ring[x->evt_idx];
        uint32_t ctrl = e->control;
        uint8_t cyc = ctrl & 1;
        uint8_t type = (ctrl >> 10) & 0x3F;

        if (cyc != x->evt_cycle) break;

        if (type == XHCI_TRB_XFER_EVT) {
            uint8_t code = (e->status >> 24) & 0xFF;
            uint8_t epid = (ctrl >> 16) & 0x1F;
            uint64_t p = e->parameter;

            xhci_evt_advance(x);

            if (code != 1) {
                usb_bad_code_count++;
            }

            if (epid == (hid_ep_num * 2 + 1)) {
                xhci_process_keys(intr_buf);
                got_xfer = 1;
            }

            (void)p;
        } else if (type == XHCI_TRB_PORTSC) {
            xhci_evt_advance(x);
        } else if (type == XHCI_TRB_CMD_CMPL) {
            xhci_evt_advance(x);
        } else {
            break;
        }
    }

    usb_xfer_pending = 0;

    if (got_xfer && usb_active_slot > 0) {
        xhci_queue_interrupt_in_async(x, (uint8_t)usb_active_slot);
        usb_xfer_pending = 1;
    }

    mmio_wr32(ir + 0x00, 2u);
}

static int xhci_setup_keyboard(xhci_t* x, uint32_t port, uint32_t speed) {
    int slot = xhci_enable_slot(x);
    if (slot < 0) return -1;

    if (xhci_address_device(x, (uint8_t)slot, port, speed) != 0) return -1;

    int rc = xhci_get_descriptor(x, (uint8_t)slot, 0x01, 0, ctrl_buf, 18);
    if (rc != 1) {
        printf("USB: GET Device Descriptor failed (rc=%d)\n", rc);
        return -1;
    }

    rc = xhci_get_descriptor(x, (uint8_t)slot, 0x02, 0, ctrl_buf, 256);
    if (rc != 1) {
        printf("USB: GET Config Descriptor failed (rc=%d)\n", rc);
        return -1;
    }

    uint32_t total = (uint32_t)(ctrl_buf[2] | (ctrl_buf[3] << 8));

    uint8_t iface = 0;
    uint8_t ep_addr = 0;
    uint16_t mps = 0;
    uint8_t interval = 0;
    if (xhci_find_hid_keyboard(ctrl_buf, total, &iface, &ep_addr, &mps, &interval) != 0) {
        printf("USB: no HID keyboard interface\n");
        return -1;
    }

    uint8_t cfg_val = ctrl_buf[5];

    rc = xhci_set_configuration(x, (uint8_t)slot, cfg_val);
    if (rc != 1) {
        printf("USB: SET_CONFIGURATION failed (rc=%d)\n", rc);
        return -1;
    }

    xhci_set_protocol_boot(x, (uint8_t)slot, iface);
    xhci_set_idle(x, (uint8_t)slot, iface);

    uint8_t ep_num = ep_addr & 0x0F;
    hid_ep_num = ep_num;
    hid_ep_mps = mps;
    hid_interface = iface;

    rc = xhci_configure_endpoint(x, (uint8_t)slot, ep_num, 7, mps, interval);
    if (rc != 0) return -1;

    memzero(prev_keys, 6);
    memzero(intr_buf, sizeof(intr_buf));

    return slot;
}

static void xhci_run(uintptr_t mmio_base, uint8_t bus, uint8_t slot, uint8_t func) {
    xhci_t* x = &xhci_inst;
    memzero(x, sizeof(*x));

    x->bar_phys = (uint64_t)mmio_base;
    x->pci_bus = bus;
    x->pci_slot = slot;
    x->pci_func = func;

    xhci_parse_caps(x, mmio_base);
    xhci_reset(x);

    mmio_wr32(x->op + 0x38, x->max_slots);
    xhci_setup_dcbaap(x);
    xhci_setup_cmd_ring(x);
    xhci_setup_evt_ring(x);
    xhci_start(x);

    uint32_t speed = 0;
    uint32_t found_port = 0;
    int found = 0;
    for (uint32_t p = 0; p < x->max_ports; p++) {
        volatile uint32_t* portsc = (volatile uint32_t*)(x->ports + p * 0x10);
        uint32_t val = mmio_rd32(portsc);
        mmio_wr32(portsc, XHCI_PORTSC_CSC | XHCI_PORTSC_PEC | XHCI_PORTSC_WRC
                       | XHCI_PORTSC_OCC | XHCI_PORTSC_PRC | XHCI_PORTSC_PLC
                       | XHCI_PORTSC_CEC);
        if (!(val & XHCI_PORTSC_CCS)) continue;
        xhci_port_reset(x, p, &speed);
        found_port = p + 1;
        found = 1;
        break;
    }

    if (!found) {
        printf("USB: no device on any port\n");
        return;
    }

    int kbslot = xhci_setup_keyboard(x, found_port, speed);
    if (kbslot < 0) {
        printf("USB: keyboard setup failed\n");
        return;
    }

    usb_active_slot = kbslot;

    lapic_enable();

    if (xhci_setup_msix(x) == 0) {
        if (irq_register(USB_IRQ_VECTOR, usb_handler_c, "xHCI") != 0) {
            printf("USB: IRQ %d already in use\n", USB_IRQ_VECTOR);
            return;
        }
        xhci_enable_interrupter(x);
        usb_xfer_pending = 0;
        xhci_queue_interrupt_in_async(x, (uint8_t)kbslot);
        usb_xfer_pending = 1;
        usb_irq_active = 1;
        printf("USB: keyboard ready (IRQ %d)\n", USB_IRQ_VECTOR);
        return;
    }
}

//
void usb_init(void) {
    for (int bus = 0; bus < 256; bus++) {
        for (int slot = 0; slot < 32; slot++) {
            for (int func = 0; func < 8; func++) {
                uint32_t id = pci_read32((uint8_t)bus, (uint8_t)slot, (uint8_t)func, 0);
                if ((id & 0xFFFF) == 0xFFFF) continue;

                uint32_t class = pci_read32((uint8_t)bus, (uint8_t)slot, (uint8_t)func, 0x08);
                uint8_t base_class = (class >> 24) & 0xFF;
                uint8_t sub_class  = (class >> 16) & 0xFF;
                uint8_t prog_if    = (class >> 8) & 0xFF;

                if (base_class != 0x0C || sub_class != 0x03) continue;

                uint32_t cmd = pci_read32((uint8_t)bus, (uint8_t)slot, (uint8_t)func, 0x04);
                cmd |= 0x0007;
                pci_write32((uint8_t)bus, (uint8_t)slot, (uint8_t)func, 0x04, cmd);

                int is_io = 0;
                uint64_t bar = pci_read_bar64((uint8_t)bus, (uint8_t)slot, (uint8_t)func, 0x10, &is_io);

                if (prog_if == 0x30) {
                    printf("USB xHCI at %x:%x.%x, MMIO=%lx\n",
                        bus, slot, func, (unsigned long)bar);
                    if (bar == 0) {
                        printf("USB: MMIO BAR is zero\n");
                        return;
                    }
                    xhci_run((uintptr_t)bar, (uint8_t)bus, (uint8_t)slot, (uint8_t)func);
                    return;
                }
                else if (prog_if == 0x20) { printf("USB EHCI unsupported\n"); return; }
                else if (prog_if == 0x10) { printf("USB OHCI unsupported\n"); return; }
                else if (prog_if == 0x00) { printf("USB UHCI unsupported\n"); return; }
            }
        }
    }
    printf("USB: no controller found\n");
}
