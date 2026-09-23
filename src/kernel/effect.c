#include "effect.h"
#include "stdio.h"
#include "terminal.h"
#include "input.h"
#include "acpi.h"
#include "smbios.h"
#include "cpuinfo.h"
#include "fb.h"
#include <string.h>

#define MAX_HANDLERS 16

struct handler_entry {
    effect_handler_t fn;
    void*            user;
    const char*      name;
};

static struct handler_entry handlers[MAX_HANDLERS];
static int handler_count = 0;

extern volatile uint64_t pit_ticks;

void effect_init(void) {
    handler_count = 0;
    for (int i = 0; i < MAX_HANDLERS; i++) {
        handlers[i].fn   = 0;
        handlers[i].user = 0;
        handlers[i].name = 0;
    }
}

int effect_register(effect_handler_t fn, void* user, const char* name) {
    if (handler_count >= MAX_HANDLERS) return -1;
    handlers[handler_count].fn   = fn;
    handlers[handler_count].user = user;
    handlers[handler_count].name = name;
    handler_count++;
    return 0;
}

void effect_perform(struct effect* e) {
    e->result = 0;
    e->error  = 0;

    for (int i = 0; i < handler_count; i++) {
        int r = handlers[i].fn(e, handlers[i].user);
        if (r == EFFECT_HANDLED) return;
        if (r == EFFECT_ERROR)   return;
    }
    e->error = -38;
}

static int trace_handler(struct effect* e, void* user) {
    (void)user;
    return EFFECT_PASS;
}

static int sandbox_handler(struct effect* e, void* user) {
    (void)user; (void)e;
    return EFFECT_PASS;
}

int effect_core_handler(struct effect* e, void* user) {
    (void)user;
    switch (e->op) {
    case EFF_WRITE:
        terminal_write((const char*)e->arg0, (size_t)e->arg1);
        e->result = (int64_t)e->arg1;
        return EFFECT_HANDLED;

    case EFF_READ: {
        char* buf = (char*)e->arg0;
        size_t len = (size_t)e->arg1;
        while (!input_available()) {
            asm volatile("sti; hlt; cli");
        }
        size_t got = 0;
        while (got < len && input_available()) {
            buf[got++] = (char)input_pop();
        }
        e->result = (int64_t)got;
        return EFFECT_HANDLED;
    }

    case EFF_IOCTL: {
        uint64_t cmd = e->arg0;
        switch (cmd) {
        case IOCTL_CLEAR:
            clear_terminal();
            e->result = 0;
            return EFFECT_HANDLED;

        case IOCTL_GET_TICKS:
            e->result = (int64_t)pit_ticks;
            return EFFECT_HANDLED;

        case IOCTL_SHUTDOWN:
            acpi_shutdown();
            return EFFECT_HANDLED;

        case IOCTL_REBOOT:
            acpi_reboot();
            return EFFECT_HANDLED;
        case IOCTL_GET_INFO: 
            struct sys_info* info = (struct sys_info*)e->arg1;
            if (!info) { e->error = -14; return EFFECT_HANDLED; }

            const char* vendor = smbios_get_board_vendor();
            const char* board  = smbios_get_board_name();
            if (!vendor) vendor = "Unknown";
            if (!board)  board  = "Unknown";

            int pos = 0;
            while (vendor[pos] && pos < 30) { info->host[pos] = vendor[pos]; pos++; }
            info->host[pos++] = ' ';
            int i = 0;
            while (board[i] && pos < 63) { info->host[pos++] = board[i++]; }
            info->host[pos] = 0;

            cpu_get_brand(info->cpu, sizeof(info->cpu));
            if (info->cpu[0] == 0) {
                const char* u = "Unknown CPU";
                int j = 0;
                while (u[j]) { info->cpu[j] = u[j]; j++; }
                info->cpu[j] = 0;
            }

            const char* g = "UNKNOWN";
            i = 0;
            while (g[i]) { info->gpu[i] = g[i]; i++; }
            info->gpu[i] = 0;

            extern const char* version;
            i = 0;
            while (version[i] && i < 31) { info->kernel[i] = version[i]; i++; }
            info->kernel[i] = 0;

            info->res_w = fb_width();
            info->res_h = fb_height();
            info->uptime_ticks = pit_ticks;

            extern uint64_t g_ram_mb;
            info->ram_mb = g_ram_mb;

            e->result = 0;
            return EFFECT_HANDLED;
        }
        e->error = -22;
        return EFFECT_HANDLED;
    }

    case EFF_EXIT:
        for (;;) asm volatile("hlt");

    default:
        return EFFECT_PASS;
    }
}

void effect_init_default_handlers(void) {
    effect_register(trace_handler,       NULL, "trace");
    effect_register(sandbox_handler,     NULL, "sandbox");
    effect_register(effect_core_handler, NULL, "core");
}