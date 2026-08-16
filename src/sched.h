/* sched.h — простой вытесняющий планировщик (round-robin) */

#ifndef SCHED_H
#define SCHED_H

#include <stdint.h>
#include <stddef.h>

#define SCHED_MAX_TASKS 8
#define SCHED_STACK_SIZE 4096

enum task_state {
    TASK_FREE = 0,
    TASK_READY,
    TASK_RUNNING,
    TASK_BLOCKED,
    TASK_ZOMBIE,
};

typedef void (*task_entry_t)(void* arg);

struct task {
    int id;
    enum task_state state;
    char name[16];
    uint32_t* sp;
    uint8_t* stack_base;
    task_entry_t entry;
    void* arg;
    uint32_t ticks_ran;
};

void sched_init(void);
int  sched_spawn(const char* name, task_entry_t entry, void* arg);
void sched_yield(void);
void sched_on_tick(void);
void sched_exit(void);

struct task* sched_current(void);
int  sched_task_count(void);
void sched_list(void);

/* демо-воркер (счётчик в фоне) */
int  sched_spawn_worker(void);
uint32_t sched_worker_counter(void);

#endif
