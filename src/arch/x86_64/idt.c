#include "idt.h"
#include "irq.h"
#include "input.h"
#include "stdio.h"
#include "io.h"

extern void idt_flush(uint64_t idt_ptr_address);
extern void terminal_write_string(const char* data);

idt_entry_t idt[IDT_ENTRIES];
idt_ptr_t   idt_ptr;

void idt_set_gate(uint8_t num, uint64_t base, uint16_t sel, uint8_t flags) {
    idt[num].base_low  = base & 0xFFFF;
    idt[num].base_mid  = (base >> 16) & 0xFFFF;
    idt[num].base_high = (base >> 32) & 0xFFFFFFFF;

    idt[num].sel      = sel;
    idt[num].ist      = 0;
    idt[num].flags    = flags;
    idt[num].reserved = 0;
}

void exception_handler(struct regs* r) {
    printf("\n*** EXCEPTION ***\n");
    printf("Vector: %d\n", (int)r->vector);
    printf("Error : %lx\n", r->error);
    printf("RIP   : %lx\n", r->rip);
    printf("CS    : %lx\n", r->cs);
    printf("RFLAGS: %lx\n", r->rflags);
    printf("RSP   : %lx\n", r->rsp);
    printf("SS    : %lx\n", r->ss);
    printf("RAX   : %lx\n", r->rax);
    printf("RBX   : %lx\n", r->rbx);
    printf("RCX   : %lx\n", r->rcx);
    printf("RDX   : %lx\n", r->rdx);
    printf("RSI   : %lx\n", r->rsi);
    printf("RDI   : %lx\n", r->rdi);

    if (r->vector == 14) {
        uint64_t cr2;
        asm volatile("mov %%cr2, %0" : "=r"(cr2));
        printf("CR2   : %lx\n", cr2);
    }

    printf("Halted.\n");
    asm volatile("cli");
    while (1) asm volatile("hlt");
}

void init_idt(void) {
    idt_ptr.limit = (sizeof(idt_entry_t) * IDT_ENTRIES) - 1;
    idt_ptr.base  = (uint64_t)&idt;

    for (int i = 0; i < IDT_ENTRIES; i++) {
        idt_set_gate(i, 0, 0, 0);
    }

    for (int i = 0; i < 32; i++) {
        idt_set_gate((uint8_t)i, isr_stub_table[i], 0x08, 0x8E);
    }

    for (int i = 0; i < IRQ_STUB_COUNT; i++) {
        idt_set_gate((uint8_t)(32 + i), irq_stub_table[i], 0x08, 0x8E);
    }

    idt_flush((uint64_t)&idt_ptr);
}

#define PIC1          0x20
#define PIC2          0xA0
#define PIC1_COMMAND  PIC1
#define PIC1_DATA     (PIC1+1)
#define PIC2_COMMAND  PIC2
#define PIC2_DATA     (PIC2+1)

void pic_remap(void) {
    outb(PIC1_COMMAND, 0x11);
    io_wait();
    outb(PIC2_COMMAND, 0x11);
    io_wait();

    outb(PIC1_DATA, 0x20);
    io_wait();

    outb(PIC2_DATA, 0x28);
    io_wait();

    outb(PIC1_DATA, 0x04);
    io_wait();
    outb(PIC2_DATA, 0x02);
    io_wait();

    outb(PIC1_DATA, 0x01);
    io_wait();
    outb(PIC2_DATA, 0x01);
    io_wait();

    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);
}

extern void terminal_putchar(char c);

static const char keyboard_map[128] = {
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
  '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0,  'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',   0,
 '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/',   0, '*',   0, ' '
};

void ps2_keyboard_handler(struct regs* r) {
    (void)r;
    uint8_t scancode = inb(0x60);

    if (scancode & 0x80) {
        return;
    }

    if (scancode > 0 && scancode < 58) {
        char ascii_char = keyboard_map[scancode];
        if (ascii_char != 0) {
            input_push(ascii_char);
        }
    }
}