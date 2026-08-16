/* calc.c — простой калькулятор арифметических выражений.
 *
 * Используется классическая техника "рекурсивного спуска" (recursive
 * descent parser) — три функции, вызывающие друг друга по уровням
 * приоритета операций:
 *
 *   parse_expr   — обрабатывает + и -           (самый низкий приоритет)
 *   parse_term    — обрабатывает * и /            (выше приоритет)
 *   parse_factor   — числа, скобки, унарный минус   (самый высокий приоритет)
 *
 * Именно из-за такого порядка вызовов (expr зовёт term, term зовёт
 * factor) в выражении "2 + 3 * 4" сначала посчитается "3 * 4", а
 * потом прибавится "2" — то есть приоритет операций соблюдается
 * автоматически, без специальных проверок. Скобки обрабатываются
 * в parse_factor рекурсивным вызовом parse_expr — отсюда и название
 * техники. */

#include "calc.h"

struct calc_parser {
    const char* p;  /* текущая позиция чтения в строке выражения */
    int error;       /* флаг ошибки разбора/вычисления */
};

static void skip_spaces(struct calc_parser* pr) {
    while (*pr->p == ' ' || *pr->p == '\t')
        pr->p++;
}

static long parse_expr(struct calc_parser* pr); /* объявление вперёд — используется в parse_factor для скобок */

/* factor: число, (выражение в скобках), или унарный минус перед любым из этого */
static long parse_factor(struct calc_parser* pr) {
    skip_spaces(pr);

    if (*pr->p == '-') {
        pr->p++;
        return -parse_factor(pr); /* унарный минус — рекурсивно применяем к следующему фактору */
    }

    if (*pr->p == '+') {
        pr->p++;
        return parse_factor(pr); /* унарный плюс — просто пропускаем, ни на что не влияет */
    }

    if (*pr->p == '(') {
        pr->p++; /* съели открывающую скобку */
        long value = parse_expr(pr);
        skip_spaces(pr);
        if (*pr->p != ')') {
            pr->error = 1; /* не нашли закрывающую скобку — ошибка в выражении */
            return 0;
        }
        pr->p++; /* съели закрывающую скобку */
        return value;
    }

    if (*pr->p < '0' || *pr->p > '9') {
        pr->error = 1; /* ожидали цифру или скобку, а нашли что-то другое */
        return 0;
    }

    long value = 0;
    while (*pr->p >= '0' && *pr->p <= '9') {
        value = value * 10 + (*pr->p - '0');
        pr->p++;
    }
    return value;
}

/* term: разбирает цепочки умножения/деления, например "3 * 4 / 2" */
static long parse_term(struct calc_parser* pr) {
    long value = parse_factor(pr);

    for (;;) {
        skip_spaces(pr);
        if (*pr->p == '*') {
            pr->p++;
            long rhs = parse_factor(pr);
            value *= rhs;
        } else if (*pr->p == '/') {
            pr->p++;
            long rhs = parse_factor(pr);
            if (rhs == 0) {
                pr->error = 1; /* деление на 0 — считаем ошибкой вычисления */
                return 0;
            }
            value /= rhs;
        } else {
            break;
        }
    }
    return value;
}

/* expr: разбирает цепочки сложения/вычитания, например "1 + 2 - 3" */
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
        pr.error = 1; /* после разбора выражения остался "хвост" — значит, в нём была ошибка */

    *out_error = pr.error;
    return result;
}
