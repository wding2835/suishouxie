/**
  * Minimal cooperative scheduler: periodic tasks driven by the HAL 1 ms tick.
  * Tasks run to completion from os_poll(), so they must never block.
  */
#ifndef OS_SCHED_H
#define OS_SCHED_H

#include <stdint.h>

#define OS_MAX_TASKS 8U

typedef void (*os_task_fn)(void *arg);

/* Registers a task run every period_ms. Returns a task id, or -1 if the table is full. */
int  os_task_create(os_task_fn fn, void *arg, uint32_t period_ms);

/* Stops and frees a task. Safe to call from inside the task itself. */
void os_task_delete(int id);

/* Runs every task that is due. Call it continuously from the main loop. */
void os_poll(void);

#endif /* OS_SCHED_H */
