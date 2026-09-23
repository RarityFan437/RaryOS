#include <stdint.h>
#include "user.h"
#include "elf.h"
#include "stdio.h"
#include "string.h"

extern const uint8_t _binary_shell_elf_start[];
extern const uint8_t _binary_shell_elf_end[];

static uint8_t user_stack[16384] __attribute__((aligned(4096)));

void jump_to_user(void) {
    uint64_t entry = 0;

    if (elf_load(_binary_shell_elf_start, &entry) != 0) {
        printf("Failed to load ELF\n");
        return;
    }

    printf("Entry point: %lx\n", (unsigned long)entry);

    uint64_t stack_top = (uint64_t)user_stack + sizeof(user_stack);

    asm volatile(
        "cli\n"
        "mov $0x2B, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "push $0x2B\n"          // SS = user data, index 5
        "push %0\n"             // RSP
        "push $0x202\n"         // RFLAGS
        "push $0x33\n"          // CS = user code, index 6
        "push %1\n"             // RIP
        "iretq\n"
        :
        : "r"(stack_top), "r"(entry)
        : "memory"
    );
}
