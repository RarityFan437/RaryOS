#pragma once
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int strlen(const char s[]);
void reverse(char s[]);

void* memset(void* dst, int val, size_t n);
void* memcpy(void* dst, const void* src, size_t n);

#ifdef __cplusplus
}
#endif
