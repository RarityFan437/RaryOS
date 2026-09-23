.global _start
.extern main

.section .text
_start:
    xor %rbp, %rbp
    xor %rdi, %rdi
    xor %rsi, %rsi
    call main

    mov %eax, %edi
    mov $3, %eax
    int $0x80

    ud2

.section .note.GNU-stack,"",@progbits
