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

    mov $pml4, %edi
    xor %ecx, %ecx
1:
    movl $0, (%edi, %ecx, 8)
    movl $0, 4(%edi, %ecx, 8)
    inc %ecx
    cmp $512, %ecx
    jne 1b

    mov $pdpt, %edi
    xor %ecx, %ecx
2:
    movl $0, (%edi, %ecx, 8)
    movl $0, 4(%edi, %ecx, 8)
    inc %ecx
    cmp $512, %ecx
    jne 2b

    mov $pd, %edi
    xor %ecx, %ecx
    xor %edx, %edx
3:
    mov %edx, %eax
    or $0x83, %eax
    mov %eax, (%edi, %ecx, 8)
    movl $0, 4(%edi, %ecx, 8)
    add $0x200000, %edx
    inc %ecx
    cmp $2048, %ecx
    jne 3b

    mov $pdpt, %edi
    mov $pd, %eax
    or $0x03, %eax
    mov %eax, 0(%edi)
    movl $0, 4(%edi)
    add $0x1000, %eax
    mov %eax, 8(%edi)
    movl $0, 12(%edi)
    add $0x1000, %eax
    mov %eax, 16(%edi)
    movl $0, 20(%edi)
    add $0x1000, %eax
    mov %eax, 24(%edi)
    movl $0, 28(%edi)

    mov $pml4, %edi
    mov $pdpt, %eax
    or $0x03, %eax
    mov %eax, 0(%edi)
    movl $0, 4(%edi)

    mov $tss64, %edi
    xor %ecx, %ecx
4:
    movl $0, (%edi, %ecx, 4)
    inc %ecx
    cmp $26, %ecx
    jne 4b

    mov $stack_kernel_top, %eax
    mov %eax, 4(%edi)
    movl $0, 8(%edi)

    movw $104, 102(%edi)

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
