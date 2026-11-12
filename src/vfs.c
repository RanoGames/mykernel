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

int vfs_auto_mount(void) {
    if (!fat32_is_mounted()) {
        if (fat32_mount() != FAT_OK) {
            return -1;
        }
    }
    fs_mkdir_p("/mnt/c");
    fs_mkdir_p("/c");
    vfs_mount("/mnt/c", VFS_FS_FAT32);
    vfs_mount("/c", VFS_FS_FAT32);
    return 0;
}

void vfs_normalize_path(const char* path, char* out, size_t out_max) {
    if (!out || out_max == 0) return;
    char combined[128];
    if (!path || !path[0]) {
        fs_pwd(out, out_max);
        return;
    }
    if (path[0] == '/') {
        int i = 0;
        while (path[i] && i < 127) { combined[i] = path[i]; i++; }
        combined[i] = '\0';
    } else {
        char cwd[64];
        fs_pwd(cwd, sizeof(cwd));
        int i = 0;
        while (cwd[i] && i < 63) { combined[i] = cwd[i]; i++; }
        if (i > 0 && combined[i - 1] != '/' && i < 126) {
            combined[i++] = '/';
        }
        int j = 0;
        while (path[j] && i < 127) {
            combined[i++] = path[j++];
        }
        combined[i] = '\0';
    }

    char tokens[16][32];
    int token_count = 0;
    const char* p = combined;
    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;
        char tok[32];
        int t = 0;
        while (*p && *p != '/' && t < 31) tok[t++] = *p++;
        tok[t] = '\0';

        if (tok[0] == '.' && tok[1] == '\0') {
            /* ignore */
        } else if (tok[0] == '.' && tok[1] == '.' && tok[2] == '\0') {
            if (token_count > 0) token_count--;
        } else if (token_count < 16) {
            int k = 0;
            while (tok[k]) { tokens[token_count][k] = tok[k]; k++; }
            tokens[token_count][k] = '\0';
            token_count++;
        }
    }

    if (token_count == 0) {
        if (out_max > 1) { out[0] = '/'; out[1] = '\0'; }
        else out[0] = '\0';
        return;
    }

    size_t pos = 0;
    for (int i = 0; i < token_count; i++) {
        if (pos + 1 < out_max) out[pos++] = '/';
        int k = 0;
        while (tokens[i][k] && pos + 1 < out_max) {
            out[pos++] = tokens[i][k++];
        }
    }
    if (pos >= out_max) pos = out_max - 1;
    out[pos] = '\0';
}

void vfs_ls_path(const char* path) {
    char full[128];
    vfs_normalize_path(path, full, sizeof(full));

    char rest[64];
    enum vfs_fs_type ty = vfs_resolve(full, rest, sizeof(rest));
    if (ty == VFS_FS_FAT32) {
        if (!fat32_is_mounted()) {
            terminal_writestring("FAT32 not mounted\n");
            return;
        }
        fat32_ls(rest[0] ? rest : "/");
    } else {
        fs_ls();
    }
}

void vfs_cat_path(const char* path) {
    char full[128];
    vfs_normalize_path(path, full, sizeof(full));

    char rest[64];
    enum vfs_fs_type ty = vfs_resolve(full, rest, sizeof(rest));
    if (ty == VFS_FS_FAT32) {
        fat32_cat(rest);
    } else {
        const char* content; size_t len;
        if (fs_read_path(full, &content, &len) == FS_OK || fs_read(path, &content, &len) == FS_OK) {
            terminal_writestring(content);
            terminal_putchar('\n');
        } else {
            terminal_writestring("not found\n");
        }
    }
}

enum fs_result vfs_cd(const char* path) {
    char full[128];
    vfs_normalize_path(path, full, sizeof(full));

    char rest[64];
    enum vfs_fs_type ty = vfs_resolve(full, rest, sizeof(rest));
    if (ty == VFS_FS_FAT32) {
        fs_mkdir_p(full);
        return fs_cd(full);
    } else {
        return fs_cd(path);
    }
}

static enum fs_result fat_to_fs_err(enum fat_result fr) {
    switch (fr) {
        case FAT_OK: return FS_OK;
        case FAT_ERR_NOT_FOUND: return FS_ERR_NOT_FOUND;
        case FAT_ERR_EXISTS: return FS_ERR_ALREADY_EXISTS;
        case FAT_ERR_IS_DIR: return FS_ERR_IS_A_DIRECTORY;
        case FAT_ERR_NOT_DIR: return FS_ERR_NOT_A_DIRECTORY;
        case FAT_ERR_NO_SPACE: return FS_ERR_NO_SPACE;
        case FAT_ERR_NOT_EMPTY: return FS_ERR_DIR_NOT_EMPTY;
        case FAT_ERR_TOO_BIG: return FS_ERR_TOO_BIG;
        default: return FS_ERR_NOT_FOUND;
    }
}

enum fs_result vfs_touch(const char* path) {
    char full[128];
    vfs_normalize_path(path, full, sizeof(full));

    char rest[64];
    enum vfs_fs_type ty = vfs_resolve(full, rest, sizeof(rest));
    if (ty == VFS_FS_FAT32) {
        enum fat_result fr = fat32_write(rest, "");
        return fat_to_fs_err(fr);
    } else {
        return fs_touch(path);
    }
}

enum fs_result vfs_write(const char* path, const char* text) {
    char full[128];
    vfs_normalize_path(path, full, sizeof(full));

    char rest[64];
    enum vfs_fs_type ty = vfs_resolve(full, rest, sizeof(rest));
    if (ty == VFS_FS_FAT32) {
        enum fat_result fr = fat32_write(rest, text ? text : "");
        return fat_to_fs_err(fr);
    } else {
        return fs_write(path, text);
    }
}

enum fs_result vfs_append(const char* path, const char* text) {
    char full[128];
    vfs_normalize_path(path, full, sizeof(full));

    char rest[64];
    enum vfs_fs_type ty = vfs_resolve(full, rest, sizeof(rest));
    if (ty == VFS_FS_FAT32) {
        char buf[1024];
        size_t existing_len = 0;
        fat32_read_file(rest, buf, sizeof(buf) - 1, &existing_len);
        buf[existing_len] = '\0';
        int t = 0;
        while (text && text[t] && existing_len + t < sizeof(buf) - 1) {
            buf[existing_len + t] = text[t];
            t++;
        }
        buf[existing_len + t] = '\0';
        enum fat_result fr = fat32_write(rest, buf);
        return fat_to_fs_err(fr);
    } else {
        return fs_append(path, text);
    }
}

enum fs_result vfs_mkdir(const char* path) {
    char full[128];
    vfs_normalize_path(path, full, sizeof(full));

    char rest[64];
    enum vfs_fs_type ty = vfs_resolve(full, rest, sizeof(rest));
    if (ty == VFS_FS_FAT32) {
        enum fat_result fr = fat32_mkdir(rest);
        fs_mkdir_p(full);
        return fat_to_fs_err(fr);
    } else {
        return fs_mkdir(path);
    }
}

enum fs_result vfs_rm(const char* path, int recursive) {
    (void)recursive;
    char full[128];
    vfs_normalize_path(path, full, sizeof(full));

    char rest[64];
    enum vfs_fs_type ty = vfs_resolve(full, rest, sizeof(rest));
    if (ty == VFS_FS_FAT32) {
        enum fat_result fr = fat32_unlink(rest);
        fs_rm(full);
        return fat_to_fs_err(fr);
    } else {
        return recursive ? fs_rm_rf(path) : fs_rm(path);
    }
}
