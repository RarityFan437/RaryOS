.section .bss
.align 4096
.global pml4
pml4:
.skip 4096

.align 4096
.global pdpt
pdpt:
.skip 4096

.align 4096
.global pd
pd:
.skip 16384

.align 16
stack_kernel:
.skip 32768
.global stack_kernel_top
stack_kernel_top:

.align 16
.global tss64
tss64:
.skip 104

.section .data
.align 8
.global gdt64
gdt64:
    .quad 0x0000000000000000
    .quad 0x00209A0000000000
    .quad 0x0000920000000000
    .quad 0
    .quad 0

.align 8
.global gdt64_ptr
gdt64_ptr:
    .word 39
    .long gdt64

.section .text
.global boot_main
.code32

boot_main:
    push %ebp
    mov %esp, %ebp

    # Цикл 1: Очистка таблицы PML4 (512 записей по 8 байт)
    mov $pml4, %edi
    xor %ecx, %ecx
1:
    movl $0, (%edi, %ecx, 8)
    movl $0, 4(%edi, %ecx, 8)
    inc %ecx
    cmp $512, %ecx
    jne 1b

    # Цикл 2: Очистка таблицы PDPT (512 записей по 8 байт)
    mov $pdpt, %edi
    xor %ecx, %ecx
2:
    movl $0, (%edi, %ecx, 8)
    movl $0, 4(%edi, %ecx, 8)
    inc %ecx
    cmp $512, %ecx
    jne 2b

    # Цикл 3: Тождественное отображение 4 ГБ памяти страницами по 2 МБ (2048 записей)
    mov $pd, %edi
    xor %ecx, %ecx
    xor %edx, %edx
3:
    mov %edx, %eax
    
    # Проверяем, находится ли адрес в зоне MMIO (>= 3 ГБ / 0xC0000000)
    cmp $0xC0000000, %edx
    jb .Lnormal_ram
    
    # Для MMIO регионов (PCI, APIC, USB xHCI) включаем флаг Cache Disable (бит 4)
    # Флаги: Present(0x1) | Writable(0x2) | Cache Disable(0x10) | Page Size 2MB(0x80) = 0x93
    or $0x93, %eax          
    jmp .Lwrite_entry
    
.Lnormal_ram:
    # Для обычной оперативной памяти стандартные флаги
    # Flags: Present(0x1) | Writable(0x2) | Page Size 2MB(0x80) = 0x83
    or $0x83, %eax          
    
.Lwrite_entry:
    mov %eax, (%edi, %ecx, 8)
    movl $0, 4(%edi, %ecx, 8)
    add $0x200000, %edx     # Шаг вперед на 2 МБ физического пространства
    inc %ecx
    cmp $2048, %ecx         # Повторяем для всех 4 таблиц PD (4 * 512 = 2048)
    jne 3b

    # Подключаем созданные таблицы PD к корневым слотам таблицы PDPT
    mov $pdpt, %edi
    mov $pd, %eax
    or $0x03, %eax          # Флаги для таблиц: Present | Writable
    mov %eax, 0(%edi)       # Слот 0: покрывает виртуальные адреса 0 - 1 ГБ
    add $0x1000, %eax
    mov %eax, 8(%edi)       # Слот 1: покрывает виртуальные адреса 1 - 2 ГБ
    add $0x1000, %eax
    mov %eax, 16(%edi)      # Слот 2: покрывает виртуальные адреса 2 - 3 ГБ
    add $0x1000, %eax
    mov %eax, 24(%edi)      # Слот 3: покрывает виртуальные адреса 3 - 4 ГБ (сюда входит USB xHCI)

    # Подключаем PDPT к первому слоту корневой PML4
    mov $pml4, %edi
    mov $pdpt, %eax
    or $0x03, %eax
    mov %eax, 0(%edi)

    # Инициализация сегмента состояния задачи (TSS64) нулями
    mov $tss64, %edi
    xor %ecx, %ecx
4:
    movl $0, (%edi, %ecx, 4)
    inc %ecx
    cmp $26, %ecx
    jne 4b

    # Запись указателя на стек ядра в структуру TSS
    mov $stack_kernel_top, %eax
    mov %eax, 4(%edi)
    movl $0, 8(%edi)

    movw $104, 102(%edi)

    # Настройка дескриптора TSS внутри таблицы GDT64
    mov $gdt64, %edi
    add $24, %edi
    mov $tss64, %ebx

    movw $103, 0(%edi)
    mov %ebx, %eax
    mov %ax, 2(%edi)
    shr $16, %eax
    mov %al, 4(%edi)
    movb $0x89, 5(%edi)
    movb $0, 6(%edi)
    mov %ebx, %eax
    shr $24, %eax
    mov %al, 7(%edi)
    movl $0, 8(%edi)
    movl $0, 12(%edi)

    pop %ebp
    ret

.section .note.GNU-stack,"",@progbits
