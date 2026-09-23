#include "elf.h"
#include "stdio.h"

int elf_load(const void* elf_data, uint64_t* out_entry) {
    const elf64_header_t* hdr = (const elf64_header_t*)elf_data;

    if (hdr->e_ident[0] != 0x7F ||
        hdr->e_ident[1] != 'E'  ||
        hdr->e_ident[2] != 'L'  ||
        hdr->e_ident[3] != 'F') {
        printf("ELF: bad magic\n");
        return -1;
    }

    if (hdr->e_machine != EM_X86_64) {
        printf("ELF: not x86-64 (machine=%x)\n", hdr->e_machine);
        return -1;
    }

    const uint8_t* base = (const uint8_t*)elf_data;

    for (int i = 0; i < hdr->e_phnum; i++) {
        const elf64_phdr_t* ph = (const elf64_phdr_t*)
            (base + hdr->e_phoff + i * hdr->e_phentsize);

        if (ph->p_type != PT_LOAD) continue;

        uint8_t* dst       = (uint8_t*)ph->p_vaddr;
        const uint8_t* src = base + ph->p_offset;

        for (uint64_t j = 0; j < ph->p_filesz; j++) dst[j] = src[j];
        for (uint64_t j = ph->p_filesz; j < ph->p_memsz; j++) dst[j] = 0;

        printf("ELF: segment vaddr=%lx  memsz=%lx  flags=%x\n",
               (unsigned long)ph->p_vaddr,
               (unsigned long)ph->p_memsz,
               ph->p_flags);
    }

    *out_entry = hdr->e_entry;
    return 0;
}
