.set MB2_MAGIC,  0xE85250D6
.set MB2_ARCH,   0
.set MB2_LEN,    (mb2_header_end - mb2_header)
.set MB2_CHECK,  -(MB2_MAGIC + MB2_ARCH + MB2_LEN)

.section .multiboot
.align 8
mb2_header:
    .long MB2_MAGIC
    .long MB2_ARCH
    .long MB2_LEN
    .long MB2_CHECK

    .align 8
    .word 5
    .word 0
    .long 20
    .long 1280
    .long 400
    .long 32

    .align 8
    .word 0
    .word 0
    .long 8
mb2_header_end:

.section .bss
.align 8
boot_magic:
.skip 8
boot_info:
.skip 8

.section .text
.global _start
.code32

_start:
    cli
    mov $stack_kernel_top, %esp

    mov %eax, boot_magic
    mov %ebx, boot_info

    .extern boot_main
    call boot_main

    .extern pml4
    mov $pml4, %eax
    mov %eax, %cr3

    mov %cr4, %eax
    or $0x20, %eax
    or $(1 << 9), %eax
    or $(1 << 10), %eax
    mov %eax, %cr4

    mov $0xC0000080, %ecx
    rdmsr
    or $(1 << 8), %eax
    wrmsr

    mov %cr0, %eax
    and $0xFFFFFFF3, %eax
    or $0x2, %eax
    or $0x80000000, %eax
    mov %eax, %cr0

    .extern gdt64_ptr
    lgdt (gdt64_ptr)

    ljmp $0x08, $.long_mode_start

.code64
.long_mode_start:
    mov $0x10, %ax
    mov %ax, %ss
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %fs
    mov %ax, %gs

    mov $stack_kernel_top, %rsp
    and $-16, %rsp

    mov $0x18, %ax
    ltr %ax

    mov boot_magic(%rip), %edi
    mov boot_info(%rip), %esi

    .extern kernel_main
    call kernel_main

    cli
1:
    hlt
    jmp 1b

.global idt_flush
idt_flush:
    lidt (%rdi)
    ret

.macro ISR_NOERR num
.global isr\num
isr\num:
    push $0
    push $\num
    jmp isr_common
.endm

.macro ISR_ERR num
.global isr\num
isr\num:
    push $\num
    jmp isr_common
.endm

ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_ERR   17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_ERR   21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_ERR   29
ISR_ERR   30
ISR_NOERR 31

.extern exception_handler
isr_common:
    push %rax
    push %rcx
    push %rdx
    push %rbx
    push %rbp
    push %rsi
    push %rdi
    push %r8
    push %r9
    push %r10
    push %r11
    push %r12
    push %r13
    push %r14
    push %r15

    mov %rsp, %rdi
    call exception_handler

    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %r11
    pop %r10
    pop %r9
    pop %r8
    pop %rdi
    pop %rsi
    pop %rbp
    pop %rbx
    pop %rdx
    pop %rcx
    pop %rax
    add $16, %rsp
    iretq

.macro IRQ_STUB num
.global irq\num
irq\num:
    push $0
    push $\num
    jmp irq_common
.endm

IRQ_STUB 32
IRQ_STUB 33
IRQ_STUB 34
IRQ_STUB 35
IRQ_STUB 36
IRQ_STUB 37
IRQ_STUB 38
IRQ_STUB 39
IRQ_STUB 40
IRQ_STUB 41
IRQ_STUB 42
IRQ_STUB 43
IRQ_STUB 44
IRQ_STUB 45
IRQ_STUB 46
IRQ_STUB 47
IRQ_STUB 48
IRQ_STUB 49
IRQ_STUB 50
IRQ_STUB 51
IRQ_STUB 52
IRQ_STUB 53
IRQ_STUB 54
IRQ_STUB 55
IRQ_STUB 56
IRQ_STUB 57
IRQ_STUB 58
IRQ_STUB 59
IRQ_STUB 60
IRQ_STUB 61
IRQ_STUB 62
IRQ_STUB 63
IRQ_STUB 64
IRQ_STUB 65
IRQ_STUB 66
IRQ_STUB 67
IRQ_STUB 68
IRQ_STUB 69
IRQ_STUB 70
IRQ_STUB 71
IRQ_STUB 72
IRQ_STUB 73
IRQ_STUB 74
IRQ_STUB 75
IRQ_STUB 76
IRQ_STUB 77
IRQ_STUB 78
IRQ_STUB 79

.extern irq_dispatch
irq_common:
    push %rax
    push %rcx
    push %rdx
    push %rbx
    push %rbp
    push %rsi
    push %rdi
    push %r8
    push %r9
    push %r10
    push %r11
    push %r12
    push %r13
    push %r14
    push %r15

    mov %rsp, %rdi
    call irq_dispatch

    pop %r15
    pop %r14
    pop %r13
    pop %r12
    pop %r11
    pop %r10
    pop %r9
    pop %r8
    pop %rdi
    pop %rsi
    pop %rbp
    pop %rbx
    pop %rdx
    pop %rcx
    pop %rax
    add $16, %rsp
    iretq

.section .data
.align 8
.global isr_stub_table
isr_stub_table:
    .quad isr0
    .quad isr1
    .quad isr2
    .quad isr3
    .quad isr4
    .quad isr5
    .quad isr6
    .quad isr7
    .quad isr8
    .quad isr9
    .quad isr10
    .quad isr11
    .quad isr12
    .quad isr13
    .quad isr14
    .quad isr15
    .quad isr16
    .quad isr17
    .quad isr18
    .quad isr19
    .quad isr20
    .quad isr21
    .quad isr22
    .quad isr23
    .quad isr24
    .quad isr25
    .quad isr26
    .quad isr27
    .quad isr28
    .quad isr29
    .quad isr30
    .quad isr31

.global irq_stub_table
irq_stub_table:
    .quad irq32
    .quad irq33
    .quad irq34
    .quad irq35
    .quad irq36
    .quad irq37
    .quad irq38
    .quad irq39
    .quad irq40
    .quad irq41
    .quad irq42
    .quad irq43
    .quad irq44
    .quad irq45
    .quad irq46
    .quad irq47
    .quad irq48
    .quad irq49
    .quad irq50
    .quad irq51
    .quad irq52
    .quad irq53
    .quad irq54
    .quad irq55
    .quad irq56
    .quad irq57
    .quad irq58
    .quad irq59
    .quad irq60
    .quad irq61
    .quad irq62
    .quad irq63
    .quad irq64
    .quad irq65
    .quad irq66
    .quad irq67
    .quad irq68
    .quad irq69
    .quad irq70
    .quad irq71
    .quad irq72
    .quad irq73
    .quad irq74
    .quad irq75
    .quad irq76
    .quad irq77
    .quad irq78
    .quad irq79

.section .note.GNU-stack,"",@progbits
