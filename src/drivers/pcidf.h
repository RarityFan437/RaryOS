#include <stdint.h>
#include <stddef.h>

struct PciVendorName {
    uint16_t    vendor_id;
    const char* name;
};

struct PciDeviceName {
    uint16_t    vendor_id;
    uint16_t    device_id;
    const char* name;
};

static constexpr PciVendorName pci_vendors[] = {
    { 0x8086, "Intel Corporation" },
    { 0x10DE, "NVIDIA Corporation" },
    { 0x1002, "Advanced Micro Devices, Inc. [AMD/ATI]" },
    { 0x10EC, "Realtek Semiconductor Co., Ltd." },
    { 0x1B36, "Red Hat, Inc. (QEMU Virtual Devices)" },
    { 0x1106, "VIA Technologies, Inc." },
    { 0x15AD, "VMware" },
    { 0x80EE, "Oracle Corporation (VirtualBox)" },
    { 0x1011, "Digital Equipment Corporation (DEC)" },
    { 0x106B, "Apple Inc." }
};

static constexpr PciDeviceName pci_devices[] = {
    // === Intel (0x8086) ===
    { 0x8086, 0x100E, "82540EM Gigabit Ethernet Controller (e1000)" },
    { 0x8086, 0x15B8, "Ethernet Connection (2) I219-V" },
    { 0x8086, 0x2822, "SATA Controller [RAID mode]" },
    { 0x8086, 0x1C22, "6 Series/C200 Series Chipset Family SMBus Controller" },
    { 0x8086, 0xA12F, "100 Series/C230 Series Chipset Family USB 3.0 xHCI Controller" },
    { 0x8086, 0x2415, "82801AA AC'97 Audio Controller" },
    { 0x8086, 0x7000, "82371SB PIIX3 ISA [Bridge]" },
    { 0x8086, 0x7010, "82371SB PIIX3 IDE [Interface]" },
    { 0x8086, 0x7111, "82371AB/EB/MB PIIX4 IDE" },
    { 0x8086, 0x2918, "82801IB (ICH9) LPC Interface Controller" },
    { 0x8086, 0x2922, "82801IR/IO/IH (ICH9R/DO/DH) 6 port SATA Controller [AHCI mode]" },

    // === Red Hat / QEMU (0x1B36) ===
    { 0x1B36, 0x000D, "QEMU XHCI USB Host Controller" },
    { 0x1B36, 0x0001, "QEMU PCI-to-PCI Bridge" },

    // === Realtek (0x10EC) ===
    { 0x10EC, 0x8139, "RTL8139 RTL8139C+ Fast Ethernet Adapter" },
    { 0x10EC, 0x8168, "RTL8111/8168/8411 PCI Express Gigabit Ethernet Controller" },

    // === VMware (0x15AD) ===
    { 0x15AD, 0x0405, "SVGA II Adapter" },
    { 0x15AD, 0x0740, "VMware Virtual Machine Communication Interface" },

    // === Oracle / VirtualBox (0x80EE) ===
    { 0x80EE, 0xBEEF, "VirtualBox Graphics Adapter" },
    { 0x80EE, 0xCAFE, "VirtualBox Guest Service" },

    // === AMD / ATI (0x1002 / 0x1022) ===
    { 0x1002, 0x4391, "SB7x0/SB8x0/SB9x0 SATA Controller [AHCI mode]" },
    { 0x1022, 0x7814, "FCH USB XHCI Controller" }
};

static constexpr size_t VENDORS_COUNT = sizeof(pci_vendors) / sizeof(pci_vendors[0]);
static constexpr size_t DEVICES_COUNT = sizeof(pci_devices) / sizeof(pci_devices[0]);

/**
 * Ищет название компании-производителя по Vendor ID
 */
const char* pci_get_vendor_name(uint16_t vendor_id) {
    for (size_t i = 0; i < VENDORS_COUNT; i++) {
        if (pci_vendors[i].vendor_id == vendor_id) {
            return pci_vendors[i].name;
        }
    }
    return "Unknown Vendor";
}

/**
 * Ищет название конкретного устройства по Vendor ID и Device ID
 */
const char* pci_get_device_name(uint16_t vendor_id, uint16_t device_id) {
    for (size_t i = 0; i < DEVICES_COUNT; i++) {
        if (pci_devices[i].vendor_id == vendor_id && pci_devices[i].device_id == device_id) {
            return pci_devices[i].name;
        }
    }
    return "Unknown Device";
}
