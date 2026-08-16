/* shell.c — оболочка: история, редактирование строки, скрипты (run). */

#include "shell.h"
#include "vga.h"
#include "keyboard.h"
#include "fs.h"
#include "calc.h"
#include <stddef.h>
#include <stdint.h>

#define CMD_BUFFER_SIZE 128
#define PWD_BUFFER_SIZE 128
#define HISTORY_SIZE    16
#define RUN_LINE_MAX    128

static char history[HISTORY_SIZE][CMD_BUFFER_SIZE];
static int history_count = 0;
static int history_pos = 0;

/* вперёд: execute_command вызывается из run */
static void execute_command(const char* line);

static void history_add(const char* line) {
    if (line[0] == '\0')
        return;

    for (int i = HISTORY_SIZE - 1; i > 0; i--) {
        size_t j = 0;
        while (history[i - 1][j] && j < CMD_BUFFER_SIZE - 1) {
            history[i][j] = history[i - 1][j];
            j++;
        }
        history[i][j] = '\0';
    }

    size_t j = 0;
    while (line[j] && j < CMD_BUFFER_SIZE - 1) {
        history[0][j] = line[j];
        j++;
    }
    history[0][j] = '\0';

    if (history_count < HISTORY_SIZE)
        history_count++;
    history_pos = -1;
}

static int str_equals(const char* a, const char* b) {
    size_t i = 0;
    while (a[i] != '\0' && b[i] != '\0') {
        if (a[i] != b[i]) return 0;
        i++;
    }
    return a[i] == b[i];
}

static int str_starts_with(const char* str, const char* prefix) {
    size_t i = 0;
    while (prefix[i] != '\0') {
        if (str[i] != prefix[i]) return 0;
        i++;
    }
    return 1;
}

static void str_copy(char* dst, const char* src, size_t max) {
    size_t i = 0;
    while (src[i] && i < max - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static size_t str_len(const char* s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

static int split_name_and_rest(const char* args, char* name_out, size_t name_out_size, const char** rest_out) {
    size_t i = 0;
    while (args[i] != '\0' && args[i] != ' ' && i < name_out_size - 1) {
        name_out[i] = args[i];
        i++;
    }
    name_out[i] = '\0';
    if (i == 0) {
        *rest_out = "";
        return 0;
    }
    const char* rest = args + i;
    while (*rest == ' ') rest++;
    *rest_out = rest;
    return 1;
}

static int split_two_names(const char* args, char* a, size_t asz, char* b, size_t bsz) {
    const char* rest;
    if (!split_name_and_rest(args, a, asz, &rest))
        return 0;
    size_t i = 0;
    while (rest[i] && rest[i] != ' ' && i < bsz - 1) {
        b[i] = rest[i];
        i++;
    }
    b[i] = '\0';
    return i > 0;
}

static void read_line(char* buffer, size_t max_len) {
    size_t len = 0;
    size_t cursor = 0;
    size_t prompt_x, prompt_y;

    terminal_get_cursor(&prompt_x, &prompt_y);
    buffer[0] = '\0';
    history_pos = -1;

    for (;;) {
        char c = keyboard_getchar();

        if (c == KEY_ENTER) {
            terminal_putchar('\n');
            buffer[len] = '\0';
            break;
        }

        if (c == KEY_BACKSPACE) {
            if (cursor > 0) {
                for (size_t i = cursor - 1; i < len - 1; i++)
                    buffer[i] = buffer[i + 1];
                len--;
                cursor--;
                buffer[len] = '\0';
                size_t y;
                terminal_get_cursor(NULL, &y);
                terminal_set_cursor(prompt_x + cursor, y);
                for (size_t i = cursor; i < len; i++)
                    terminal_putchar(buffer[i]);
                terminal_putchar(' ');
                terminal_set_cursor(prompt_x + cursor, y);
            }
            continue;
        }

        if (c == KEY_LEFT) {
            if (cursor > 0) { cursor--; terminal_move_left(1); }
            continue;
        }
        if (c == KEY_RIGHT) {
            if (cursor < len) { cursor++; terminal_move_right(1); }
            continue;
        }

        if (c == KEY_UP) {
            if (history_count == 0) continue;
            if (history_pos < history_count - 1) history_pos++;
            size_t y;
            terminal_get_cursor(NULL, &y);
            terminal_set_cursor(prompt_x, y);
            for (size_t i = 0; i < len; i++) terminal_putchar(' ');
            terminal_set_cursor(prompt_x, y);
            str_copy(buffer, history[history_pos], max_len);
            len = str_len(buffer);
            cursor = len;
            terminal_writestring(buffer);
            continue;
        }

        if (c == KEY_DOWN) {
            size_t y;
            terminal_get_cursor(NULL, &y);
            terminal_set_cursor(prompt_x, y);
            for (size_t i = 0; i < len; i++) terminal_putchar(' ');
            terminal_set_cursor(prompt_x, y);
            if (history_pos > 0) {
                history_pos--;
                str_copy(buffer, history[history_pos], max_len);
                len = str_len(buffer);
                cursor = len;
                terminal_writestring(buffer);
            } else {
                history_pos = -1;
                buffer[0] = '\0';
                len = 0;
                cursor = 0;
            }
            continue;
        }

        if (c >= 32 && c < 127 && len < max_len - 1) {
            for (size_t i = len; i > cursor; i--)
                buffer[i] = buffer[i - 1];
            buffer[cursor] = c;
            len++;
            cursor++;
            buffer[len] = '\0';
            size_t y;
            terminal_get_cursor(NULL, &y);
            terminal_set_cursor(prompt_x + cursor - 1, y);
            for (size_t i = cursor - 1; i < len; i++)
                terminal_putchar(buffer[i]);
            terminal_set_cursor(prompt_x + cursor, y);
        }
    }
}

static void print_fs_error(enum fs_result err) {
    terminal_writestring("Error: ");
    terminal_writestring(fs_strerror(err));
    terminal_putchar('\n');
}

static void cmd_help(void) {
    terminal_writestring("System:\n");
    terminal_writestring("  help, clear, echo, about, uname, whoami, free\n");
    terminal_writestring("  reboot, shutdown, history, calc <expr>\n");
    terminal_writestring("Files (RAM only, lost on reboot):\n");
    terminal_writestring("  ls, pwd, cd, mkdir, touch, cat, rm\n");
    terminal_writestring("  write <f> <text>   - overwrite file\n");
    terminal_writestring("  append <f> <text>  - append line to file\n");
    terminal_writestring("  cp <src> <dst>     - copy file\n");
    terminal_writestring("  mv <src> <dst>     - rename file/dir\n");
    terminal_writestring("Programs:\n");
    terminal_writestring("  run <file>         - run script (each line = shell cmd)\n");
    terminal_writestring("Line editing: Left/Right, Up/Down = history\n");
}

static void cmd_reboot(void) {
    terminal_writestring("Rebooting...\n");
    uint8_t good = 0x02;
    while (good & 0x02)
        __asm__ volatile ("inb $0x64, %0" : "=a"(good));
    __asm__ volatile ("outb %0, $0x64" : : "a"((uint8_t)0xFE));
    for (;;) __asm__ volatile ("hlt");
}

static void cmd_shutdown(void) {
    terminal_writestring("Shutting down...\n");
    __asm__ volatile ("outw %0, %1" : : "a"((uint16_t)0x2000), "Nd"((uint16_t)0x604));
    __asm__ volatile ("outw %0, %1" : : "a"((uint16_t)0x2000), "Nd"((uint16_t)0xB004));
    __asm__ volatile ("outw %0, %1" : : "a"((uint16_t)0x3400), "Nd"((uint16_t)0x4004));
    for (;;) __asm__ volatile ("cli; hlt");
}

static void cmd_calc(const char* expr) {
    if (expr[0] == '\0') {
        terminal_writestring("Usage: calc <expr>\n");
        return;
    }
    int error = 0;
    long result = calc_eval(expr, &error);
    if (error) {
        terminal_writestring("Error in expression\n");
        return;
    }
    terminal_write_int((int32_t)result);
    terminal_putchar('\n');
}

static void cmd_uname(void) {
    terminal_writestring("MyKernel i686 GNU/MyKernel\n");
}

static void cmd_whoami(void) {
    terminal_writestring("root\n");
}

static void cmd_free(void) {
    terminal_writestring("RAM FS nodes used: ");
    terminal_write_uint((uint32_t)fs_node_count());
    terminal_writestring(" / 256\n");
    terminal_writestring("(no virtual memory yet — all data in kernel RAM)\n");
}

static void cmd_history_list(void) {
    if (history_count == 0) {
        terminal_writestring("(no history)\n");
        return;
    }
    for (int i = history_count - 1; i >= 0; i--) {
        terminal_write_uint((uint32_t)(history_count - i));
        terminal_writestring("  ");
        terminal_writestring(history[i]);
        terminal_putchar('\n');
    }
}

static void cmd_pwd(void) {
    char path[PWD_BUFFER_SIZE];
    fs_pwd(path, sizeof(path));
    terminal_writestring(path);
    terminal_putchar('\n');
}

static void cmd_cd(const char* arg) {
    if (arg[0] == '\0') { terminal_writestring("Usage: cd <dir>\n"); return; }
    enum fs_result r = fs_cd(arg);
    if (r != FS_OK) print_fs_error(r);
}

static void cmd_mkdir(const char* arg) {
    if (arg[0] == '\0') { terminal_writestring("Usage: mkdir <name>\n"); return; }
    enum fs_result r = fs_mkdir(arg);
    if (r != FS_OK) print_fs_error(r);
}

static void cmd_touch(const char* arg) {
    if (arg[0] == '\0') { terminal_writestring("Usage: touch <name>\n"); return; }
    enum fs_result r = fs_touch(arg);
    if (r != FS_OK) print_fs_error(r);
}

static void cmd_rm(const char* arg) {
    if (arg[0] == '\0') { terminal_writestring("Usage: rm <name>\n"); return; }
    enum fs_result r = fs_rm(arg);
    if (r != FS_OK) print_fs_error(r);
}

static void cmd_cat(const char* arg) {
    if (arg[0] == '\0') { terminal_writestring("Usage: cat <file>\n"); return; }
    const char* content;
    size_t len;
    enum fs_result r = fs_read(arg, &content, &len);
    if (r != FS_OK) { print_fs_error(r); return; }
    terminal_writestring(content);
    terminal_putchar('\n');
}

static void cmd_write(const char* args) {
    char name[FS_NAME_MAX];
    const char* text;
    if (!split_name_and_rest(args, name, sizeof(name), &text)) {
        terminal_writestring("Usage: write <file> <text>\n");
        return;
    }
    enum fs_result r = fs_write(name, text);
    if (r != FS_OK) print_fs_error(r);
}

static void cmd_append(const char* args) {
    char name[FS_NAME_MAX];
    const char* text;
    if (!split_name_and_rest(args, name, sizeof(name), &text)) {
        terminal_writestring("Usage: append <file> <text>\n");
        return;
    }
    enum fs_result r = fs_append(name, text);
    if (r != FS_OK) print_fs_error(r);
}

static void cmd_cp(const char* args) {
    char src[FS_NAME_MAX], dst[FS_NAME_MAX];
    if (!split_two_names(args, src, sizeof(src), dst, sizeof(dst))) {
        terminal_writestring("Usage: cp <src> <dst>\n");
        return;
    }
    enum fs_result r = fs_cp(src, dst);
    if (r != FS_OK) print_fs_error(r);
}

static void cmd_mv(const char* args) {
    char src[FS_NAME_MAX], dst[FS_NAME_MAX];
    if (!split_two_names(args, src, sizeof(src), dst, sizeof(dst))) {
        terminal_writestring("Usage: mv <src> <dst>\n");
        return;
    }
    enum fs_result r = fs_mv(src, dst);
    if (r != FS_OK) print_fs_error(r);
}

/* Запуск скрипта: каждая строка файла — команда shell (как bash script.sh).
 * Строки с # в начале — комментарии. Пустые строки пропускаются. */
static void cmd_run(const char* arg) {
    if (arg[0] == '\0') {
        terminal_writestring("Usage: run <file>\n");
        terminal_writestring("Example:\n");
        terminal_writestring("  write myscript.sh echo Hello\n");
        terminal_writestring("  append myscript.sh calc 2^10\n");
        terminal_writestring("  run myscript.sh\n");
        return;
    }

    const char* content;
    size_t len;
    enum fs_result r = fs_read(arg, &content, &len);
    if (r != FS_OK) {
        print_fs_error(r);
        return;
    }

    char line[RUN_LINE_MAX];
    size_t i = 0;
    while (i < len) {
        size_t j = 0;
        while (i < len && content[i] != '\n' && j < RUN_LINE_MAX - 1)
            line[j++] = content[i++];
        line[j] = '\0';
        if (i < len && content[i] == '\n') i++;

        /* trim leading spaces */
        const char* p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0' || *p == '#')
            continue;

        terminal_writestring("+ ");
        terminal_writestring(p);
        terminal_putchar('\n');
        execute_command(p);
    }
}

static void execute_command(const char* line) {
    if (line[0] == '\0') return;
    else if (str_equals(line, "help")) cmd_help();
    else if (str_equals(line, "clear")) terminal_initialize();
    else if (str_equals(line, "about"))
        terminal_writestring("MyKernel -- learning OS, Multiboot/GRUB, RAM FS, scripts via run\n");
    else if (str_equals(line, "reboot")) cmd_reboot();
    else if (str_equals(line, "shutdown")) cmd_shutdown();
    else if (str_equals(line, "uname")) cmd_uname();
    else if (str_equals(line, "whoami")) cmd_whoami();
    else if (str_equals(line, "free")) cmd_free();
    else if (str_equals(line, "history")) cmd_history_list();
    else if (str_starts_with(line, "echo ")) {
        terminal_writestring(line + 5);
        terminal_putchar('\n');
    } else if (str_equals(line, "echo")) terminal_putchar('\n');
    else if (str_starts_with(line, "calc ")) cmd_calc(line + 5);
    else if (str_equals(line, "calc")) cmd_calc("");
    else if (str_equals(line, "ls")) fs_ls();
    else if (str_equals(line, "pwd")) cmd_pwd();
    else if (str_starts_with(line, "cd ")) cmd_cd(line + 3);
    else if (str_equals(line, "cd")) cmd_cd("");
    else if (str_starts_with(line, "mkdir ")) cmd_mkdir(line + 6);
    else if (str_starts_with(line, "touch ")) cmd_touch(line + 6);
    else if (str_starts_with(line, "rm ")) cmd_rm(line + 3);
    else if (str_starts_with(line, "cat ")) cmd_cat(line + 4);
    else if (str_starts_with(line, "write ")) cmd_write(line + 6);
    else if (str_starts_with(line, "append ")) cmd_append(line + 7);
    else if (str_starts_with(line, "cp ")) cmd_cp(line + 3);
    else if (str_starts_with(line, "mv ")) cmd_mv(line + 3);
    else if (str_starts_with(line, "run ")) cmd_run(line + 4);
    else if (str_equals(line, "run")) cmd_run("");
    else {
        terminal_writestring("Unknown command: ");
        terminal_writestring(line);
        terminal_writestring("\nType 'help'\n");
    }
}

void shell_run(void) {
    char line[CMD_BUFFER_SIZE];
    char path[PWD_BUFFER_SIZE];

    terminal_writestring("\nMyKernel shell. Type 'help'. Scripts: write/append + run\n");

    for (;;) {
        fs_pwd(path, sizeof(path));
        terminal_writestring(path);
        terminal_writestring("> ");
        read_line(line, CMD_BUFFER_SIZE);
        history_add(line);
        execute_command(line);
    }
}
