#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

enum effect_op {
    EFF_NOP   = 0,
    EFF_WRITE = 1,
    EFF_READ  = 2,
    EFF_EXIT  = 3,
    EFF_IOCTL = 4,
};

enum ioctl_cmd {
    IOCTL_CLEAR     = 1,
    IOCTL_GET_TICKS = 2,
    IOCTL_SHUTDOWN  = 3,
    IOCTL_REBOOT    = 4,
    IOCTL_GET_INFO = 10
};

#define EFFECT_PASS     0
#define EFFECT_HANDLED  1
#define EFFECT_ERROR   -1

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

struct effect {
    uint32_t op;
    uint32_t flags;
    uint64_t arg0;
    uint64_t arg1;
    uint64_t arg2;
    uint64_t arg3;
    int64_t  result;
    int32_t  error;
    uint32_t pad;
} __attribute__((packed));

typedef int (*effect_handler_t)(struct effect* e, void* user);

void effect_init(void);
void effect_init_default_handlers(void);
int  effect_register(effect_handler_t fn, void* user, const char* name);
void effect_perform(struct effect* e);
int  effect_core_handler(struct effect* e, void* user);

#ifdef __cplusplus
}
#endif