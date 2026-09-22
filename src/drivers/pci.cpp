#include "pci.hpp"
#include "io.h"

namespace Pci {

struct DeviceSignature {
    DeviceType    type;
    bool          use_class_matching;
    uint8_t  class_code;
    uint8_t  subclass;
    uint16_t vendor_id;
    uint16_t device_id;
};

static constexpr DeviceSignature lookup_table[] = {
    // Type             MatchByClass  Class  Sub    Vendor  Device
    { DeviceType::Xhci,       true,   0x0C,  0x03,  0x0000, 0x0000 }, // xHCI (Любой USB 3.0)
    { DeviceType::Ehci,       true,   0x0C,  0x03,  0x0000, 0x0000 }, // EHCI (USB 2.0)
    { DeviceType::Ahci,       true,   0x01,  0x06,  0x0000, 0x0000 }, // SATA AHCI
    { DeviceType::Nvme,       true,   0x01,  0x08,  0x0000, 0x0000 }, // NVMe Controller
    { DeviceType::IntelE1000, false,  0x00,  0x00,  0x8086, 0x100E }  // Intel e1000
};

uint32_t PciScanner::readConfig(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) noexcept
{
    uint32_t address = ((static_cast<uint32_t>(1) << 31)    | 
                        (static_cast<uint32_t>(bus) << 16)  |
                        (static_cast<uint32_t>(slot) << 11) |
                        (static_cast<uint32_t>(func) << 8)  | 
                        (offset & 0xFC));
    outl(CONFIG_ADDRESS, address);
    return inl(CONFIG_DATA);
}

bool PciScanner::findPciDevice(DeviceType type, PciDevice& outDev) noexcept
{
    DeviceSignature sig{};
    bool found_sig = false;
    for (const auto& item : lookup_table) {
        if (item.type == type) {
            sig = item;
            found_sig = true;
            break;
        }
    }
    if (!found_sig) return false;

    for (uint32_t bus = 0; bus < 256; ++bus)
    {
        for (uint32_t slot = 0; slot < 32; ++slot)
        {
            // есть ли устройство в этом слоте
            uint32_t id_f0 = readConfig(bus, slot, 0, 0x00);
            if ((id_f0 & 0xFFFF) == 0xFFFF) continue;

            // Читаем заголовок функции 0, чтобы узнать, многофункциональное ли устройство
            uint32_t header_f0 = readConfig(bus, slot, 0, 0x0C);
            bool is_multi_function = (header_f0 >> 16) & 0x80;

            // проходимся по каждой функции
            for (uint32_t func = 0; func < 8; ++func)
            {
                // нет смысла проверять все функции если устройство не многофункциональное
                if (func > 0 && !is_multi_function) break;

                // читаем конфиг устройства
                uint32_t id = readConfig(bus, slot, func, 0x00);
                if ((id & 0xFFFF) == 0xFFFF) continue;
                
                uint16_t vendorId = id & 0xFFFF;
                uint16_t deviceId = (id >> 16) & 0xFFFF;

                uint32_t classRev = readConfig(bus, slot, func, 0x08);
                uint8_t classCode = (classRev >> 24) & 0xFF;
                uint8_t subclass  = (classRev >> 16) & 0xFF;
                uint8_t progIf    = (classRev >> 8)  & 0xFF; 
                
                // проверяем точно ли совпадает то что мы достали с сигнатурой
                bool is_match = false;
                if (sig.use_class_matching) {
                    if (classCode == sig.class_code && subclass == sig.subclass) {
                        if (type == DeviceType::Xhci && progIf != 0x30) continue;
                        if (type == DeviceType::Ehci && progIf != 0x20) continue; 
                        is_match = true;
                    }
                } else {
                    // корректируем если что
                    is_match = (vendorId == sig.vendor_id && deviceId == sig.device_id);
                }

                if (is_match) {
                    outDev.bus       = static_cast<uint8_t>(bus);
                    outDev.slot      = static_cast<uint8_t>(slot);
                    outDev.func      = static_cast<uint8_t>(func);
                    outDev.vendor_id = vendorId;
                    outDev.device_id = deviceId;

                    uint32_t bar0 = readConfig(bus, slot, func, 0x10);
                    
                    if ((bar0 & 0x1) == 0) {
                        if (((bar0 >> 1) & 0x3) == 2) { 
                            uint32_t bar1 = readConfig(bus, slot, func, 0x14);
                            outDev.mmio_base = (static_cast<uint64_t>(bar1) << 32) | (static_cast<uint64_t>(bar0) & 0xFFFFFFF0ULL);
                        } else {
                            outDev.mmio_base = bar0 & 0xFFFFFFF0;
                        }
                    } else {
                        outDev.mmio_base = bar0 & 0xFFFFFFFC;
                    }
                    return true; 
                }
            }
        }
    }
    return false;
}

extern "C" int pci_find_device(pci_device_type_t type, pci_device_t* out_dev) {
    if (!out_dev) return 0;

    Pci::DeviceType cpp_type = static_cast<Pci::DeviceType>(type);
    Pci::PciDevice cpp_dev;

    if (Pci::PciScanner::findPciDevice(cpp_type, cpp_dev)) {
        out_dev->bus       = cpp_dev.bus;
        out_dev->slot      = cpp_dev.slot;
        out_dev->func      = cpp_dev.func;
        out_dev->mmio_base = cpp_dev.mmio_base;
        out_dev->vendor_id = cpp_dev.vendor_id;
        out_dev->device_id = cpp_dev.device_id;
        return 1;
    }
    return 0;
}
}