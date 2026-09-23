#pragma once
#include <stdint.h>

struct idt_entry_struct {
    uint16_t base_low;
    uint16_t sel;
    uint8_t  ist;
    uint8_t  flags;
    uint16_t base_mid;
    uint32_t base_high;
    uint32_t reserved;
} __attribute__((packed));

typedef struct idt_entry_struct idt_entry_t;

struct idt_ptr_struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

typedef struct idt_ptr_struct idt_ptr_t;

struct regs {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rdi, rsi, rbp, rbx, rdx, rcx, rax;
    uint64_t vector, error;
    uint64_t rip, cs, rflags, rsp, ss;
} __attribute__((packed));

#define IDT_ENTRIES 256
#define IRQ_STUB_COUNT 48

extern idt_entry_t idt[IDT_ENTRIES];
extern idt_ptr_t   idt_ptr;

extern void idt_flush(uint64_t);
extern uint64_t isr_stub_table[32];
extern uint64_t irq_stub_table[IRQ_STUB_COUNT];
extern void isr128(void);