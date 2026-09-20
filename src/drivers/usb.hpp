#pragma once
#include <stdint.h>
#include <stddef.h>

struct regs;

#define USB_IRQ_VECTOR 0x40

#ifdef __cplusplus

constexpr size_t RING_TRBS       = 256;
constexpr size_t MAX_SCRATCHPAD  = 32;

#pragma pack(push, 1)
struct trb_t {
    uint64_t parameter;
    uint32_t status;
    uint32_t control;
};

struct erst_entry_t {
    uint64_t rsba;
    uint32_t rssz;
    uint32_t reserved;
};
#pragma pack(pop)

class XhciController {
public:
    void parse_caps(uintptr_t mmio_base);
    void halt();
    void reset();
    void setup_dcbaap();
    void setup_cmd_ring();
    void setup_evt_ring();
    void start();
    void evt_advance();
    int  wait_cmd(uint64_t cmd_phys, uint8_t* out_slot);
    int  submit_cmd(uint32_t type, uint64_t param, uint32_t status, uint8_t slot_id, uint8_t* out_slot);
    void ep0_init();
    int  wait_xfer(uint64_t trb_phys, uint32_t* out_len);
    int  control_xfer(uint8_t slot, uint8_t bmRequestType, uint8_t bRequest,
                      uint16_t wValue, uint16_t wIndex, uint16_t wLength,
                      void* data, int dir_in);
    int  enable_slot();
    int  address_device(uint8_t slot, uint32_t port, uint32_t speed);
    int  get_descriptor(uint8_t slot, uint8_t type, uint8_t idx, void* buf, uint16_t len);
    int  set_configuration(uint8_t slot, uint8_t cfg);
    int  set_protocol_boot(uint8_t slot, uint8_t iface);
    int  set_idle(uint8_t slot, uint8_t iface);
    int  configure_endpoint(uint8_t slot, uint8_t ep_num, uint8_t ep_type, uint16_t mps, uint8_t interval);
    int  queue_interrupt_in_async(uint8_t slot);
    void port_reset(uint32_t port, uint32_t* out_speed);
    int  find_hid_keyboard(const uint8_t* cfg, uint32_t total, uint8_t* out_iface,
                           uint8_t* out_ep_addr, uint16_t* out_mps, uint8_t* out_interval);
    void process_keys(const uint8_t* report);
    int  setup_keyboard(uint32_t port, uint32_t speed);
    void run(uintptr_t mmio_base, uint8_t bus, uint8_t slot, uint8_t func);
    void handle_interrupt();

    int  setup_msix();
    void enable_interrupter();

private:
    volatile uint8_t*  mmio  = nullptr;
    volatile uint8_t*  op    = nullptr;
    volatile uint8_t*  rt    = nullptr;
    volatile uint8_t*  ports = nullptr;
    volatile uint32_t* db    = nullptr;

    uint8_t  caplength      = 0;
    uint8_t  max_slots      = 0;
    uint16_t max_intrs      = 0;
    uint8_t  max_ports      = 0;
    uint32_t num_scratchpad = 0;
    uint32_t pagesize       = 0;

    uint64_t dcbaap_phys    = 0;
    uint64_t cmd_ring_phys  = 0;
    uint64_t evt_ring_phys  = 0;
    uint64_t erst_phys      = 0;

    uint8_t pci_bus = 0, pci_slot = 0, pci_func = 0;

    alignas(64)   uint64_t     dcbaap[256]{};
    alignas(64)   uint64_t     scratchpad_array[MAX_SCRATCHPAD]{};
    alignas(4096) uint8_t      scratchpad_data[MAX_SCRATCHPAD][4096]{};
    alignas(64)   trb_t        cmd_ring[RING_TRBS]{};
    alignas(64)   trb_t        evt_ring[RING_TRBS]{};
    alignas(64)   erst_entry_t erst[1]{};
    alignas(64)   trb_t        ep0_ring[RING_TRBS]{};
    alignas(64)   trb_t        intr_ring[RING_TRBS]{};
    alignas(64)   uint8_t      input_ctx[1056]{};
    alignas(64)   uint8_t      dev_ctx[256][1024]{};
    alignas(64)   uint8_t      ctrl_buf[512]{};
    alignas(64)   uint8_t      intr_buf[64]{};

    uint32_t cmd_idx = 0, cmd_cycle = 0;
    uint32_t evt_idx = 0, evt_cycle = 0;
    uint32_t ep0_idx = 0, ep0_cycle = 0;
    uint32_t intr_idx = 0, intr_cycle = 0;

    uint8_t  prev_keys[6]{};
    uint8_t  hid_ep_num    = 0;
    uint16_t hid_ep_mps    = 0;
    uint8_t  hid_interface = 0;
    int      usb_active_slot = 0;
    int      usb_irq_active  = 0;
};

extern "C" {
#endif

void     usb_init(void);
uint64_t usb_get_isr_count(void);
uint64_t usb_get_keys_count(void);
uint64_t usb_get_push_count(void);
uint64_t usb_get_bad_code_count(void);
void     usb_handler_c(struct regs* r);

#ifdef __cplusplus
}
#endif
