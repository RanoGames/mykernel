/* fs.h — hierarchical RAM FS (tmpfs-like). */

#ifndef FS_H
#define FS_H

#include <stddef.h>
#include <stdint.h>

#define FS_NAME_MAX 48
#define FS_FILE_MAX 8192

enum fs_result {
    FS_OK = 0,
    FS_ERR_NOT_FOUND,
    FS_ERR_ALREADY_EXISTS,
    FS_ERR_NOT_A_DIRECTORY,
    FS_ERR_IS_A_DIRECTORY,
    FS_ERR_NO_SPACE,
    FS_ERR_DIR_NOT_EMPTY,
    FS_ERR_INVALID_NAME,
    FS_ERR_TOO_BIG,
};

void fs_init(void);

enum fs_result fs_mkdir(const char* name);
enum fs_result fs_touch(const char* name);
enum fs_result fs_cd(const char* name);
enum fs_result fs_rm(const char* name);
enum fs_result fs_rm_rf(const char* name); /* recursive, like rm -r */
enum fs_result fs_write(const char* name, const char* text);
enum fs_result fs_write_bin(const char* name, const void* data, size_t len);
enum fs_result fs_append(const char* name, const char* text);
enum fs_result fs_read(const char* name, const char** out_content, size_t* out_len);
enum fs_result fs_read_path(const char* path, const char** out_content, size_t* out_len);
enum fs_result fs_cp(const char* src, const char* dst);
enum fs_result fs_mv(const char* src, const char* dst); /* atomic under FS lock */
/* Exclusive create: fails if name exists (like open O_EXCL) — use fs_touch */
enum fs_result fs_size(const char* name, size_t* out_size);
enum fs_result fs_mkdir_p(const char* path);

void fs_ls(void);
int fs_cwd_entry(int index, char* name_out, size_t name_sz, int* is_dir);
void fs_pwd(char* buffer, size_t buffer_size);
int fs_node_count(void);
const char* fs_strerror(enum fs_result err);

#endif
