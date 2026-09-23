#include "stdio.h"
#include "string.h"
#include "syscall.h"

#define LINE_MAX 128
#define ARGV_MAX 16

static char line[LINE_MAX];

static void print_info(void) {
    struct sys_info si;
    if (get_sys_info(&si) != 0) {
        printf("Failed to get system info\n");
        return;
    }

    unsigned long total_sec = si.uptime_ticks / 100;
    unsigned long mins = total_sec / 60;
    unsigned long secs = total_sec % 60;

    printf("OS:         RaryOS\n");
    printf("Host:       %s\n", si.host);
    printf("Kernel:     RaryOS v%s\n", si.kernel);
    printf("Uptime:     %lu min %lu sec\n", mins, secs);
    printf("Shell:      RaryTerminal v1.0\n");
    printf("Resolution: %ux%u\n", si.res_w, si.res_h);
    printf("CPU:        %s\n", si.cpu);
    printf("GPU:        %s\n", si.gpu);
    printf("RAM:        %lu MB\n", (unsigned long)si.ram_mb);
    printf("\n");
}

static int readline(char* buf, int max) {
    int i = 0;
    for (;;) {
        int c = getchar();
        if (c < 0) continue;
        if (c == '\r') continue;
        if (c == '\n') {
            putchar('\n');
            buf[i] = 0;
            return i;
        }
        if (c == '\b' || c == 127) {
            if (i > 0) {
                i--;
                putchar('\b');
                putchar(' ');
                putchar('\b');
            }
            continue;
        }
        if (i < max - 1) {
            buf[i++] = (char)c;
            putchar((char)c);
        }
    }
}

static int tokenize(char* buf, char** argv, int max) {
    int argc = 0;
    char* p = buf;
    while (*p) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        if (argc >= max - 1) break;
        argv[argc++] = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        if (*p) { *p = 0; p++; }
    }
    argv[argc] = 0;
    return argc;
}

static void cmd_help(int argc, char** argv);
static void cmd_clear(int argc, char** argv);
static void cmd_echo(int argc, char** argv);
static void cmd_info(int argc, char** argv);
static void cmd_shutdown(int argc, char** argv);
static void cmd_reboot(int argc, char** argv);

struct cmd {
    const char* name;
    const char* help;
    void (*fn)(int, char**);
};

static struct cmd cmds[] = {
    { "help",     "show this help",         cmd_help },
    { "clear",    "clear the screen",       cmd_clear },
    { "echo",     "print arguments",        cmd_echo },
    { "info",     "show system information", cmd_info },
    { "shutdown", "power off the machine",  cmd_shutdown },
    { "reboot",   "restart the machine",    cmd_reboot },
    { 0, 0, 0 }
};

static void cmd_help(int argc, char** argv) {
    (void)argc; (void)argv;
    printf("Commands:\n");
    for (struct cmd* c = cmds; c->name; c++)
        printf("  %s - %s\n", c->name, c->help);
}

static void cmd_clear(int argc, char** argv) {
    (void)argc; (void)argv;
    clear_screen();
}

static void cmd_echo(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        if (i > 1) putchar(' ');
        sys_write(argv[i], strlen(argv[i]));
    }
    putchar('\n');
}

static void cmd_info(int argc, char** argv) {
    (void)argc; (void)argv;
    print_info();
}

static void cmd_shutdown(int argc, char** argv) {
    (void)argc; (void)argv;
    printf("Shutting down...\n");
    shutdown();
}

static void cmd_reboot(int argc, char** argv) {
    (void)argc; (void)argv;
    printf("Rebooting...\n");
    reboot();
}

int main(void) {
    clear_screen();
    print_info();

    for (;;) {
        printf("> ");

        int n = readline(line, LINE_MAX);
        if (n == 0) continue;

        char* argv[ARGV_MAX];
        int argc = tokenize(line, argv, ARGV_MAX);
        if (argc == 0) continue;

        int found = 0;
        for (struct cmd* c = cmds; c->name; c++) {
            if (strcmp(c->name, argv[0]) == 0) {
                c->fn(argc, argv);
                found = 1;
                break;
            }
        }
        if (!found) printf("Unknown command: %s\n", argv[0]);
    }
}
