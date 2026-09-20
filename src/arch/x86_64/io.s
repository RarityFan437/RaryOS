.global outb
.global inb
.global outl
.global inl
.global io_wait

.section .text

outb:
    mov %di, %dx    # Порт должен быть в DX
    mov %sil, %al   # Значение в AL
    out %al, %dx
    ret

inb:
    mov %di, %dx    # Порт в DX
    xor %rax, %rax  # Очищаем возвращаемый регистр
    in %dx, %al     # Читаем байт в AL
    ret

outl:
    mov %di, %dx    # Порт в DX
    mov %esi, %eax  # Двойное слово в EAX
    out %eax, %dx
    ret

inl:
    mov %di, %dx    # Порт в DX
    in %dx, %eax    # Читаем двойное слово в EAX
    ret

io_wait:
    mov $0x80, %dx
    xor %al, %al
    out %al, %dx
    ret

.section .note.GNU-stack,"",@progbits
