#include "shell.h"
#include "stdio.h"
#include "string.h"
#include "syscall.h"

#define LINE_MAX 256

static char line[LINE_MAX];
static int  line_len = 0;

typedef void (*cmd_fn)(int argc, char** argv);

struct cmd {
    const char* name;
    const char* help;
    cmd_fn      fn;
};

static int strcmp_(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
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
        if (*p) { *p = '\0'; p++; }
    }
    argv[argc] = 0;
    return argc;
}

static void cmd_help(int argc, char** argv);
static void cmd_clear(int argc, char** argv);
static void cmd_echo(int argc, char** argv);
static void cmd_shutdown(int argc, char** argv);
static void cmd_reboot(int argc, char** argv);
static void cmd_version(int argc, char** argv);
static void cmd_ticks(int argc, char** argv);

static struct cmd cmds[] = {
    { "help",     "show this help",         cmd_help },
    { "clear",    "clear the screen",       cmd_clear },
    { "echo",     "print arguments",        cmd_echo },
    { "shutdown", "power off the machine",  cmd_shutdown },
    { "reboot",   "restart the machine",    cmd_reboot },
    { "version",  "print kernel version",   cmd_version },
    { "ticks",    "print PIT/LAPIC ticks",  cmd_ticks },
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
    sys_clear();
}

static void cmd_echo(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        if (i > 1) sys_putchar(' ');
        printf("%s", argv[i]);
    }
    sys_putchar('\n');
}

static void cmd_shutdown(int argc, char** argv) {
    (void)argc; (void)argv;
    printf("Shutting down...\n");
    sys_shutdown();
}

static void cmd_reboot(int argc, char** argv) {
    (void)argc; (void)argv;
    printf("Rebooting...\n");
    sys_reboot();
}

static void cmd_version(int argc, char** argv) {
    (void)argc; (void)argv;
    printf("RaryOS 0.1.22\n");
}

static void cmd_ticks(int argc, char** argv) {
    (void)argc; (void)argv;
    printf("PIT ticks: %lu  LAPIC ticks: %lu\n",
           (unsigned long)sys_ticks(),
           (unsigned long)sys_lapic_ticks());
}

static void execute_line(void) {
    line[line_len] = '\0';

    char* argv[16];
    int argc = tokenize(line, argv, 16);
    if (argc == 0) return;

    for (struct cmd* c = cmds; c->name; c++) {
        if (strcmp_(c->name, argv[0]) == 0) {
            c->fn(argc, argv);
            return;
        }
    }
    printf("Unknown command: %s\n", argv[0]);
}

void shell_init(void) {
    line_len = 0;
}

void shell_handle_char(char c) {
    if (c == '\n') {
        sys_putchar('\n');
        execute_line();
        line_len = 0;
        printf("> ");
        return;
    }
    if (c == '\b') {
        if (line_len > 0) {
            line_len--;
            sys_putchar('\b');
        }
        return;
    }
    if ((unsigned char)c < 32) return;
    if (line_len >= LINE_MAX - 1) return;

    line[line_len++] = c;
    sys_putchar(c);
}

void shell_run(void) {
    shell_init();
    printf("> ");
    for (;;) {
        sys_halt();
        while (sys_input_available()) {
            int c = sys_read_char();
            if (c < 0) break;
            shell_handle_char((char)c);
        }
    }
}