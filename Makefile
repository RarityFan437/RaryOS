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

# ---------------------------------------------------------------------------
# Kernel build
# ---------------------------------------------------------------------------
INCLUDE_FLAGS = -I$(SRC_DIR)/lib \
                -I$(SRC_DIR)/arch/x86_64 \
                -I$(SRC_DIR)/drivers \
                -I$(SRC_DIR)/kernel \
                -I$(SRC_DIR)/kernel/memory \
                -I$(SRC_DIR)/kernel/graphics \
                -I$(SRC_DIR)/kernel/user \
                -I$(SRC_DIR)/kernel/smbios \
                -I$(SRC_DIR)/boot

COMMON_FLAGS  = -m64 -ffreestanding -O2 -nostdlib -mno-red-zone -mcmodel=small -fno-pic $(INCLUDE_FLAGS)

CFLAGS   = $(COMMON_FLAGS) -std=gnu99 -Wall -Wextra
CXXFLAGS = $(COMMON_FLAGS) -fno-exceptions -fno-rtti -fno-threadsafe-statics -Wall -Wextra
ASFLAGS  = --64

ALL_S    = $(shell find $(SRC_DIR) -name '*.s')
ALL_C    = $(shell find $(SRC_DIR) -name '*.c')
ALL_CPP  = $(shell find $(SRC_DIR) -name '*.cpp')

SRCS_S   = $(filter-out $(SRC_DIR)/user_libc/% $(SRC_DIR)/programs/%, $(ALL_S))
SRCS_C   = $(filter-out $(SRC_DIR)/user_libc/% $(SRC_DIR)/programs/%, $(ALL_C))
SRCS_CPP = $(filter-out $(SRC_DIR)/user_libc/% $(SRC_DIR)/programs/%, $(ALL_CPP))

OBJS = $(patsubst $(SRC_DIR)/%, $(BUILD_DIR)/%.o, $(SRCS_S) $(SRCS_C) $(SRCS_CPP))

# ---------------------------------------------------------------------------
# User program paths
# ---------------------------------------------------------------------------
USER_CFLAGS := -m64 -ffreestanding -O2 -nostdlib -fno-pic -fno-pie \
               -mno-red-zone -mno-sse -mno-sse2 -mno-mmx \
               -Wall -Wextra \
               -Isrc/user_libc

USER_LDFLAGS := -m elf_x86_64 -T src/programs/shell/linker.ld \
                -nostdlib -static -no-pie

USER_LIB_SRCS := $(wildcard src/user_libc/*.c)
USER_LIB_ASM  := $(wildcard src/user_libc/*.s)
USER_LIB_OBJS := $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(USER_LIB_SRCS)) \
                 $(patsubst src/%.s,$(BUILD_DIR)/%.o,$(USER_LIB_ASM))

USER_SHELL_OBJS := $(BUILD_DIR)/programs/shell/main.o $(USER_LIB_OBJS)
USER_SHELL_ELF  := $(BUILD_DIR)/programs/shell/shell.elf
USER_SHELL_BIN  := $(BUILD_DIR)/programs/shell/shell_elf.o

.PHONY: all clean user

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

$(ELF64): $(OBJS) $(USER_SHELL_BIN)
	@echo " [LD]      $@"
	@$(LD) -m elf_x86_64 -z max-page-size=0x1000 -z noexecstack \
	    -T linker.ld -o $(ELF64) $(OBJS) $(USER_SHELL_BIN)

# ---------------------------------------------------------------------------
# Kernel compilation rules
# ---------------------------------------------------------------------------
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

# ---------------------------------------------------------------------------
# User libc
# ---------------------------------------------------------------------------
$(BUILD_DIR)/user_libc/%.o: src/user_libc/%.c
	@mkdir -p $(dir $@)
	@echo " [USER CC] $<"
	@$(CC) $(USER_CFLAGS) -c $< -o $@

$(BUILD_DIR)/user_libc/%.o: src/user_libc/%.s
	@mkdir -p $(dir $@)
	@echo " [USER AS] $<"
	@$(CC) -m64 -c $< -o $@

# ---------------------------------------------------------------------------
# User program
# ---------------------------------------------------------------------------
$(BUILD_DIR)/programs/shell/main.o: src/programs/shell/main.c
	@mkdir -p $(dir $@)
	@echo " [USER CC] $<"
	@$(CC) $(USER_CFLAGS) -c $< -o $@

$(USER_SHELL_ELF): $(USER_SHELL_OBJS) src/programs/shell/linker.ld
	@echo " [USER LD] $@"
	@$(LD) $(USER_LDFLAGS) -o $@ $(USER_SHELL_OBJS)

$(USER_SHELL_BIN): $(USER_SHELL_ELF)
	@echo " [EMBED]   $<"
	@cd $(BUILD_DIR)/programs/shell && \
	    $(LD) -r -b binary -o shell_elf.o shell.elf

user: $(USER_SHELL_ELF)
	@echo "User program built: $(USER_SHELL_ELF)"
	@readelf -h $(USER_SHELL_ELF) | head -20

# ---------------------------------------------------------------------------
# Clean
# ---------------------------------------------------------------------------
clean:
	@echo " [CLEAN]   Removing build folder and $(IMG)..."
	@rm -rf $(BUILD_DIR) $(IMG)
	@rm -f *.log