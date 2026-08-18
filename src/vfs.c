/* vfs.c — mount table for MyKernel */
#include "vfs.h"
#include "fat32.h"
#include "vga.h"
#include "atomic.h"

static struct vfs_mount mounts[VFS_MAX_MOUNTS];
static spinlock_t vfs_lock;

static int str_eq(const char* a, const char* b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == *b;
}

static void str_copy(char* d, const char* s, int max) {
    int i = 0;
    while (s[i] && i < max - 1) { d[i] = s[i]; i++; }
    d[i] = 0;
}

static int str_len(const char* s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

void vfs_init(void) {
    spin_init(&vfs_lock);
    for (int i = 0; i < VFS_MAX_MOUNTS; i++)
        mounts[i].used = 0;

    /* Root is always RAM FS */
    mounts[0].used = 1;
    str_copy(mounts[0].path, "/", VFS_MP_NAME_MAX);
    mounts[0].type = VFS_FS_RAM;
}

static int find_mount(const char* path) {
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (mounts[i].used && str_eq(mounts[i].path, path))
            return i;
    }
    return -1;
}

enum fs_result vfs_mount(const char* path, enum vfs_fs_type type) {
    if (!path || path[0] != '/') return FS_ERR_INVALID_NAME;
    if (type == VFS_FS_NONE) return FS_ERR_INVALID_NAME;
    if (str_eq(path, "/")) return FS_ERR_ALREADY_EXISTS; /* cannot replace root */

    spin_lock(&vfs_lock);

    if (find_mount(path) >= 0) {
        spin_unlock(&vfs_lock);
        return FS_ERR_ALREADY_EXISTS;
    }

    if (type == VFS_FS_FAT32) {
        if (!fat32_is_mounted()) {
            if (fat32_mount() != 0) {
                spin_unlock(&vfs_lock);
                return FS_ERR_NOT_FOUND; /* no disk / not FAT */
            }
        }
    }

    int slot = -1;
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (!mounts[i].used) { slot = i; break; }
    }
    if (slot < 0) {
        spin_unlock(&vfs_lock);
        return FS_ERR_NO_SPACE;
    }

    mounts[slot].used = 1;
    str_copy(mounts[slot].path, path, VFS_MP_NAME_MAX);
    mounts[slot].type = type;

    /* Ensure RAM dir exists as mount point marker */
    spin_unlock(&vfs_lock);
    if (type == VFS_FS_FAT32) {
        /* mkdir mount point in RAM if missing (ignore errors) */
        fs_mkdir_p(path);
    }
    return FS_OK;
}

enum fs_result vfs_umount(const char* path) {
    if (!path || str_eq(path, "/")) return FS_ERR_INVALID_NAME;

    spin_lock(&vfs_lock);
    int idx = find_mount(path);
    if (idx < 0) {
        spin_unlock(&vfs_lock);
        return FS_ERR_NOT_FOUND;
    }
    mounts[idx].used = 0;
    mounts[idx].type = VFS_FS_NONE;
    mounts[idx].path[0] = 0;
    spin_unlock(&vfs_lock);
    return FS_OK;
}

void vfs_list(void) {
    spin_lock(&vfs_lock);
    terminal_writestring("Mount table:\n");
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (!mounts[i].used) continue;
        terminal_writestring("  ");
        terminal_writestring(mounts[i].path);
        terminal_writestring("  ->  ");
        if (mounts[i].type == VFS_FS_RAM)
            terminal_writestring("ramfs\n");
        else if (mounts[i].type == VFS_FS_FAT32)
            terminal_writestring("fat32\n");
        else
            terminal_writestring("?\n");
    }
    spin_unlock(&vfs_lock);
}

enum vfs_fs_type vfs_resolve(const char* path, char* rest_out, int rest_max) {
    if (!path || !path[0]) {
        if (rest_out && rest_max > 0) rest_out[0] = 0;
        return VFS_FS_RAM;
    }

    spin_lock(&vfs_lock);
    /* Longest prefix match */
    int best = -1;
    int best_len = -1;
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (!mounts[i].used) continue;
        int len = str_len(mounts[i].path);
        int ok = 1;
        for (int k = 0; k < len; k++) {
            if (path[k] != mounts[i].path[k]) { ok = 0; break; }
        }
        if (!ok) continue;
        /* boundary: end or '/' */
        if (path[len] != 0 && path[len] != '/' && !(len == 1 && mounts[i].path[0] == '/'))
            continue;
        if (len > best_len) {
            best_len = len;
            best = i;
        }
    }
    enum vfs_fs_type ty = VFS_FS_RAM;
    if (best >= 0) {
        ty = mounts[best].type;
        if (rest_out && rest_max > 0) {
            const char* rest = path + best_len;
            if (*rest == '/') rest++;
            str_copy(rest_out, rest, rest_max);
        }
    } else if (rest_out && rest_max > 0) {
        str_copy(rest_out, path, rest_max);
    }
    spin_unlock(&vfs_lock);
    return ty;
}

int vfs_is_mounted(enum vfs_fs_type type) {
    spin_lock(&vfs_lock);
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (mounts[i].used && mounts[i].type == type) {
            spin_unlock(&vfs_lock);
            return 1;
        }
    }
    spin_unlock(&vfs_lock);
    return 0;
}


#include "fs.h"

/* Unified ls/cat through mount table */
void vfs_ls_path(const char* path) {
    char rest[64];
    enum vfs_fs_type ty = vfs_resolve(path ? path : "/", rest, sizeof(rest));
    if (ty == VFS_FS_FAT32) {
        if (!fat32_is_mounted()) {
            terminal_writestring("FAT32 not mounted\n");
            return;
        }
        fat32_ls(rest[0] ? rest : "/");
    } else {
        if (rest[0]) {
            /* try cd into path for RAM — simple: only list cwd for now */
            terminal_writestring("(ramfs) use: cd + ls  or absolute under /\n");
        }
        fs_ls();
    }
}

void vfs_cat_path(const char* path) {
    char rest[64];
    enum vfs_fs_type ty = vfs_resolve(path ? path : "", rest, sizeof(rest));
    if (ty == VFS_FS_FAT32) {
        fat32_cat(rest);
    } else {
        const char* content; size_t len;
        if (fs_read_path(path, &content, &len) == FS_OK || fs_read(rest, &content, &len) == FS_OK) {
            terminal_writestring(content);
            terminal_putchar('\n');
        } else {
            terminal_writestring("not found\n");
        }
    }
}
