/* sched.c — round-robin, вытеснение по таймеру */

#include "sched.h"
#include "kmalloc.h"
#include "vga.h"
#include <stdint.h>

extern void context_switch(uint32_t** old_sp, uint32_t* new_sp);

static struct task tasks[SCHED_MAX_TASKS];
static struct task* current;
static int sched_enabled;
static int ticks_slice;
static volatile uint32_t g_worker_counter;

static void str_copy16(char* d, const char* s) {
    int i = 0;
    while (s && s[i] && i < 15) { d[i] = s[i]; i++; }
    d[i] = '\0';
}

static void task_exit_stub(void) {
    sched_exit();
    for (;;) __asm__ volatile ("hlt");
}

static void task_bootstrap(void) {
    task_entry_t fn = current->entry;
    void* arg = current->arg;
    fn(arg);
    task_exit_stub();
}

void sched_init(void) {
    for (int i = 0; i < SCHED_MAX_TASKS; i++) {
        tasks[i].state = TASK_FREE;
        tasks[i].id = i;
        tasks[i].sp = 0;
        tasks[i].stack_base = 0;
        tasks[i].ticks_ran = 0;
        tasks[i].name[0] = '\0';
    }
    current = &tasks[0];
    current->state = TASK_RUNNING;
    str_copy16(current->name, "shell");
    current->entry = 0;
    current->arg = 0;
    current->stack_base = 0;
    current->sp = 0;
    sched_enabled = 1;
    ticks_slice = 0;
    g_worker_counter = 0;
}

static struct task* pick_next(void) {
    int start = current ? current->id : 0;
    for (int i = 1; i <= SCHED_MAX_TASKS; i++) {
        int idx = (start + i) % SCHED_MAX_TASKS;
        if (tasks[idx].state == TASK_READY)
            return &tasks[idx];
    }
    if (current && current->state == TASK_RUNNING)
        return current;
    return current;
}

static void schedule(void) {
    if (!sched_enabled || !current)
        return;
    struct task* next = pick_next();
    if (!next || next == current)
        return;
    struct task* prev = current;
    if (prev->state == TASK_RUNNING)
        prev->state = TASK_READY;
    next->state = TASK_RUNNING;
    current = next;
    ticks_slice = 0;
    context_switch(&prev->sp, next->sp);
}

int sched_spawn(const char* name, task_entry_t entry, void* arg) {
    struct task* t = 0;
    for (int i = 1; i < SCHED_MAX_TASKS; i++) {
        if (tasks[i].state == TASK_FREE) { t = &tasks[i]; break; }
    }
    if (!t) return -1;

    uint8_t* stack = (uint8_t*)kmalloc(SCHED_STACK_SIZE);
    if (!stack) return -1;

    t->stack_base = stack;
    t->entry = entry;
    t->arg = arg;
    t->ticks_ran = 0;
    str_copy16(t->name, name ? name : "task");

    uint32_t* sp = (uint32_t*)(stack + SCHED_STACK_SIZE);
    sp = (uint32_t*)((uint32_t)sp & ~0xFu);
    *--sp = (uint32_t)task_bootstrap;
    *--sp = 0; *--sp = 0; *--sp = 0; *--sp = 0;
    *--sp = 0; *--sp = 0; *--sp = 0; *--sp = 0;
    t->sp = sp;
    t->state = TASK_READY;
    return t->id;
}

void sched_yield(void) {
    if (!sched_enabled) return;
    __asm__ volatile ("cli");
    schedule();
    __asm__ volatile ("sti");
}

void sched_on_tick(void) {
    if (!sched_enabled || !current) return;
    current->ticks_ran++;
    ticks_slice++;
    if (ticks_slice >= 10) {
        ticks_slice = 0;
        struct task* next = pick_next();
        if (next && next != current) {
            struct task* prev = current;
            if (prev->state == TASK_RUNNING)
                prev->state = TASK_READY;
            next->state = TASK_RUNNING;
            current = next;
            context_switch(&prev->sp, next->sp);
        }
    }
}

void sched_exit(void) {
    __asm__ volatile ("cli");
    if (current->id == 0) {
        __asm__ volatile ("sti");
        return;
    }
    current->state = TASK_FREE;
    current->stack_base = 0;
    struct task* next = pick_next();
    if (!next) next = &tasks[0];
    struct task* prev = current;
    current = next;
    current->state = TASK_RUNNING;
    context_switch(&prev->sp, next->sp);
    __asm__ volatile ("sti");
}

struct task* sched_current(void) { return current; }

int sched_task_count(void) {
    int n = 0;
    for (int i = 0; i < SCHED_MAX_TASKS; i++)
        if (tasks[i].state != TASK_FREE) n++;
    return n;
}

void sched_list(void) {
    terminal_writestring("ID  STATE    TICKS  NAME\n");
    for (int i = 0; i < SCHED_MAX_TASKS; i++) {
        if (tasks[i].state == TASK_FREE) continue;
        terminal_write_uint((uint32_t)tasks[i].id);
        terminal_writestring("   ");
        switch (tasks[i].state) {
            case TASK_READY:   terminal_writestring("READY  "); break;
            case TASK_RUNNING: terminal_writestring("RUN    "); break;
            case TASK_BLOCKED: terminal_writestring("BLOCK  "); break;
            case TASK_ZOMBIE:  terminal_writestring("ZOMBIE "); break;
            default:           terminal_writestring("?      "); break;
        }
        terminal_write_uint(tasks[i].ticks_ran);
        terminal_writestring("  ");
        terminal_writestring(tasks[i].name);
        if (&tasks[i] == current) terminal_writestring(" *");
        terminal_putchar('\n');
    }
    terminal_writestring("worker counter: ");
    terminal_write_uint(g_worker_counter);
    terminal_putchar('\n');
}

static void worker_entry(void* arg) {
    (void)arg;
    for (;;) {
        g_worker_counter++;
        /* немного покрутиться и отдать CPU */
        for (volatile int i = 0; i < 50000; i++)
            ;
        sched_yield();
    }
}

int sched_spawn_worker(void) {
    return sched_spawn("worker", worker_entry, 0);
}

uint32_t sched_worker_counter(void) {
    return g_worker_counter;
}
