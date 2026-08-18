/* vfs.h — simple mount table (RAM FS + FAT32) */
#ifndef VFS_H
#define VFS_H

#include "fs.h"

#define VFS_MAX_MOUNTS 4
#define VFS_MP_NAME_MAX 16

enum vfs_fs_type {
    VFS_FS_NONE = 0,
    VFS_FS_RAM,
    VFS_FS_FAT32,
};

struct vfs_mount {
    int used;
    char path[VFS_MP_NAME_MAX]; /* e.g. "/", "/mnt" */
    enum vfs_fs_type type;
};

void vfs_init(void);

/* Mount filesystem at path. path like "/mnt". */
enum fs_result vfs_mount(const char* path, enum vfs_fs_type type);

/* Unmount by path */
enum fs_result vfs_umount(const char* path);

void vfs_list(void); /* print mounts */

/* Resolve: which FS owns this absolute-ish path? */
enum vfs_fs_type vfs_resolve(const char* path, char* rest_out, int rest_max);

int vfs_is_mounted(enum vfs_fs_type type);

void vfs_ls_path(const char* path);
void vfs_cat_path(const char* path);

#endif
