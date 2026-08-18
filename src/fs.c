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

/* Use for short critical sections in mutators */

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
    nodes[idx].type = FS_TYPE_FREE;
    nodes[idx].content_len = 0;
    nodes[idx].content[0] = '\0';
    nodes[idx].name[0] = '\0';
    nodes[idx].parent = -1;
}

/* True if cwd is idx or a descendant of idx. */
static int fs_cwd_in_subtree(int idx) {
    int walk = cwd;
    while (walk != -1) {
        if (walk == idx) return 1;
        walk = nodes[walk].parent;
    }
    return 0;
}

/*
 * Optimized subtree delete for large trees:
 * 1) BFS collect all nodes in subtree (one scan fan-out, O(n) for n=FS_MAX_NODES)
 * 2) Fix cwd once if inside tree
 * 3) Free all collected slots (no per-child full rescan restart)
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

/* Non-recursive: files OK; empty dirs OK; non-empty dir → DIR_NOT_EMPTY */
enum fs_result fs_rm(const char* name) {
    fs_lock();
    enum fs_result r = FS_OK;
    if (!name || !name[0]) { r = FS_ERR_INVALID_NAME; goto out; }
    if (name[0] == '/' && name[1] == '\0') { r = FS_ERR_INVALID_NAME; goto out; }
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
    fs_lock();
    enum fs_result r = FS_OK;
    if (!name || !name[0]) { r = FS_ERR_INVALID_NAME; goto out; }
    if (name[0] == '/' && name[1] == '\0') { r = FS_ERR_INVALID_NAME; goto out; }
    int idx = fs_find_child(cwd, name);
    if (idx == -1) { r = FS_ERR_NOT_FOUND; goto out; }
    r = fs_rm_subtree(idx);
out:
    fs_unlock();
    return r;
}

enum fs_result fs_write(const char* name, const char* text) {
    return fs_write_bin(name, text, k_strlen(text));
}

enum fs_result fs_write_bin(const char* name, const void* data, size_t len) {
    fs_lock();
    enum fs_result r = FS_OK;
    if (len >= FS_FILE_MAX) { r = FS_ERR_TOO_BIG; goto out; }
    int idx = fs_find_child(cwd, name);
    if (idx == -1) {
        fs_unlock(); /* touch takes its own lock */
        r = fs_touch(name);
        fs_lock();
        if (r != FS_OK) goto out;
        idx = fs_find_child(cwd, name);
    }
    if (idx == -1) { r = FS_ERR_NOT_FOUND; goto out; }
    if (nodes[idx].type != FS_TYPE_FILE) { r = FS_ERR_IS_A_DIRECTORY; goto out; }
    k_memcpy(nodes[idx].content, data, len);
    nodes[idx].content[len] = '\0';
    nodes[idx].content_len = len;
out:
    fs_unlock();
    return r;
}


enum fs_result fs_append(const char* name, const char* text) {
    fs_lock();
    enum fs_result r = FS_OK;
    int idx = fs_find_child(cwd, name);
    if (idx == -1) {
        fs_unlock();
        r = fs_touch(name);
        fs_lock();
        if (r != FS_OK) goto out;
        idx = fs_find_child(cwd, name);
    }
    if (idx == -1) { r = FS_ERR_NOT_FOUND; goto out; }
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
