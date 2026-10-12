/**
  * Minimal preemptive kernel for the Cortex-M4.
  *  - Fixed-priority scheduling (higher number = higher priority), round-robin between equal priorities.
  *  - Every task has its own stack and runs on PSP; exceptions run on MSP.
  *  - Context switch in PendSV, 1 ms tick driven from SysTick_Handler via os_tick().
  */
#ifndef OS_SCHED_H
#define OS_SCHED_H

#include <stdint.h>

#define OS_MAX_TASKS 8U /* includes the idle task */

typedef void (*os_task_fn)(void *arg);

/* Prepares the kernel and creates the idle task (priority 0). Call once before creating tasks. */
void os_init(void);

/* Creates a task on a caller-supplied stack. Returns a task id, or -1 if the table is full.
   Priority 1..255; 0 is reserved for idle. */
int  os_task_create(os_task_fn fn, void *arg, uint8_t prio, uint32_t *stack, uint32_t stack_words);

/* Starts the scheduler. Never returns. */
void os_start(void);

/* Blocks the calling task for ms milliseconds (0 = just yield). */
void os_delay(uint32_t ms);

/* Lets other ready tasks of equal or higher priority run. */
void os_yield(void);

/* Removes a task (id < 0 = the calling task). */
void os_task_delete(int id);

/* 1 ms tick counter of the kernel. */
uint32_t os_ticks(void);

/* 1 once os_start() has run. */
int  os_is_running(void);

/* Must be called from SysTick_Handler every 1 ms. */
void os_tick(void);

#endif /* OS_SCHED_H */
