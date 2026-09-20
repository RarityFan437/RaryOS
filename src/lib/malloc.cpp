#include "malloc.h"
#include <stdint.h>

extern "C" void* sbrk(ptrdiff_t increment);

namespace {
    struct MemoryBlock {
        size_t size;
        bool is_free;
        MemoryBlock* next;

        void* data() {
            return reinterpret_cast<void*>(this + 1);
        }

        static MemoryBlock* from_data(void* data_ptr) {
            return reinterpret_cast<MemoryBlock*>(data_ptr) - 1;
        }

        void split(size_t requested_size) {
            constexpr size_t header_size = sizeof(MemoryBlock);
            if (size >= requested_size + header_size + 16) {
                uintptr_t next_addr = reinterpret_cast<uintptr_t>(this) + header_size + requested_size;
                MemoryBlock* next_block = reinterpret_cast<MemoryBlock*>(next_addr);

                next_block->size = size - requested_size - header_size;
                next_block->is_free = true;
                next_block->next = this->next;

                this->size = requested_size;
                this->next = next_block;
            }
        }

        void coalesce() {
            constexpr size_t header_size = sizeof(MemoryBlock);
            while (next && is_free && next->is_free) {
                size += header_size + next->size;
                next = next->next;
            }
        }
    };

    MemoryBlock* first_block = nullptr;
}

extern "C" void* malloc(size_t size) {
    if (size == 0) return nullptr;

    size = (size + 7) & ~static_cast<size_t>(7);

    MemoryBlock* current = first_block;
    MemoryBlock* last = nullptr;

    while (current != nullptr) {
        if (current->is_free && current->size >= size) {
            current->split(size);
            current->is_free = false;
            return current->data();
        }
        last = current;
        current = current->next;
    }


    constexpr size_t header_size = sizeof(MemoryBlock);
    size_t total_size = header_size + size;
    void* raw_mem = sbrk(total_size);

    if (raw_mem == reinterpret_cast<void*>(-1)) {
        return nullptr;
    }

    MemoryBlock* new_block = reinterpret_cast<MemoryBlock*>(raw_mem);
    new_block->size = size;
    new_block->is_free = false;
    new_block->next = nullptr;

    if (first_block == nullptr) {
        first_block = new_block;
    } else {
        last->next = new_block;
    }

    return new_block->data();
}

extern "C" void free(void* ptr) {
    if (ptr == nullptr) return;

    MemoryBlock* block = MemoryBlock::from_data(ptr);
    block->is_free = true;

    MemoryBlock* current = first_block;
    while (current != nullptr) {
        current->coalesce();
        current = current->next;
    }
}
