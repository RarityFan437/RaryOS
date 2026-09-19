#include "input.h"
#include <stdint.h>

static volatile char     buffer[INPUT_BUFFER_SIZE];
static volatile uint32_t head = 0;
static volatile uint32_t tail = 0;

void input_init(void) {
    head = 0;
    tail = 0;
}

void input_push(char c) {
    uint32_t next = (head + 1) % INPUT_BUFFER_SIZE;
    if (next == tail) {
        return;
    }
    buffer[head] = c;
    head = next;
}

int input_pop(void) {
    if (head == tail) return -1;
    char c = buffer[tail];
    tail = (tail + 1) % INPUT_BUFFER_SIZE;
    return (int)(unsigned char)c;
}

int input_available(void) {
    return head != tail;
}

void input_flush(void) {
    head = 0;
    tail = 0;
}