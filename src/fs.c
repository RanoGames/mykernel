/* fs.c — реализация RAM-файловой системы. */

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

void fs_init(void) {
    for (int i = 0; i < FS_MAX_NODES; i++)
        nodes[i].type = FS_TYPE_FREE;

    nodes[FS_ROOT_INDEX].type = FS_TYPE_DIR;
    k_strcpy_truncate(nodes[FS_ROOT_INDEX].name, "/", FS_NAME_MAX);
    nodes[FS_ROOT_INDEX].parent = -1;

    cwd = FS_ROOT_INDEX;
}

static int fs_alloc_node(void) {
    for (int i = 0; i < FS_MAX_NODES; i++) {
        if (nodes[i].type == FS_TYPE_FREE)
            return i;
    }
    return -1;
}

static int fs_find_child(int parent_idx, const char* name) {
    for (int i = 0; i < FS_MAX_NODES; i++) {
        if (nodes[i].type != FS_TYPE_FREE &&
            nodes[i].parent == parent_idx &&
            k_streq(nodes[i].name, name)) {
            return i;
        }
    }
    return -1;
}

static int fs_is_valid_name(const char* name) {
    if (name[0] == '\0')
        return 0;
    if (k_streq(name, ".") || k_streq(name, ".."))
        return 0;
    if (k_strlen(name) >= FS_NAME_MAX)
        return 0;
    return 1;
}

enum fs_result fs_mkdir(const char* name) {
    if (!fs_is_valid_name(name))
        return FS_ERR_INVALID_NAME;
    if (fs_find_child(cwd, name) != -1)
        return FS_ERR_ALREADY_EXISTS;

    int idx = fs_alloc_node();
    if (idx == -1)
        return FS_ERR_NO_SPACE;

    nodes[idx].type = FS_TYPE_DIR;
    k_strcpy_truncate(nodes[idx].name, name, FS_NAME_MAX);
    nodes[idx].parent = cwd;
    return FS_OK;
}

enum fs_result fs_touch(const char* name) {
    if (!fs_is_valid_name(name))
        return FS_ERR_INVALID_NAME;
    if (fs_find_child(cwd, name) != -1)
        return FS_ERR_ALREADY_EXISTS;

    int idx = fs_alloc_node();
    if (idx == -1)
        return FS_ERR_NO_SPACE;

    nodes[idx].type = FS_TYPE_FILE;
    k_strcpy_truncate(nodes[idx].name, name, FS_NAME_MAX);
    nodes[idx].parent = cwd;
    nodes[idx].content_len = 0;
    nodes[idx].content[0] = '\0';
    return FS_OK;
}

enum fs_result fs_cd(const char* name) {
    if (k_streq(name, ".")) {
        return FS_OK;
    } else if (k_streq(name, "..")) {
        if (nodes[cwd].parent != -1)
            cwd = nodes[cwd].parent;
        return FS_OK;
    } else if (k_streq(name, "/")) {
        cwd = FS_ROOT_INDEX;
        return FS_OK;
    }

    int idx = fs_find_child(cwd, name);
    if (idx == -1)
        return FS_ERR_NOT_FOUND;
    if (nodes[idx].type != FS_TYPE_DIR)
        return FS_ERR_NOT_A_DIRECTORY;

    cwd = idx;
    return FS_OK;
}

enum fs_result fs_rm(const char* name) {
    int idx = fs_find_child(cwd, name);
    if (idx == -1)
        return FS_ERR_NOT_FOUND;

    if (nodes[idx].type == FS_TYPE_DIR) {
        for (int i = 0; i < FS_MAX_NODES; i++) {
            if (nodes[i].type != FS_TYPE_FREE && nodes[i].parent == idx)
                return FS_ERR_DIR_NOT_EMPTY;
        }
    }

    nodes[idx].type = FS_TYPE_FREE;
    return FS_OK;
}

enum fs_result fs_write(const char* name, const char* text) {
    int idx = fs_find_child(cwd, name);

    if (idx == -1) {
        enum fs_result r = fs_touch(name);
        if (r != FS_OK)
            return r;
        idx = fs_find_child(cwd, name);
    }

    if (nodes[idx].type != FS_TYPE_FILE)
        return FS_ERR_IS_A_DIRECTORY;

    k_strcpy_truncate(nodes[idx].content, text, FS_FILE_MAX);
    nodes[idx].content_len = k_strlen(nodes[idx].content);
    return FS_OK;
}

enum fs_result fs_read(const char* name, const char** out_content, size_t* out_len) {
    int idx = fs_find_child(cwd, name);
    if (idx == -1)
        return FS_ERR_NOT_FOUND;
    if (nodes[idx].type != FS_TYPE_FILE)
        return FS_ERR_IS_A_DIRECTORY;

    *out_content = nodes[idx].content;
    *out_len = nodes[idx].content_len;
    return FS_OK;
}

void fs_ls(void) {
    int found_any = 0;
    for (int i = 0; i < FS_MAX_NODES; i++) {
        if (nodes[i].type != FS_TYPE_FREE && nodes[i].parent == cwd) {
            found_any = 1;
            terminal_writestring("  ");
            terminal_writestring(nodes[i].name);
            if (nodes[i].type == FS_TYPE_DIR)
                terminal_writestring("/");
            terminal_writestring("\n");
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
        case FS_OK:                  return "OK";
        case FS_ERR_NOT_FOUND:       return "no such file or directory";
        case FS_ERR_ALREADY_EXISTS:  return "already exists";
        case FS_ERR_NOT_A_DIRECTORY: return "not a directory";
        case FS_ERR_IS_A_DIRECTORY:  return "is a directory, not a file";
        case FS_ERR_NO_SPACE:        return "no space left (file limit reached)";
        case FS_ERR_DIR_NOT_EMPTY:   return "directory not empty";
        case FS_ERR_INVALID_NAME:    return "invalid name";
        default:                     return "unknown error";
    }
}
