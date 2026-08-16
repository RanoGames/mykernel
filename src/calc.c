/* calc.c — простой калькулятор арифметических выражений.
 *
 * Используется классическая техника "рекурсивного спуска" (recursive
 * descent parser) — функции вызывают друг друга по уровням приоритета:
 *
 *   parse_expr   — обрабатывает + и -           (самый низкий приоритет)
 *   parse_term    — обрабатывает * и /            (средний приоритет)
 *   parse_power   — обрабатывает ^ (степень)      (высокий приоритет, правоассоциативно)
 *   parse_factor   — числа, скобки, унарный минус   (самый высокий приоритет)
 *
 * Именно из-за такого порядка вызовов в выражении "2 + 3 * 4^2"
 * сначала посчитается 4^2, потом умножение, потом сложение.
 * Степень правоассоциативна: 2^3^2 = 2^(3^2) = 512. */

#include "calc.h"

struct calc_parser {
    const char* p;  /* текущая позиция чтения в строке выражения */
    int error;       /* флаг ошибки разбора/вычисления */
};

static void skip_spaces(struct calc_parser* pr) {
    while (*pr->p == ' ' || *pr->p == '\t')
        pr->p++;
}

/* Целочисленное возведение в степень (только неотрицательный показатель).
 * Отрицательная степень — ошибка (у нас нет дробных чисел). */
static long ipow(long base, long exp, int* error) {
    if (exp < 0) {
        *error = 1;
        return 0;
    }

    long result = 1;
    while (exp > 0) {
        if (exp & 1)
            result *= base;
        base *= base;
        exp >>= 1;
    }
    return result;
}

static long parse_expr(struct calc_parser* pr); /* объявление вперёд */

/* factor: число, (выражение в скобках), или унарный минус/плюс */
static long parse_factor(struct calc_parser* pr) {
    skip_spaces(pr);

    if (*pr->p == '-') {
        pr->p++;
        return -parse_factor(pr);
    }

    if (*pr->p == '+') {
        pr->p++;
        return parse_factor(pr);
    }

    if (*pr->p == '(') {
        pr->p++;
        long value = parse_expr(pr);
        skip_spaces(pr);
        if (*pr->p != ')') {
            pr->error = 1;
            return 0;
        }
        pr->p++;
        return value;
    }

    if (*pr->p < '0' || *pr->p > '9') {
        pr->error = 1;
        return 0;
    }

    long value = 0;
    while (*pr->p >= '0' && *pr->p <= '9') {
        value = value * 10 + (*pr->p - '0');
        pr->p++;
    }
    return value;
}

/* power: возведение в степень, правоассоциативно (2^3^2 = 2^(3^2)) */
static long parse_power(struct calc_parser* pr) {
    long base = parse_factor(pr);

    skip_spaces(pr);
    if (*pr->p == '^') {
        pr->p++;
        long exp = parse_power(pr);          /* рекурсия → правоассоциативность */
        base = ipow(base, exp, &pr->error);
    }
    return base;
}

/* term: цепочки умножения/деления */
static long parse_term(struct calc_parser* pr) {
    long value = parse_power(pr);

    for (;;) {
        skip_spaces(pr);
        if (*pr->p == '*') {
            pr->p++;
            long rhs = parse_power(pr);
            value *= rhs;
        } else if (*pr->p == '/') {
            pr->p++;
            long rhs = parse_power(pr);
            if (rhs == 0) {
                pr->error = 1;
                return 0;
            }
            value /= rhs;
        } else {
            break;
        }
    }
    return value;
}

/* expr: цепочки сложения/вычитания */
static long parse_expr(struct calc_parser* pr) {
    long value = parse_term(pr);

    for (;;) {
        skip_spaces(pr);
        if (*pr->p == '+') {
            pr->p++;
            value += parse_term(pr);
        } else if (*pr->p == '-') {
            pr->p++;
            value -= parse_term(pr);
        } else {
            break;
        }
    }
    return value;
}

long calc_eval(const char* expr, int* out_error) {
    struct calc_parser pr = { expr, 0 };

    long result = parse_expr(&pr);

    skip_spaces(&pr);
    if (*pr.p != '\0')
        pr.error = 1;

    *out_error = pr.error;
    return result;
}
