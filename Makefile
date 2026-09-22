CC  = gcc
CXX = g++
AS  = as
LD  = ld
OBJCOPY = objcopy

IMG   = raryos.img

SRC_DIR   = src
BUILD_DIR = build

ELF64 = $(BUILD_DIR)/my_kernel.elf64
BIN   = $(BUILD_DIR)/my_kernel.bin

INCLUDE_FLAGS = -I$(SRC_DIR)/lib \
                -I$(SRC_DIR)/arch/x86_64 \
                -I$(SRC_DIR)/drivers \
                -I$(SRC_DIR)/kernel \
                -I$(SRC_DIR)/kernel/memory \
                -I$(SRC_DIR)/boot

COMMON_FLAGS  = -m64 -ffreestanding -O2 -nostdlib -mno-red-zone -mcmodel=small -fno-pic $(INCLUDE_FLAGS)

CFLAGS   = $(COMMON_FLAGS) -std=gnu99 -Wall -Wextra
CXXFLAGS = $(COMMON_FLAGS) -fno-exceptions -fno-rtti -fno-threadsafe-statics -Wall -Wextra
ASFLAGS  = --64

SRCS_S   = $(shell find $(SRC_DIR) -name '*.s')
SRCS_C   = $(shell find $(SRC_DIR) -name '*.c')
SRCS_CPP = $(shell find $(SRC_DIR) -name '*.cpp')

OBJS = $(patsubst $(SRC_DIR)/%, $(BUILD_DIR)/%.o, $(SRCS_S) $(SRCS_C) $(SRCS_CPP))

.PHONY: all clean

all: $(IMG)

$(IMG): $(BIN)
	@echo " [ISO]     Creating $(IMG)..."
	@mkdir -p $(BUILD_DIR)/iso/boot/grub
	@cp $(BIN) $(BUILD_DIR)/iso/boot/my_kernel.bin
	@echo "set timeout=0" > $(BUILD_DIR)/iso/boot/grub/grub.cfg
	@echo "set default=0" >> $(BUILD_DIR)/iso/boot/grub/grub.cfg
	@echo "insmod all_video" >> $(BUILD_DIR)/iso/boot/grub/grub.cfg
	@echo "insmod efi_gop" >> $(BUILD_DIR)/iso/boot/grub/grub.cfg
	@echo "set gfxmode=1024x768x32,1024x768,800x600x32,800x600,640x480x32,640x480,auto" >> $(BUILD_DIR)/iso/boot/grub/grub.cfg
	@echo "set gfxpayload=keep" >> $(BUILD_DIR)/iso/boot/grub/grub.cfg
	@echo 'menuentry "RaryOS" {' >> $(BUILD_DIR)/iso/boot/grub/grub.cfg
	@echo "    multiboot2 /boot/my_kernel.bin" >> $(BUILD_DIR)/iso/boot/grub/grub.cfg
	@echo "    boot" >> $(BUILD_DIR)/iso/boot/grub/grub.cfg
	@echo "}" >> $(BUILD_DIR)/iso/boot/grub/grub.cfg
	@grub-mkrescue -o $(IMG) $(BUILD_DIR)/iso >/dev/null 2>&1 || (echo "ERROR: grub-mkrescue failed"; exit 1)
	@rm -rf $(BUILD_DIR)/iso
	@echo "Successfully built: $(IMG)"

$(BIN): $(ELF64)
	@echo " [OBJCOPY] $@"
	@$(OBJCOPY) -O elf32-i386 $(ELF64) $(BIN)

$(ELF64): $(OBJS)
	@echo " [LD]      $@"
	@$(LD) -m elf_x86_64 -z max-page-size=0x1000 -T linker.ld -o $(ELF64) $(OBJS)

$(BUILD_DIR)/%.s.o: $(SRC_DIR)/%.s
	@mkdir -p $(dir $@)
	@echo " [AS]      $<"
	@$(AS) $(ASFLAGS) $< -o $@

$(BUILD_DIR)/%.c.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	@echo " [CC]      $<"
	@$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.cpp.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(dir $@)
	@echo " [CXX]     $<"
	@$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	@echo " [CLEAN]   Removing build folder and $(IMG)..."
	@rm -rf $(BUILD_DIR) $(IMG)
	@rm -f *.log
