/* shell.c — примитивная командная оболочка.
 *
 * Логика простая и типичная для учебных ОС:
 *   1. напечатать приглашение с текущим путём, например "/home> "
 *   2. читать символы с клавиатуры, пока не нажат Enter,
 *      одновременно отображая их на экране (эхо) и складывая в буфер строки
 *   3. сравнить введённую строку с известными командами
 *   4. выполнить соответствующее действие
 *   5. повторить
 *
 * Это специально написано без malloc/динамической памяти — вся
 * ОС пока не имеет менеджера памяти, поэтому буфер строки — обычный
 * массив на стеке фиксированного размера. */

#include "shell.h"
#include "vga.h"
#include "keyboard.h"
#include "fs.h"
#include "calc.h"
#include <stddef.h>
#include <stdint.h>

#define CMD_BUFFER_SIZE 128
#define PWD_BUFFER_SIZE 128

/* Сравнение двух строк "вручную" — аналог strcmp из libc, но своя реализация,
 * т.к. мы собираем ядро без стандартной библиотеки C (-nostdlib/-ffreestanding) */
static int str_equals(const char* a, const char* b) {
    size_t i = 0;
    while (a[i] != '\0' && b[i] != '\0') {
        if (a[i] != b[i])
            return 0;
        i++;
    }
    return a[i] == b[i]; /* оба должны закончиться одновременно */
}

/* Проверка, начинается ли строка str с префикса prefix.
 * Нужна для команд с аргументами, например "echo привет мир" */
static int str_starts_with(const char* str, const char* prefix) {
    size_t i = 0;
    while (prefix[i] != '\0') {
        if (str[i] != prefix[i])
            return 0;
        i++;
    }
    return 1;
}

/* Разбить строку "имя остаток текста" на первое "слово" (до пробела)
 * и всё, что после него. Используется командой write: "write file.txt Privet mir"
 * -> name_out = "file.txt", rest_out указывает на "Privet mir".
 * name_out должен быть буфером размера хотя бы FS_NAME_MAX. Возвращает
 * 0, если аргументов вообще не было (пустая строка). */
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
    while (*rest == ' ') rest++; /* пропускаем пробелы между именем и остальным текстом */
    *rest_out = rest;
    return 1;
}

/* Читает одну строку ввода с клавиатуры в buffer (с эхом на экран
 * и поддержкой Backspace). Останавливается на Enter или при
 * заполнении буфера. Строка гарантированно завершается '\0'. */
static void read_line(char* buffer, size_t max_len) {
    size_t len = 0;

    for (;;) {
        char c = keyboard_getchar();

        if (c == KEY_ENTER) {
            terminal_putchar('\n');
            break;
        } else if (c == KEY_BACKSPACE) {
            if (len > 0) {
                len--;
                terminal_backspace();
            }
        } else if (len < max_len - 1) {
            buffer[len++] = c;
            terminal_putchar(c); /* эхо введённого символа на экран */
        }
        /* если буфер уже полон — лишние символы молча игнорируем */
    }

    buffer[len] = '\0';
}

/* Команда help — список доступных команд */
static void cmd_help(void) {
    terminal_writestring("Available commands:\n");
    terminal_writestring("  help              - this help text\n");
    terminal_writestring("  clear             - clear the screen\n");
    terminal_writestring("  echo <text>       - print text\n");
    terminal_writestring("  about             - info about the kernel\n");
    terminal_writestring("  reboot            - reboot the machine\n");
    terminal_writestring("  shutdown          - power off the machine\n");
    terminal_writestring("  calc <expr>       - calculator, e.g.: calc 2^8 + 3 * (4 - 1)\n");
    terminal_writestring("Files and directories (stored in RAM, lost on reboot):\n");
    terminal_writestring("  ls                - list files/dirs in current directory\n");
    terminal_writestring("  pwd               - print current path\n");
    terminal_writestring("  cd <dir>          - change directory (cd .. - up, cd / - to root)\n");
    terminal_writestring("  mkdir <name>      - create a directory\n");
    terminal_writestring("  touch <name>      - create an empty file\n");
    terminal_writestring("  write <file> <text> - write text to a file (creates it if missing)\n");
    terminal_writestring("  cat <file>        - show file contents\n");
    terminal_writestring("  rm <name>         - remove a file or an empty directory\n");
}

/* Команда reboot — классический трюк через контроллер клавиатуры 8042:
 * запись команды 0xFE в его порт вызывает аппаратный сброс процессора.
 * Это не "чистый" способ, но самый простой и работающий почти везде
 * (включая настоящее старое/эмулируемое железо). */
static void cmd_reboot(void) {
    terminal_writestring("Rebooting...\n");
    uint8_t good = 0x02;
    /* ждём, пока входной буфер контроллера клавиатуры освободится */
    while (good & 0x02) {
        __asm__ volatile ("inb $0x64, %0" : "=a"(good));
    }
    __asm__ volatile ("outb %0, $0x64" : : "a"((uint8_t)0xFE));
    for (;;) __asm__ volatile ("hlt"); /* если вдруг не сработало */
}

/* Команда shutdown — пытается выключить машину.
 * Работает в QEMU, Bochs и VirtualBox.
 * На реальном железе без поддержки ACPI просто останавливает процессор. */
static void cmd_shutdown(void) {
    terminal_writestring("Shutting down...\n");

    /* QEMU */
    __asm__ volatile ("outw %0, %1" : : "a"((uint16_t)0x2000), "Nd"((uint16_t)0x604));
    /* Bochs / старые версии QEMU */
    __asm__ volatile ("outw %0, %1" : : "a"((uint16_t)0x2000), "Nd"((uint16_t)0xB004));
    /* VirtualBox */
    __asm__ volatile ("outw %0, %1" : : "a"((uint16_t)0x3400), "Nd"((uint16_t)0x4004));

    /* Если ничего не сработало — останавливаем процессор */
    for (;;) {
        __asm__ volatile ("cli; hlt");
    }
}

/* Команда calc — парсит и вычисляет выражение через модуль calc.c */
static void cmd_calc(const char* expr) {
    if (expr[0] == '\0') {
        terminal_writestring("Usage: calc <expr>, e.g.: calc 2^8 + 3 * (4 - 1)\n");
        return;
    }

    int error = 0;
    long result = calc_eval(expr, &error);

    if (error) {
        terminal_writestring("Error in expression\n");
        return;
    }

    terminal_write_int((int32_t) result);
    terminal_putchar('\n');
}

/* Общий помощник: вывести код ошибки ФС в человекочитаемом виде */
static void print_fs_error(enum fs_result err) {
    terminal_writestring("Error: ");
    terminal_writestring(fs_strerror(err));
    terminal_putchar('\n');
}

static void cmd_pwd(void) {
    char path[PWD_BUFFER_SIZE];
    fs_pwd(path, sizeof(path));
    terminal_writestring(path);
    terminal_putchar('\n');
}

static void cmd_cd(const char* arg) {
    if (arg[0] == '\0') {
        terminal_writestring("Usage: cd <dir>\n");
        return;
    }
    enum fs_result r = fs_cd(arg);
    if (r != FS_OK)
        print_fs_error(r);
}

static void cmd_mkdir(const char* arg) {
    if (arg[0] == '\0') {
        terminal_writestring("Usage: mkdir <name>\n");
        return;
    }
    enum fs_result r = fs_mkdir(arg);
    if (r != FS_OK)
        print_fs_error(r);
}

static void cmd_touch(const char* arg) {
    if (arg[0] == '\0') {
        terminal_writestring("Usage: touch <name>\n");
        return;
    }
    enum fs_result r = fs_touch(arg);
    if (r != FS_OK)
        print_fs_error(r);
}

static void cmd_rm(const char* arg) {
    if (arg[0] == '\0') {
        terminal_writestring("Usage: rm <name>\n");
        return;
    }
    enum fs_result r = fs_rm(arg);
    if (r != FS_OK)
        print_fs_error(r);
}

static void cmd_cat(const char* arg) {
    if (arg[0] == '\0') {
        terminal_writestring("Usage: cat <file>\n");
        return;
    }
    const char* content;
    size_t len;
    enum fs_result r = fs_read(arg, &content, &len);
    if (r != FS_OK) {
        print_fs_error(r);
        return;
    }
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
    if (r != FS_OK)
        print_fs_error(r);
}

/* Разбор и выполнение одной введённой команды */
static void execute_command(const char* line) {
    if (line[0] == '\0') {
        return; /* пустая строка — ничего не делаем */
    } else if (str_equals(line, "help")) {
        cmd_help();
    } else if (str_equals(line, "clear")) {
        terminal_initialize();
    } else if (str_equals(line, "about")) {
        terminal_writestring("MyKernel -- a learning kernel in C, boots via GRUB (Multiboot)\n");
    } else if (str_equals(line, "reboot")) {
        cmd_reboot();
    } else if (str_equals(line, "shutdown")) {
        cmd_shutdown();
    } else if (str_starts_with(line, "echo ")) {
        terminal_writestring(line + 5);
        terminal_putchar('\n');
    } else if (str_equals(line, "echo")) {
        terminal_putchar('\n');
    } else if (str_starts_with(line, "calc ")) {
        cmd_calc(line + 5);
    } else if (str_equals(line, "calc")) {
        cmd_calc("");
    } else if (str_equals(line, "ls")) {
        fs_ls();
    } else if (str_equals(line, "pwd")) {
        cmd_pwd();
    } else if (str_starts_with(line, "cd ")) {
        cmd_cd(line + 3);
    } else if (str_equals(line, "cd")) {
        cmd_cd("");
    } else if (str_starts_with(line, "mkdir ")) {
        cmd_mkdir(line + 6);
    } else if (str_starts_with(line, "touch ")) {
        cmd_touch(line + 6);
    } else if (str_starts_with(line, "rm ")) {
        cmd_rm(line + 3);
    } else if (str_starts_with(line, "cat ")) {
        cmd_cat(line + 4);
    } else if (str_starts_with(line, "write ")) {
        cmd_write(line + 6);
    } else {
        terminal_writestring("Unknown command: ");
        terminal_writestring(line);
        terminal_writestring("\nType 'help' for the list of commands\n");
    }
}

void shell_run(void) {
    char line[CMD_BUFFER_SIZE];
    char path[PWD_BUFFER_SIZE];

    terminal_writestring("\nMyKernel shell. Type 'help' for the list of commands.\n");

    for (;;) {
        fs_pwd(path, sizeof(path));
        terminal_writestring(path);
        terminal_writestring("> ");
        read_line(line, CMD_BUFFER_SIZE);
        execute_command(line);
    }
}
