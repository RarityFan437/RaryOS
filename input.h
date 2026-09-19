#pragma once
#include <stddef.h>

#define INPUT_BUFFER_SIZE 256

void input_init(void);
void input_push(char c);
int  input_pop(void);
int  input_available(void);
void input_flush(void);