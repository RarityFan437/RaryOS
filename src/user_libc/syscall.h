#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EFF_WRITE 1
#define EFF_READ  2
#define EFF_EXIT  3
#define EFF_IOCTL 4

#define IOCTL_CLEAR     1
#define IOCTL_GET_TICKS 2
#define IOCTL_SHUTDOWN  3
#define IOCTL_REBOOT    4
#define IOCTL_GET_INFO  10

struct sys_info {
    char     host[64];
    char     cpu[64];
    char     gpu[64];
    char     kernel[32];
    uint32_t res_w;
    uint32_t res_h;
    uint32_t _pad;
    uint64_t uptime_ticks;
    uint64_t ram_mb;
};

long sys_write(const void* buf, size_t len);
long sys_read(void* buf, size_t len);
void sys_exit(int code) __attribute__((noreturn));
long sys_ioctl(long cmd, long arg0);
long sys_ioctl2(long cmd, long arg0, long arg1);

#ifdef __cplusplus
}
#endif