/* fs.c — RAM FS + /lib layout */

#include "fs.h"
#include "vga.h"

#define FS_MAX_NODES 256
#define FS_ROOT_INDEX 0
#define FS_MAX_DEPTH  16

enum fs_node_type {
    FS_TYPE_FREE = 0,
    FS_TYPE_DIR,
    FS_TYPE_FILE,
};

struct fs_node {
    enum fs_node_type type;
    char name[FS_NAME_MAX];
    int parent;
    char content[FS_FILE_MAX];
    size_t content_len;
};

static struct fs_node nodes[FS_MAX_NODES];
static int cwd;

static size_t k_strlen(const char* s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

static int k_streq(const char* a, const char* b) {
    size_t i = 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) return 0;
        i++;
    }
    return a[i] == b[i];
}

static void k_strcpy_truncate(char* dst, const char* src, size_t dst_size) {
    size_t i = 0;
    while (src[i] && i < dst_size - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void k_memcpy(void* d, const void* s, size_t n) {
    uint8_t* dd = (uint8_t*)d;
    const uint8_t* ss = (const uint8_t*)s;
    for (size_t i = 0; i < n; i++) dd[i] = ss[i];
}

void fs_init(void) {
    for (int i = 0; i < FS_MAX_NODES; i++)
        nodes[i].type = FS_TYPE_FREE;

    nodes[FS_ROOT_INDEX].type = FS_TYPE_DIR;
    k_strcpy_truncate(nodes[FS_ROOT_INDEX].name, "/", FS_NAME_MAX);
    nodes[FS_ROOT_INDEX].parent = -1;
    cwd = FS_ROOT_INDEX;

    fs_mkdir("lib");
    fs_mkdir("bin");
    fs_mkdir("usr");
    fs_cd("usr");
    fs_mkdir("lib");
    fs_cd("/");
}

static int fs_alloc_node(void) {
    for (int i = 0; i < FS_MAX_NODES; i++)
        if (nodes[i].type == FS_TYPE_FREE)
            return i;
    return -1;
}

static int fs_find_child(int parent_idx, const char* name) {
    for (int i = 0; i < FS_MAX_NODES; i++) {
        if (nodes[i].type != FS_TYPE_FREE &&
            nodes[i].parent == parent_idx &&
            k_streq(nodes[i].name, name))
            return i;
    }
    return -1;
}

static int fs_is_valid_name(const char* name) {
    if (name[0] == '\0') return 0;
    if (k_streq(name, ".") || k_streq(name, "..")) return 0;
    if (k_strlen(name) >= FS_NAME_MAX) return 0;
    return 1;
}

enum fs_result fs_mkdir(const char* name) {
    if (!fs_is_valid_name(name)) return FS_ERR_INVALID_NAME;
    if (fs_find_child(cwd, name) != -1) return FS_ERR_ALREADY_EXISTS;
    int idx = fs_alloc_node();
    if (idx == -1) return FS_ERR_NO_SPACE;
    nodes[idx].type = FS_TYPE_DIR;
    k_strcpy_truncate(nodes[idx].name, name, FS_NAME_MAX);
    nodes[idx].parent = cwd;
    return FS_OK;
}

enum fs_result fs_touch(const char* name) {
    if (!fs_is_valid_name(name)) return FS_ERR_INVALID_NAME;
    if (fs_find_child(cwd, name) != -1) return FS_ERR_ALREADY_EXISTS;
    int idx = fs_alloc_node();
    if (idx == -1) return FS_ERR_NO_SPACE;
    nodes[idx].type = FS_TYPE_FILE;
    k_strcpy_truncate(nodes[idx].name, name, FS_NAME_MAX);
    nodes[idx].parent = cwd;
    nodes[idx].content_len = 0;
    nodes[idx].content[0] = '\0';
    return FS_OK;
}

enum fs_result fs_cd(const char* name) {
    if (k_streq(name, ".")) return FS_OK;
    if (k_streq(name, "..")) {
        if (nodes[cwd].parent != -1)
            cwd = nodes[cwd].parent;
        return FS_OK;
    }
    if (k_streq(name, "/")) {
        cwd = FS_ROOT_INDEX;
        return FS_OK;
    }
    int idx = fs_find_child(cwd, name);
    if (idx == -1) return FS_ERR_NOT_FOUND;
    if (nodes[idx].type != FS_TYPE_DIR) return FS_ERR_NOT_A_DIRECTORY;
    cwd = idx;
    return FS_OK;
}

/* Free node idx and all descendants (post-order). depth guards cycles. */
static enum fs_result fs_rm_node_recursive(int idx, int depth) {
    if (idx < 0 || idx >= FS_MAX_NODES) return FS_ERR_NOT_FOUND;
    if (nodes[idx].type == FS_TYPE_FREE) return FS_OK;
    if (idx == FS_ROOT_INDEX) return FS_ERR_INVALID_NAME; /* never delete / */
    if (depth > FS_MAX_DEPTH) return FS_ERR_INVALID_NAME;

    if (nodes[idx].type == FS_TYPE_DIR) {
        /* Restart scan: removing children shifts logical tree */
        int progress = 1;
        while (progress) {
            progress = 0;
            for (int i = 0; i < FS_MAX_NODES; i++) {
                if (nodes[i].type == FS_TYPE_FREE) continue;
                if (nodes[i].parent != idx) continue;
                enum fs_result r = fs_rm_node_recursive(i, depth + 1);
                if (r != FS_OK) return r;
                progress = 1;
                break; /* restart from beginning after one removal */
            }
        }
    }

    /* If cwd was inside deleted subtree, jump to parent of idx or root */
    int p = nodes[idx].parent;
    int walk = cwd;
    while (walk != -1) {
        if (walk == idx) {
            cwd = (p >= 0) ? p : FS_ROOT_INDEX;
            break;
        }
        walk = nodes[walk].parent;
    }

    nodes[idx].type = FS_TYPE_FREE;
    nodes[idx].content_len = 0;
    nodes[idx].content[0] = '\0';
    nodes[idx].name[0] = '\0';
    nodes[idx].parent = -1;
    return FS_OK;
}

/* Non-recursive: files OK; empty dirs OK; non-empty dir → DIR_NOT_EMPTY */
enum fs_result fs_rm(const char* name) {
    if (!name || !name[0]) return FS_ERR_INVALID_NAME;
    if (name[0] == '/' && name[1] == '\0') return FS_ERR_INVALID_NAME;
    int idx = fs_find_child(cwd, name);
    if (idx == -1) return FS_ERR_NOT_FOUND;
    if (nodes[idx].type == FS_TYPE_DIR) {
        for (int i = 0; i < FS_MAX_NODES; i++)
            if (nodes[i].type != FS_TYPE_FREE && nodes[i].parent == idx)
                return FS_ERR_DIR_NOT_EMPTY;
    }
    return fs_rm_node_recursive(idx, 0);
}

/* Recursive: rm -r — delete file or directory tree */
enum fs_result fs_rm_rf(const char* name) {
    if (!name || !name[0]) return FS_ERR_INVALID_NAME;
    if (name[0] == '/' && name[1] == '\0') return FS_ERR_INVALID_NAME;
    int idx = fs_find_child(cwd, name);
    if (idx == -1) return FS_ERR_NOT_FOUND;
    return fs_rm_node_recursive(idx, 0);
}

enum fs_result fs_write(const char* name, const char* text) {
    return fs_write_bin(name, text, k_strlen(text));
}

enum fs_result fs_write_bin(const char* name, const void* data, size_t len) {
    if (len >= FS_FILE_MAX) return FS_ERR_TOO_BIG;
    int idx = fs_find_child(cwd, name);
    if (idx == -1) {
        enum fs_result r = fs_touch(name);
        if (r != FS_OK) return r;
        idx = fs_find_child(cwd, name);
    }
    if (nodes[idx].type != FS_TYPE_FILE) return FS_ERR_IS_A_DIRECTORY;
    k_memcpy(nodes[idx].content, data, len);
    nodes[idx].content[len] = '\0';
    nodes[idx].content_len = len;
    return FS_OK;
}

enum fs_result fs_append(const char* name, const char* text) {
    int idx = fs_find_child(cwd, name);
    if (idx == -1) {
        enum fs_result r = fs_touch(name);
        if (r != FS_OK) return r;
        idx = fs_find_child(cwd, name);
    }
    if (nodes[idx].type != FS_TYPE_FILE) return FS_ERR_IS_A_DIRECTORY;

    size_t pos = nodes[idx].content_len;
    if (pos > 0 && pos + 1 < FS_FILE_MAX)
        nodes[idx].content[pos++] = '\n';
    size_t i = 0;
    while (text[i] && pos + 1 < FS_FILE_MAX)
        nodes[idx].content[pos++] = text[i++];
    nodes[idx].content[pos] = '\0';
    nodes[idx].content_len = pos;
    return FS_OK;
}

enum fs_result fs_read(const char* name, const char** out_content, size_t* out_len) {
    int idx = fs_find_child(cwd, name);
    if (idx == -1) return FS_ERR_NOT_FOUND;
    if (nodes[idx].type != FS_TYPE_FILE) return FS_ERR_IS_A_DIRECTORY;
    *out_content = nodes[idx].content;
    *out_len = nodes[idx].content_len;
    return FS_OK;
}

enum fs_result fs_read_path(const char* path, const char** out_content, size_t* out_len) {
    if (!path || !path[0]) return FS_ERR_NOT_FOUND;

    int saved = cwd;
    int dir = FS_ROOT_INDEX;
    if (path[0] != '/')
        dir = cwd;

    const char* p = path;
    if (*p == '/') p++;

    char component[FS_NAME_MAX];
    while (*p) {
        size_t n = 0;
        while (p[n] && p[n] != '/') {
            if (n + 1 < FS_NAME_MAX) component[n] = p[n];
            n++;
        }
        if (n >= FS_NAME_MAX) { cwd = saved; return FS_ERR_INVALID_NAME; }
        component[n] = '\0';
        p += n;
        if (*p == '/') p++;

        if (component[0] == '\0' || k_streq(component, "."))
            continue;
        if (k_streq(component, "..")) {
            if (nodes[dir].parent != -1) dir = nodes[dir].parent;
            continue;
        }

        int idx = fs_find_child(dir, component);
        if (idx == -1) { cwd = saved; return FS_ERR_NOT_FOUND; }

        if (*p == '\0') {
            if (nodes[idx].type != FS_TYPE_FILE) { cwd = saved; return FS_ERR_IS_A_DIRECTORY; }
            *out_content = nodes[idx].content;
            *out_len = nodes[idx].content_len;
            cwd = saved;
            return FS_OK;
        }
        if (nodes[idx].type != FS_TYPE_DIR) { cwd = saved; return FS_ERR_NOT_A_DIRECTORY; }
        dir = idx;
    }
    cwd = saved;
    return FS_ERR_NOT_FOUND;
}

enum fs_result fs_mkdir_p(const char* path) {
    if (!path || path[0] != '/') return FS_ERR_INVALID_NAME;
    int saved = cwd;
    cwd = FS_ROOT_INDEX;
    const char* p = path + 1;
    char component[FS_NAME_MAX];
    while (*p) {
        size_t n = 0;
        while (p[n] && p[n] != '/') {
            if (n + 1 < FS_NAME_MAX) component[n] = p[n];
            n++;
        }
        component[n] = '\0';
        p += n;
        if (*p == '/') p++;
        if (!component[0]) continue;
        int idx = fs_find_child(cwd, component);
        if (idx == -1) {
            enum fs_result r = fs_mkdir(component);
            if (r != FS_OK) { cwd = saved; return r; }
            idx = fs_find_child(cwd, component);
        }
        if (nodes[idx].type != FS_TYPE_DIR) { cwd = saved; return FS_ERR_NOT_A_DIRECTORY; }
        cwd = idx;
    }
    cwd = saved;
    return FS_OK;
}

enum fs_result fs_cp(const char* src, const char* dst) {
    int sidx = fs_find_child(cwd, src);
    if (sidx == -1) return FS_ERR_NOT_FOUND;
    if (nodes[sidx].type != FS_TYPE_FILE) return FS_ERR_IS_A_DIRECTORY;
    if (!fs_is_valid_name(dst)) return FS_ERR_INVALID_NAME;
    if (fs_find_child(cwd, dst) != -1) return FS_ERR_ALREADY_EXISTS;
    int didx = fs_alloc_node();
    if (didx == -1) return FS_ERR_NO_SPACE;
    nodes[didx].type = FS_TYPE_FILE;
    k_strcpy_truncate(nodes[didx].name, dst, FS_NAME_MAX);
    nodes[didx].parent = cwd;
    k_memcpy(nodes[didx].content, nodes[sidx].content, nodes[sidx].content_len);
    nodes[didx].content_len = nodes[sidx].content_len;
    nodes[didx].content[nodes[didx].content_len] = '\0';
    return FS_OK;
}

enum fs_result fs_mv(const char* src, const char* dst) {
    int sidx = fs_find_child(cwd, src);
    if (sidx == -1) return FS_ERR_NOT_FOUND;
    if (!fs_is_valid_name(dst)) return FS_ERR_INVALID_NAME;
    if (fs_find_child(cwd, dst) != -1) return FS_ERR_ALREADY_EXISTS;
    k_strcpy_truncate(nodes[sidx].name, dst, FS_NAME_MAX);
    return FS_OK;
}

enum fs_result fs_size(const char* name, size_t* out_size) {
    int idx = fs_find_child(cwd, name);
    if (idx == -1) return FS_ERR_NOT_FOUND;
    if (nodes[idx].type == FS_TYPE_DIR) {
        *out_size = 0;
        return FS_ERR_IS_A_DIRECTORY;
    }
    *out_size = nodes[idx].content_len;
    return FS_OK;
}

int fs_node_count(void) {
    int n = 0;
    for (int i = 0; i < FS_MAX_NODES; i++)
        if (nodes[i].type != FS_TYPE_FREE) n++;
    return n;
}

int fs_cwd_entry(int index, char* name_out, size_t name_sz, int* is_dir) {
    int n = 0;
    for (int i = 0; i < FS_MAX_NODES; i++) {
        if (nodes[i].type == FS_TYPE_FREE) continue;
        if (nodes[i].parent != cwd) continue;
        if (n == index) {
            k_strcpy_truncate(name_out, nodes[i].name, name_sz);
            if (is_dir) *is_dir = (nodes[i].type == FS_TYPE_DIR) ? 1 : 0;
            return 0;
        }
        n++;
    }
    return -1;
}

void fs_ls(void) {
    int found_any = 0;
    for (int i = 0; i < FS_MAX_NODES; i++) {
        if (nodes[i].type != FS_TYPE_FREE && nodes[i].parent == cwd) {
            found_any = 1;
            if (nodes[i].type == FS_TYPE_DIR) {
                terminal_writestring("  [DIR]  ");
                terminal_writestring(nodes[i].name);
                terminal_writestring("/\n");
            } else {
                terminal_writestring("  [FILE] ");
                terminal_writestring(nodes[i].name);
                terminal_writestring("  (");
                terminal_write_uint((uint32_t)nodes[i].content_len);
                terminal_writestring(" bytes)\n");
            }
        }
    }
    if (!found_any)
        terminal_writestring("  (empty)\n");
}

void fs_pwd(char* buffer, size_t buffer_size) {
    int path[FS_MAX_DEPTH];
    int depth = 0;
    int cur = cwd;
    while (cur != -1 && depth < FS_MAX_DEPTH) {
        path[depth++] = cur;
        cur = nodes[cur].parent;
    }
    size_t pos = 0;
    if (depth == 1) {
        k_strcpy_truncate(buffer, "/", buffer_size);
        return;
    }
    for (int i = depth - 2; i >= 0; i--) {
        const char* name = nodes[path[i]].name;
        size_t name_len = k_strlen(name);
        if (pos + 1 < buffer_size) buffer[pos++] = '/';
        for (size_t j = 0; j < name_len && pos + 1 < buffer_size; j++)
            buffer[pos++] = name[j];
    }
    buffer[pos] = '\0';
}

const char* fs_strerror(enum fs_result err) {
    switch (err) {
        case FS_OK: return "OK";
        case FS_ERR_NOT_FOUND: return "no such file or directory";
        case FS_ERR_ALREADY_EXISTS: return "already exists";
        case FS_ERR_NOT_A_DIRECTORY: return "not a directory";
        case FS_ERR_IS_A_DIRECTORY: return "is a directory, not a file";
        case FS_ERR_NO_SPACE: return "no space left";
        case FS_ERR_DIR_NOT_EMPTY: return "directory not empty";
        case FS_ERR_INVALID_NAME: return "invalid name";
        case FS_ERR_TOO_BIG: return "file too big";
        default: return "unknown error";
    }
}
