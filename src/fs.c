/* fs.c — RAM FS + /lib layout */

#include "fs.h"
#include "vga.h"
#include "atomic.h"

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

/* ---- FS lock: spinlock (atomic xchg) + nestable cli ---- */
static spinlock_t fs_spinlock;
static int fs_lock_depth;
static uint32_t fs_if_was_on;

static void fs_lock(void) {
    uint32_t flags;
    __asm__ volatile ("pushf; pop %0; cli" : "=r"(flags));
    if (fs_lock_depth == 0) {
        fs_if_was_on = flags & 0x200;
        spin_lock(&fs_spinlock);
    }
    fs_lock_depth++;
}

static void fs_unlock(void) {
    if (fs_lock_depth <= 0) return;
    fs_lock_depth--;
    if (fs_lock_depth == 0) {
        spin_unlock(&fs_spinlock);
        if (fs_if_was_on)
            __asm__ volatile ("sti");
    }
}

static size_t k_strlen(const char* s) {
    if (!s) return 0;
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

static int k_streq(const char* a, const char* b) {
    if (!a || !b) return (a == b);
    size_t i = 0;
    while (a[i] && b[i]) {
        if (a[i] != b[i]) return 0;
        i++;
    }
    return a[i] == b[i];
}

static void k_strcpy_truncate(char* dst, const char* src, size_t dst_size) {
    if (!dst || dst_size == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    size_t i = 0;
    while (src[i] && i < dst_size - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void k_memcpy(void* d, const void* s, size_t n) {
    if (!d || !s || n == 0) return;
    uint8_t* dd = (uint8_t*)d;
    const uint8_t* ss = (const uint8_t*)s;
    for (size_t i = 0; i < n; i++) dd[i] = ss[i];
}

static void k_memset(void* d, int val, size_t n) {
    if (!d || n == 0) return;
    uint8_t* dd = (uint8_t*)d;
    for (size_t i = 0; i < n; i++) dd[i] = (uint8_t)val;
}

void fs_init(void) {
    fs_lock_depth = 0;
    spin_init(&fs_spinlock);
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
    if (!name || parent_idx < 0 || parent_idx >= FS_MAX_NODES) return -1;
    for (int i = 0; i < FS_MAX_NODES; i++) {
        if (nodes[i].type != FS_TYPE_FREE &&
            nodes[i].parent == parent_idx &&
            k_streq(nodes[i].name, name))
            return i;
    }
    return -1;
}

static int fs_is_valid_name(const char* name) {
    if (!name || name[0] == '\0') return 0;
    if (k_streq(name, ".") || k_streq(name, "..")) return 0;
    if (k_strlen(name) >= FS_NAME_MAX) return 0;
    return 1;
}

enum fs_result fs_mkdir(const char* name) {
    if (!name) return FS_ERR_INVALID_NAME;
    fs_lock();
    enum fs_result r = FS_OK;
    if (!fs_is_valid_name(name)) { r = FS_ERR_INVALID_NAME; goto out; }
    if (fs_find_child(cwd, name) != -1) { r = FS_ERR_ALREADY_EXISTS; goto out; }
    int idx = fs_alloc_node();
    if (idx == -1) { r = FS_ERR_NO_SPACE; goto out; }
    nodes[idx].type = FS_TYPE_DIR;
    k_strcpy_truncate(nodes[idx].name, name, FS_NAME_MAX);
    nodes[idx].parent = cwd;
out:
    fs_unlock();
    return r;
}

enum fs_result fs_touch(const char* name) {
    if (!name) return FS_ERR_INVALID_NAME;
    fs_lock();
    enum fs_result r = FS_OK;
    if (!fs_is_valid_name(name)) { r = FS_ERR_INVALID_NAME; goto out; }
    if (fs_find_child(cwd, name) != -1) { r = FS_ERR_ALREADY_EXISTS; goto out; }
    int idx = fs_alloc_node();
    if (idx == -1) { r = FS_ERR_NO_SPACE; goto out; }
    nodes[idx].type = FS_TYPE_FILE;
    k_strcpy_truncate(nodes[idx].name, name, FS_NAME_MAX);
    nodes[idx].parent = cwd;
    nodes[idx].content_len = 0;
    nodes[idx].content[0] = '\0';
out:
    fs_unlock();
    return r;
}

enum fs_result fs_cd(const char* name) {
    if (!name || !name[0]) return FS_ERR_NOT_FOUND;
    fs_lock();
    enum fs_result r = FS_OK;
    if (k_streq(name, ".")) goto out;
    if (k_streq(name, "..")) {
        if (nodes[cwd].parent != -1)
            cwd = nodes[cwd].parent;
        goto out;
    }
    if (k_streq(name, "/")) {
        cwd = FS_ROOT_INDEX;
        goto out;
    }
    int idx = fs_find_child(cwd, name);
    if (idx == -1) { r = FS_ERR_NOT_FOUND; goto out; }
    if (nodes[idx].type != FS_TYPE_DIR) { r = FS_ERR_NOT_A_DIRECTORY; goto out; }
    cwd = idx;
out:
    fs_unlock();
    return r;
}

/* Free one node slot (must already have no live children if not bulk-free). */
static void fs_free_slot(int idx) {
    if (idx < 0 || idx >= FS_MAX_NODES) return;
    nodes[idx].type = FS_TYPE_FREE;
    nodes[idx].content_len = 0;
    nodes[idx].content[0] = '\0';
    nodes[idx].name[0] = '\0';
    nodes[idx].parent = -1;
}

/* True if cwd is idx or a descendant of idx. */
static int fs_cwd_in_subtree(int idx) {
    int walk = cwd;
    int depth = 0;
    while (walk != -1 && depth++ < FS_MAX_NODES) {
        if (walk == idx) return 1;
        walk = nodes[walk].parent;
    }
    return 0;
}

/*
 * Optimized subtree delete:
 * 1) BFS collect all nodes in subtree
 * 2) Fix cwd once if inside tree
 * 3) Free all collected slots in reverse order
 */
static enum fs_result fs_rm_subtree(int root_idx) {
    if (root_idx < 0 || root_idx >= FS_MAX_NODES) return FS_ERR_NOT_FOUND;
    if (nodes[root_idx].type == FS_TYPE_FREE) return FS_OK;
    if (root_idx == FS_ROOT_INDEX) return FS_ERR_INVALID_NAME;

    int queue[FS_MAX_NODES];
    int qh = 0, qt = 0;
    uint8_t seen[FS_MAX_NODES];
    for (int i = 0; i < FS_MAX_NODES; i++) seen[i] = 0;

    queue[qt++] = root_idx;
    seen[root_idx] = 1;

    while (qh < qt) {
        int u = queue[qh++];
        if (nodes[u].type != FS_TYPE_DIR) continue;
        for (int i = 0; i < FS_MAX_NODES; i++) {
            if (nodes[i].type == FS_TYPE_FREE) continue;
            if (nodes[i].parent != u) continue;
            if (seen[i]) continue;
            if (qt >= FS_MAX_NODES) return FS_ERR_NO_SPACE;
            seen[i] = 1;
            queue[qt++] = i;
        }
    }

    if (fs_cwd_in_subtree(root_idx)) {
        int p = nodes[root_idx].parent;
        cwd = (p >= 0) ? p : FS_ROOT_INDEX;
    }

    /* Free in reverse BFS order (children before parents). */
    for (int i = qt - 1; i >= 0; i--)
        fs_free_slot(queue[i]);

    return FS_OK;
}

/* Non-recursive: files OK; empty dirs OK; non-empty dir -> DIR_NOT_EMPTY */
enum fs_result fs_rm(const char* name) {
    if (!name || !name[0]) return FS_ERR_INVALID_NAME;
    if (name[0] == '/' && name[1] == '\0') return FS_ERR_INVALID_NAME;
    fs_lock();
    enum fs_result r = FS_OK;
    int idx = fs_find_child(cwd, name);
    if (idx == -1) { r = FS_ERR_NOT_FOUND; goto out; }
    if (nodes[idx].type == FS_TYPE_DIR) {
        for (int i = 0; i < FS_MAX_NODES; i++) {
            if (nodes[i].type != FS_TYPE_FREE && nodes[i].parent == idx) {
                r = FS_ERR_DIR_NOT_EMPTY;
                goto out;
            }
        }
    }
    r = fs_rm_subtree(idx);
out:
    fs_unlock();
    return r;
}

/* Recursive: rm -r — delete file or entire directory tree */
enum fs_result fs_rm_rf(const char* name) {
    if (!name || !name[0]) return FS_ERR_INVALID_NAME;
    if (name[0] == '/' && name[1] == '\0') return FS_ERR_INVALID_NAME;
    fs_lock();
    enum fs_result r = FS_OK;
    int idx = fs_find_child(cwd, name);
    if (idx == -1) { r = FS_ERR_NOT_FOUND; goto out; }
    r = fs_rm_subtree(idx);
out:
    fs_unlock();
    return r;
}

enum fs_result fs_write(const char* name, const char* text) {
    if (!name || !name[0] || !text) return FS_ERR_INVALID_NAME;
    return fs_write_bin(name, text, k_strlen(text));
}

enum fs_result fs_write_bin(const char* name, const void* data, size_t len) {
    if (!name || !name[0]) return FS_ERR_INVALID_NAME;
    if (len > 0 && !data) return FS_ERR_INVALID_NAME;
    if (len >= FS_FILE_MAX) return FS_ERR_TOO_BIG;

    fs_lock();
    enum fs_result r = FS_OK;
    int idx = fs_find_child(cwd, name);
    if (idx == -1) {
        if (!fs_is_valid_name(name)) { r = FS_ERR_INVALID_NAME; goto out; }
        idx = fs_alloc_node();
        if (idx == -1) { r = FS_ERR_NO_SPACE; goto out; }
        nodes[idx].type = FS_TYPE_FILE;
        k_strcpy_truncate(nodes[idx].name, name, FS_NAME_MAX);
        nodes[idx].parent = cwd;
        nodes[idx].content_len = 0;
        nodes[idx].content[0] = '\0';
    }
    if (nodes[idx].type != FS_TYPE_FILE) { r = FS_ERR_IS_A_DIRECTORY; goto out; }
    k_memcpy(nodes[idx].content, data, len);
    nodes[idx].content[len] = '\0';
    nodes[idx].content_len = len;
out:
    fs_unlock();
    return r;
}

enum fs_result fs_write_at(const char* name, size_t pos, const void* data, size_t len) {
    if (!name || !name[0]) return FS_ERR_INVALID_NAME;
    if (len > 0 && !data) return FS_ERR_INVALID_NAME;
    if (pos >= FS_FILE_MAX || len >= FS_FILE_MAX || pos + len >= FS_FILE_MAX)
        return FS_ERR_TOO_BIG;

    fs_lock();
    enum fs_result r = FS_OK;
    int idx = fs_find_child(cwd, name);
    if (idx == -1) {
        if (!fs_is_valid_name(name)) { r = FS_ERR_INVALID_NAME; goto out; }
        idx = fs_alloc_node();
        if (idx == -1) { r = FS_ERR_NO_SPACE; goto out; }
        nodes[idx].type = FS_TYPE_FILE;
        k_strcpy_truncate(nodes[idx].name, name, FS_NAME_MAX);
        nodes[idx].parent = cwd;
        nodes[idx].content_len = 0;
        nodes[idx].content[0] = '\0';
    }
    if (nodes[idx].type != FS_TYPE_FILE) { r = FS_ERR_IS_A_DIRECTORY; goto out; }
    if (pos > nodes[idx].content_len) {
        k_memset(nodes[idx].content + nodes[idx].content_len, 0, pos - nodes[idx].content_len);
    }
    k_memcpy(nodes[idx].content + pos, data, len);
    if (pos + len > nodes[idx].content_len) {
        nodes[idx].content_len = pos + len;
        nodes[idx].content[nodes[idx].content_len] = '\0';
    }
out:
    fs_unlock();
    return r;
}

enum fs_result fs_append(const char* name, const char* text) {
    if (!name || !name[0] || !text) return FS_ERR_INVALID_NAME;
    fs_lock();
    enum fs_result r = FS_OK;
    int idx = fs_find_child(cwd, name);
    if (idx == -1) {
        if (!fs_is_valid_name(name)) { r = FS_ERR_INVALID_NAME; goto out; }
        idx = fs_alloc_node();
        if (idx == -1) { r = FS_ERR_NO_SPACE; goto out; }
        nodes[idx].type = FS_TYPE_FILE;
        k_strcpy_truncate(nodes[idx].name, name, FS_NAME_MAX);
        nodes[idx].parent = cwd;
        nodes[idx].content_len = 0;
        nodes[idx].content[0] = '\0';
    }
    if (nodes[idx].type != FS_TYPE_FILE) { r = FS_ERR_IS_A_DIRECTORY; goto out; }
    size_t pos = nodes[idx].content_len;
    if (pos > 0 && pos < FS_FILE_MAX - 1)
        nodes[idx].content[pos++] = '\n';
    size_t i = 0;
    while (text[i] && pos < FS_FILE_MAX - 1)
        nodes[idx].content[pos++] = text[i++];
    nodes[idx].content[pos] = '\0';
    nodes[idx].content_len = pos;
out:
    fs_unlock();
    return r;
}

enum fs_result fs_read(const char* name, const char** out_content, size_t* out_len) {
    if (!name || !out_content || !out_len) return FS_ERR_NOT_FOUND;
    fs_lock();
    enum fs_result r = FS_OK;
    int idx = fs_find_child(cwd, name);
    if (idx == -1) { r = FS_ERR_NOT_FOUND; goto out; }
    if (nodes[idx].type != FS_TYPE_FILE) { r = FS_ERR_IS_A_DIRECTORY; goto out; }
    *out_content = nodes[idx].content;
    *out_len = nodes[idx].content_len;
out:
    fs_unlock();
    return r;
}

enum fs_result fs_read_path(const char* path, const char** out_content, size_t* out_len) {
    if (!path || !path[0] || !out_content || !out_len) return FS_ERR_NOT_FOUND;

    fs_lock();
    enum fs_result r = FS_OK;
    int dir = (path[0] == '/') ? FS_ROOT_INDEX : cwd;
    const char* p = path;
    if (*p == '/') p++;

    char component[FS_NAME_MAX];
    while (*p) {
        size_t n = 0;
        while (p[n] && p[n] != '/') {
            if (n + 1 < FS_NAME_MAX) component[n] = p[n];
            n++;
        }
        if (n >= FS_NAME_MAX) { r = FS_ERR_INVALID_NAME; goto out; }
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
        if (idx == -1) { r = FS_ERR_NOT_FOUND; goto out; }

        if (*p == '\0') {
            if (nodes[idx].type != FS_TYPE_FILE) { r = FS_ERR_IS_A_DIRECTORY; goto out; }
            *out_content = nodes[idx].content;
            *out_len = nodes[idx].content_len;
            r = FS_OK;
            goto out;
        }
        if (nodes[idx].type != FS_TYPE_DIR) { r = FS_ERR_NOT_A_DIRECTORY; goto out; }
        dir = idx;
    }
    r = FS_ERR_NOT_FOUND;
out:
    fs_unlock();
    return r;
}

enum fs_result fs_mkdir_p(const char* path) {
    if (!path || path[0] != '/') return FS_ERR_INVALID_NAME;

    fs_lock();
    enum fs_result r = FS_OK;
    int dir = FS_ROOT_INDEX;
    const char* p = path + 1;
    char component[FS_NAME_MAX];

    while (*p) {
        size_t n = 0;
        while (p[n] && p[n] != '/') {
            if (n + 1 < FS_NAME_MAX) component[n] = p[n];
            n++;
        }
        if (n >= FS_NAME_MAX) { r = FS_ERR_INVALID_NAME; goto out; }
        component[n] = '\0';
        p += n;
        if (*p == '/') p++;
        if (!component[0]) continue;

        int idx = fs_find_child(dir, component);
        if (idx == -1) {
            if (!fs_is_valid_name(component)) { r = FS_ERR_INVALID_NAME; goto out; }
            idx = fs_alloc_node();
            if (idx == -1) { r = FS_ERR_NO_SPACE; goto out; }
            nodes[idx].type = FS_TYPE_DIR;
            k_strcpy_truncate(nodes[idx].name, component, FS_NAME_MAX);
            nodes[idx].parent = dir;
        }
        if (nodes[idx].type != FS_TYPE_DIR) { r = FS_ERR_NOT_A_DIRECTORY; goto out; }
        dir = idx;
    }
out:
    fs_unlock();
    return r;
}

enum fs_result fs_cp(const char* src, const char* dst) {
    if (!src || !dst) return FS_ERR_INVALID_NAME;
    fs_lock();
    enum fs_result r = FS_OK;
    int sidx = fs_find_child(cwd, src);
    if (sidx == -1) { r = FS_ERR_NOT_FOUND; goto out; }
    if (nodes[sidx].type != FS_TYPE_FILE) { r = FS_ERR_IS_A_DIRECTORY; goto out; }
    if (!fs_is_valid_name(dst)) { r = FS_ERR_INVALID_NAME; goto out; }
    if (fs_find_child(cwd, dst) != -1) { r = FS_ERR_ALREADY_EXISTS; goto out; }
    int didx = fs_alloc_node();
    if (didx == -1) { r = FS_ERR_NO_SPACE; goto out; }
    nodes[didx].type = FS_TYPE_FILE;
    k_strcpy_truncate(nodes[didx].name, dst, FS_NAME_MAX);
    nodes[didx].parent = cwd;
    k_memcpy(nodes[didx].content, nodes[sidx].content, nodes[sidx].content_len);
    nodes[didx].content_len = nodes[sidx].content_len;
    nodes[didx].content[nodes[didx].content_len] = '\0';
out:
    fs_unlock();
    return r;
}

enum fs_result fs_mv(const char* src, const char* dst) {
    if (!src || !dst) return FS_ERR_INVALID_NAME;
    fs_lock();
    enum fs_result r = FS_OK;
    int sidx = fs_find_child(cwd, src);
    if (sidx == -1) { r = FS_ERR_NOT_FOUND; goto out; }
    if (!fs_is_valid_name(dst)) { r = FS_ERR_INVALID_NAME; goto out; }
    if (fs_find_child(cwd, dst) != -1) { r = FS_ERR_ALREADY_EXISTS; goto out; }
    k_strcpy_truncate(nodes[sidx].name, dst, FS_NAME_MAX);
out:
    fs_unlock();
    return r;
}

enum fs_result fs_size(const char* name, size_t* out_size) {
    if (!name || !out_size) return FS_ERR_NOT_FOUND;
    fs_lock();
    enum fs_result r = FS_OK;
    int idx = fs_find_child(cwd, name);
    if (idx == -1) { r = FS_ERR_NOT_FOUND; goto out; }
    if (nodes[idx].type == FS_TYPE_DIR) {
        *out_size = 0;
        r = FS_ERR_IS_A_DIRECTORY;
        goto out;
    }
    *out_size = nodes[idx].content_len;
out:
    fs_unlock();
    return r;
}

int fs_node_count(void) {
    fs_lock();
    int n = 0;
    for (int i = 0; i < FS_MAX_NODES; i++)
        if (nodes[i].type != FS_TYPE_FREE) n++;
    fs_unlock();
    return n;
}

int fs_cwd_entry(int index, char* name_out, size_t name_sz, int* is_dir) {
    if (!name_out || name_sz == 0) return -1;
    fs_lock();
    int n = 0;
    for (int i = 0; i < FS_MAX_NODES; i++) {
        if (nodes[i].type == FS_TYPE_FREE) continue;
        if (nodes[i].parent != cwd) continue;
        if (n == index) {
            k_strcpy_truncate(name_out, nodes[i].name, name_sz);
            if (is_dir) *is_dir = (nodes[i].type == FS_TYPE_DIR) ? 1 : 0;
            fs_unlock();
            return 0;
        }
        n++;
    }
    fs_unlock();
    return -1;
}

void fs_ls(void) {
    fs_lock();
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
    fs_unlock();
}

void fs_pwd(char* buffer, size_t buffer_size) {
    if (!buffer || buffer_size == 0) return;
    fs_lock();
    int path[FS_MAX_DEPTH];
    int depth = 0;
    int cur = cwd;
    while (cur != -1 && depth < FS_MAX_DEPTH) {
        path[depth++] = cur;
        cur = nodes[cur].parent;
    }
    size_t pos = 0;
    if (depth <= 1) {
        k_strcpy_truncate(buffer, "/", buffer_size);
        fs_unlock();
        return;
    }
    for (int i = depth - 2; i >= 0; i--) {
        const char* name = nodes[path[i]].name;
        size_t name_len = k_strlen(name);
        if (pos + 1 < buffer_size) buffer[pos++] = '/';
        for (size_t j = 0; j < name_len && pos + 1 < buffer_size; j++)
            buffer[pos++] = name[j];
    }
    if (pos >= buffer_size) pos = buffer_size - 1;
    buffer[pos] = '\0';
    fs_unlock();
}

const char* fs_strerror(enum fs_result err) {
    switch (err) {
        case FS_OK: return "OK";
        case FS_ERR_NOT_FOUND: return "No such file or directory";
        case FS_ERR_ALREADY_EXISTS: return "Already exists";
        case FS_ERR_NOT_A_DIRECTORY: return "Not a directory";
        case FS_ERR_IS_A_DIRECTORY: return "Is a directory, not a file";
        case FS_ERR_NO_SPACE: return "No space left";
        case FS_ERR_DIR_NOT_EMPTY: return "Directory not empty";
        case FS_ERR_INVALID_NAME: return "Invalid name";
        case FS_ERR_TOO_BIG: return "File too big";
        default: return "Unknown error";
    }
}
