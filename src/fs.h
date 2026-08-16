/* fs.h — иерархическая файловая система в RAM (как tmpfs). */

#ifndef FS_H
#define FS_H

#include <stddef.h>

#define FS_NAME_MAX 48
#define FS_FILE_MAX 2048

enum fs_result {
    FS_OK = 0,
    FS_ERR_NOT_FOUND,
    FS_ERR_ALREADY_EXISTS,
    FS_ERR_NOT_A_DIRECTORY,
    FS_ERR_IS_A_DIRECTORY,
    FS_ERR_NO_SPACE,
    FS_ERR_DIR_NOT_EMPTY,
    FS_ERR_INVALID_NAME,
};

void fs_init(void);

enum fs_result fs_mkdir(const char* name);
enum fs_result fs_touch(const char* name);
enum fs_result fs_cd(const char* name);
enum fs_result fs_rm(const char* name);
enum fs_result fs_write(const char* name, const char* text);
enum fs_result fs_append(const char* name, const char* text);
enum fs_result fs_read(const char* name, const char** out_content, size_t* out_len);
enum fs_result fs_cp(const char* src, const char* dst);
enum fs_result fs_mv(const char* src, const char* dst);
enum fs_result fs_size(const char* name, size_t* out_size);

void fs_ls(void);
void fs_pwd(char* buffer, size_t buffer_size);
int fs_node_count(void);
const char* fs_strerror(enum fs_result err);

#endif
