#pragma once

#include <stdint.h>

#ifdef __cplusplus
namespace Pci {

constexpr uint16_t CONFIG_ADDRESS = 0xCF8; 
constexpr uint16_t CONFIG_DATA    = 0xCFC;

enum class DeviceType {
    Xhci,         
    Ehci,         
    Ahci,         
    IntelE1000,   
    Nvme          
};

struct PciDevice {
    uint8_t bus        {0};
    uint8_t slot       {0};
    uint8_t func       {0};
    uint64_t mmio_base {0};
    uint16_t vendor_id {0};
    uint16_t device_id {0};
};

class PciScanner {
    public:
        PciScanner() = delete;
        ~PciScanner() = delete;
        static bool findPciDevice(DeviceType deviceType, PciDevice& outDev) noexcept;
    private:
        static uint32_t readConfig(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) noexcept;
};
}
#endif


#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PCI_DEVICE_XHCI = 0,
    PCI_DEVICE_EHCI,
    PCI_DEVICE_AHCI,
    PCI_DEVICE_E1000,
    PCI_DEVICE_NVME
} pci_device_type_t;

typedef struct {
    uint8_t bus;
    uint8_t slot;
    uint8_t func;
    uint64_t mmio_base;
    uint16_t vendor_id;
    uint16_t device_id;
} pci_device_t;

int pci_find_device(pci_device_type_t type, pci_device_t* out_dev);

#ifdef __cplusplus
}
#endif
