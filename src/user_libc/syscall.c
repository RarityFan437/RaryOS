#include "syscall.h"

static inline long _syscall3(long n, long a0, long a1, long a2) {
    long ret;
    asm volatile(
        "int $0x80"
        : "=a"(ret)
        : "a"(n), "D"(a0), "S"(a1), "d"(a2)
        : "memory"
    );
    return ret;
}

long sys_write(const void* buf, size_t len) {
    return _syscall3(EFF_WRITE, (long)buf, (long)len, 0);
}

long sys_read(void* buf, size_t len) {
    return _syscall3(EFF_READ, (long)buf, (long)len, 0);
}

void sys_exit(int code) {
    _syscall3(EFF_EXIT, (long)code, 0, 0);
    __builtin_unreachable();
}

long sys_ioctl(long cmd, long arg) {
    return _syscall3(EFF_IOCTL, cmd, arg, 0);
}

long sys_ioctl2(long cmd, long arg0, long arg1) {
    return _syscall3(EFF_IOCTL, cmd, arg0, arg1);
}
