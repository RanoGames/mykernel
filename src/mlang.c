/* mlang.c — MyLang: простой императивный язык
 *
 * Синтаксис:
 *   # комментарий
 *   x = 2 + 3 * 4
 *   print x
 *   print "hello"
 *   if x > 5
 *     print "big"
 *   end
 *   while x > 0
 *     print x
 *     x = x - 1
 *   end
 */

#include "mlang.h"
#include "vga.h"
#include "calc.h"
#include <stddef.h>
#include <stdint.h>

#define MAX_VARS 32
#define MAX_NAME 16
#define MAX_LINE 128
#define MAX_LINES 64

struct var {
    char name[MAX_NAME];
    long value;
    int used;
};

static struct var vars[MAX_VARS];

static int str_eq(const char* a, const char* b) {
    while (*a && *b) {
        if (*a != *b) return 0;
        a++; b++;
    }
    return *a == *b;
}

static void str_copy(char* d, const char* s, size_t max) {
    size_t i = 0;
    while (s[i] && i < max - 1) { d[i] = s[i]; i++; }
    d[i] = '\0';
}

static const char* skip_ws(const char* p) {
    while (*p == ' ' || *p == '\t') p++;
    return p;
}

static int is_ident_start(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int is_ident(char c) {
    return is_ident_start(c) || (c >= '0' && c <= '9');
}

void mlang_reset(void) {
    for (int i = 0; i < MAX_VARS; i++)
        vars[i].used = 0;
}

static struct var* var_find(const char* name) {
    for (int i = 0; i < MAX_VARS; i++)
        if (vars[i].used && str_eq(vars[i].name, name))
            return &vars[i];
    return 0;
}

static struct var* var_set(const char* name, long value) {
    struct var* v = var_find(name);
    if (v) {
        v->value = value;
        return v;
    }
    for (int i = 0; i < MAX_VARS; i++) {
        if (!vars[i].used) {
            str_copy(vars[i].name, name, MAX_NAME);
            vars[i].value = value;
            vars[i].used = 1;
            return &vars[i];
        }
    }
    return 0;
}

/* Подставить имена переменных в выражение → буфер с числами */
static int expand_expr(const char* expr, char* out, size_t out_sz) {
    size_t o = 0;
    const char* p = expr;
    while (*p) {
        if (is_ident_start(*p)) {
            char name[MAX_NAME];
            size_t n = 0;
            while (is_ident(*p) && n < MAX_NAME - 1)
                name[n++] = *p++;
            name[n] = '\0';
            struct var* v = var_find(name);
            long val = v ? v->value : 0;
            /* число в out */
            char tmp[24];
            int ti = 0;
            unsigned long u;
            if (val < 0) {
                if (o + 1 >= out_sz) return -1;
                out[o++] = '-';
                u = (unsigned long)(-(val + 1)) + 1;
            } else {
                u = (unsigned long)val;
            }
            if (u == 0) {
                tmp[ti++] = '0';
            } else {
                while (u && ti < 20) {
                    tmp[ti++] = (char)('0' + (u % 10));
                    u /= 10;
                }
            }
            while (ti--) {
                if (o + 1 >= out_sz) return -1;
                out[o++] = tmp[ti];
            }
        } else {
            if (o + 1 >= out_sz) return -1;
            out[o++] = *p++;
        }
    }
    out[o] = '\0';
    return 0;
}

static int eval_expr(const char* expr, long* out) {
    char buf[128];
    if (expand_expr(expr, buf, sizeof(buf)) != 0)
        return -1;
    int err = 0;
    long v = calc_eval(buf, &err);
    if (err) return -1;
    *out = v;
    return 0;
}

/* сравнение: expr op expr, op is == != < > <= >= */
static int eval_condition(const char* cond, int* result) {
    const char* p = skip_ws(cond);
    /* ищем оператор */
    const char* op = 0;
    int oplen = 0;
    for (const char* q = p; *q; q++) {
        if ((q[0] == '=' && q[1] == '=') ||
            (q[0] == '!' && q[1] == '=') ||
            (q[0] == '<' && q[1] == '=') ||
            (q[0] == '>' && q[1] == '=')) {
            op = q; oplen = 2; break;
        }
        if (*q == '<' || *q == '>') {
            op = q; oplen = 1; break;
        }
    }
    if (!op) {
        long v;
        if (eval_expr(p, &v) != 0) return -1;
        *result = (v != 0);
        return 0;
    }

    char left[64], right[64];
    size_t li = 0;
    for (const char* q = p; q < op && li < sizeof(left) - 1; q++)
        left[li++] = *q;
    left[li] = '\0';
    const char* r = op + oplen;
    size_t ri = 0;
    while (*r && ri < sizeof(right) - 1)
        right[ri++] = *r++;
    right[ri] = '\0';

    long a, b;
    if (eval_expr(left, &a) != 0) return -1;
    if (eval_expr(right, &b) != 0) return -1;

    if (oplen == 2) {
        if (op[0] == '=' && op[1] == '=') *result = (a == b);
        else if (op[0] == '!' && op[1] == '=') *result = (a != b);
        else if (op[0] == '<' && op[1] == '=') *result = (a <= b);
        else if (op[0] == '>' && op[1] == '=') *result = (a >= b);
        else return -1;
    } else {
        if (op[0] == '<') *result = (a < b);
        else if (op[0] == '>') *result = (a > b);
        else return -1;
    }
    return 0;
}

static void print_long(long v) {
    terminal_write_int((int32_t)v);
}

int mlang_exec_line(const char* line) {
    const char* p = skip_ws(line);
    if (*p == '\0' || *p == '#')
        return 0;

    /* print "string" */
    if (p[0] == 'p' && p[1] == 'r' && p[2] == 'i' && p[3] == 'n' && p[4] == 't' &&
        (p[5] == ' ' || p[5] == '\t' || p[5] == '\0')) {
        p = skip_ws(p + 5);
        if (*p == '"') {
            p++;
            while (*p && *p != '"') {
                terminal_putchar(*p);
                p++;
            }
            terminal_putchar('\n');
            return 0;
        }
        long v;
        if (eval_expr(p, &v) != 0) {
            terminal_writestring("mlang: bad expression\n");
            return -1;
        }
        print_long(v);
        terminal_putchar('\n');
        return 0;
    }

    /* assignment: name = expr */
    if (is_ident_start(*p)) {
        char name[MAX_NAME];
        size_t n = 0;
        const char* q = p;
        while (is_ident(*q) && n < MAX_NAME - 1)
            name[n++] = *q++;
        name[n] = '\0';
        q = skip_ws(q);
        if (*q == '=') {
            q = skip_ws(q + 1);
            long v;
            if (eval_expr(q, &v) != 0) {
                terminal_writestring("mlang: bad expression\n");
                return -1;
            }
            if (!var_set(name, v)) {
                terminal_writestring("mlang: too many variables\n");
                return -1;
            }
            return 0;
        }
    }

    terminal_writestring("mlang: syntax error\n");
    return -1;
}

/* Разбор скрипта с if/while/end */
int mlang_exec_script(const char* text) {
    /* разобьём на строки */
    char lines[MAX_LINES][MAX_LINE];
    int nlines = 0;
    size_t i = 0;

    while (text[i] && nlines < MAX_LINES) {
        size_t j = 0;
        while (text[i] && text[i] != '\n' && j < MAX_LINE - 1)
            lines[nlines][j++] = text[i++];
        lines[nlines][j] = '\0';
        if (text[i] == '\n') i++;
        nlines++;
    }

    int pc = 0;
    while (pc < nlines) {
        const char* p = skip_ws(lines[pc]);

        if (*p == '\0' || *p == '#') {
            pc++;
            continue;
        }

        /* end — ошибка вне блока */
        if (str_eq(p, "end")) {
            terminal_writestring("mlang: unexpected end\n");
            return -1;
        }

        /* if cond */
        if (p[0] == 'i' && p[1] == 'f' && (p[2] == ' ' || p[2] == '\t')) {
            int cond = 0;
            if (eval_condition(skip_ws(p + 2), &cond) != 0) {
                terminal_writestring("mlang: bad condition\n");
                return -1;
            }
            /* найти matching end, собрать тело */
            int depth = 1;
            int body_start = pc + 1;
            int body_end = -1;
            for (int k = body_start; k < nlines; k++) {
                const char* t = skip_ws(lines[k]);
                if ((t[0] == 'i' && t[1] == 'f' && (t[2] == ' ' || t[2] == '\t')) ||
                    (t[0] == 'w' && t[1] == 'h' && t[2] == 'i' && t[3] == 'l' && t[4] == 'e' &&
                     (t[5] == ' ' || t[5] == '\t')))
                    depth++;
                else if (str_eq(t, "end")) {
                    depth--;
                    if (depth == 0) { body_end = k; break; }
                }
            }
            if (body_end < 0) {
                terminal_writestring("mlang: if without end\n");
                return -1;
            }
            if (cond) {
                for (int k = body_start; k < body_end; k++) {
                    const char* t = skip_ws(lines[k]);
                    /* вложенные if/while — рекурсивно через мини-скрипт */
                    if ((t[0] == 'i' && t[1] == 'f' && (t[2] == ' ' || t[2] == '\t')) ||
                        (t[0] == 'w' && t[1] == 'h' && t[2] == 'i' && t[3] == 'l' && t[4] == 'e')) {
                        /* собрать хвост от k до body_end как скрипт — упрощение:
                         * выполняем построчно только простые строки;
                         * для вложенности пересоберём подскрипт */
                        char sub[MAX_LINES * MAX_LINE];
                        size_t so = 0;
                        for (int m = k; m < body_end; m++) {
                            size_t len = 0;
                            while (lines[m][len]) len++;
                            if (so + len + 2 >= sizeof(sub)) break;
                            for (size_t c = 0; c < len; c++) sub[so++] = lines[m][c];
                            sub[so++] = '\n';
                        }
                        sub[so] = '\0';
                        if (mlang_exec_script(sub) != 0)
                            return -1;
                        break;
                    }
                    if (mlang_exec_line(lines[k]) != 0)
                        return -1;
                }
            }
            pc = body_end + 1;
            continue;
        }

        /* while cond */
        if (p[0] == 'w' && p[1] == 'h' && p[2] == 'i' && p[3] == 'l' && p[4] == 'e' &&
            (p[5] == ' ' || p[5] == '\t')) {
            int depth = 1;
            int body_start = pc + 1;
            int body_end = -1;
            for (int k = body_start; k < nlines; k++) {
                const char* t = skip_ws(lines[k]);
                if ((t[0] == 'i' && t[1] == 'f' && (t[2] == ' ' || t[2] == '\t')) ||
                    (t[0] == 'w' && t[1] == 'h' && t[2] == 'i' && t[3] == 'l' && t[4] == 'e' &&
                     (t[5] == ' ' || t[5] == '\t')))
                    depth++;
                else if (str_eq(t, "end")) {
                    depth--;
                    if (depth == 0) { body_end = k; break; }
                }
            }
            if (body_end < 0) {
                terminal_writestring("mlang: while without end\n");
                return -1;
            }

            for (int iter = 0; iter < 10000; iter++) {
                int cond = 0;
                if (eval_condition(skip_ws(p + 5), &cond) != 0) {
                    terminal_writestring("mlang: bad condition\n");
                    return -1;
                }
                if (!cond) break;
                for (int k = body_start; k < body_end; k++) {
                    if (mlang_exec_line(lines[k]) != 0)
                        return -1;
                }
            }
            pc = body_end + 1;
            continue;
        }

        if (mlang_exec_line(lines[pc]) != 0)
            return -1;
        pc++;
    }
    return 0;
}
