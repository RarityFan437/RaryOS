#include <stddef.h>

typedef struct BlockHeader 
{
    size_t size;           
    int is_free;         
    struct BlockHeader* next; 
} BlockHeader;

#define HEADER_SIZE sizeof(BlockHeader)

extern void* sbrk(ptrdiff_t increment);

static BlockHeader* first_block = NULL;

void* malloc(size_t size)
{
    if (size == 0) return NULL;
    size = (size + 7) & 7;

    BlockHeader* current = first_block;
    BlockHeader* last = NULL;

    while (current != NULL) {
        if (current->is_free && current->size >= size) {
            current->is_free = 0;
            return (void*)(current + 1);
        }
        last = current;
        current = current->next;
    }

    size_t total_size = HEADER_SIZE + size;
    BlockHeader* block = (BlockHeader*)sbrk(total_size);

    if (block == (void*)-1) {
        return NULL; 
    }

    block->size = size;
    block->is_free = 0;
    block->next = NULL;

    if (first_block == NULL) {
        first_block = block; 
    } else {
        last->next = block;
    }

    return (void*)(block + 1);
}

void free(void* ptr) {
    if (ptr == NULL) return;

    BlockHeader* block = ((BlockHeader*)ptr) - 1;
    
    block->is_free = 1;
}