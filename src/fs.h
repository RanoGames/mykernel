/* fs.h — простая иерархическая файловая система, целиком живущая в
 * оперативной памяти (RAM-based, наподобие tmpfs в Linux).
 *
 * ВАЖНО: это НЕ настоящая файловая система на диске — данные
 * пропадают при перезагрузке/выключении. Работа с настоящим диском
 * (драйвер ATA/AHCI + разбор FAT/ext) — гораздо более сложная и
 * рискованная тема (легко испортить данные на реальном диске при
 * ошибке), это следующий большой шаг после того, как обкатаете
 * работу с файлами в памяти. Команды ls/cd/mkdir/cat и т.д. работают
 * одинаково что для RAM-ФС, что для будущей дисковой — так что это
 * не потраченное время, а хорошая основа. */

#ifndef FS_H
#define FS_H

#include <stddef.h>

#define FS_NAME_MAX 32   /* максимальная длина имени файла/папки */
#define FS_FILE_MAX 512  /* максимальный размер содержимого одного файла */

/* Коды ошибок, которые возвращают функции файловой системы */
enum fs_result {
    FS_OK = 0,
    FS_ERR_NOT_FOUND,
    FS_ERR_ALREADY_EXISTS,
    FS_ERR_NOT_A_DIRECTORY,
    FS_ERR_IS_A_DIRECTORY,
    FS_ERR_NO_SPACE,        /* кончились "слоты" под новые файлы/папки */
    FS_ERR_DIR_NOT_EMPTY,   /* нельзя удалить непустую папку */
    FS_ERR_INVALID_NAME,
};

void fs_init(void);

/* Все операции ниже работают относительно текущей директории (cwd),
 * которую меняет fs_cd(). Это как обычный shell в Linux/Windows. */

enum fs_result fs_mkdir(const char* name);
enum fs_result fs_touch(const char* name);       /* создать пустой файл */
enum fs_result fs_cd(const char* name);           /* ".." — вверх, "/" — в корень */
enum fs_result fs_rm(const char* name);             /* удалить файл или ПУСТУЮ папку */

/* Записать текст в файл (перезаписывает содержимое; если файла нет — создаёт) */
enum fs_result fs_write(const char* name, const char* text);

/* Найти файл и получить указатель на его содержимое (только для чтения).
 * Возвращает FS_OK и заполняет out_content/out_len, либо код ошибки. */
enum fs_result fs_read(const char* name, const char** out_content, size_t* out_len);

/* Вывести список содержимого текущей директории на экран (сама вызывает terminal_writestring) */
void fs_ls(void);

/* Записать в buffer (размер buffer_size) полный путь текущей директории, например "/home/user" */
void fs_pwd(char* buffer, size_t buffer_size);

/* Человекочитаемое описание кода ошибки — для вывода в shell */
const char* fs_strerror(enum fs_result err);

#endif
