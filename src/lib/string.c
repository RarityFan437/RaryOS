#include <stddef.h>

int strlen(const char s[])
{
    int i = 0;
    while (s[i] != '\0')
    {
        i++;
    }
    return i;
}

void reverse(char s[])
{
    int size = strlen(s);
    for (int i=0; i<size/2; ++i)
    {
        char temp = s[i];
        s[i] = s[size-i-1];
        s[size-i-1] = temp;
    }
}

void* memset(void* dst, int val, size_t n) {
    char* d = (char*)dst;
    for (size_t i = 0; i < n; i++) {
        d[i] = (char)val;
    }
    return dst;
}

void* memcpy(void* dst, const void* src, size_t n) {
    char* d = (char*)dst;
    const char* s = (const char*)src;
    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
    return dst;
}
