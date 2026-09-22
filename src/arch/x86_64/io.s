.global insw

.section .text

.global insw
insw:
    mov %edi, %edx    # порт в DX
    mov %rsi, %rdi    # буфер назначения в RDI (для insw)
    mov %rdx, %rcx  
    cld             
    rep insw       
    ret

.section .note.GNU-stack,"",@progbits
