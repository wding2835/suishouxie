#include "main.h"
#include "os_sched.h"

typedef struct
{
  os_task_fn fn;
  void *arg;
  uint32_t period;
  uint32_t next_run;
} os_task_t;

static os_task_t tasks[OS_MAX_TASKS];

int os_task_create(os_task_fn fn, void *arg, uint32_t period_ms)
{
  for (uint32_t i = 0; i < OS_MAX_TASKS; i++)
  {
    if (tasks[i].fn == NULL)
    {
      tasks[i].arg = arg;
      tasks[i].period = period_ms;
      tasks[i].next_run = HAL_GetTick() + period_ms;
      tasks[i].fn = fn;
      return (int)i;
    }
  }
  return -1;
}

void os_task_delete(int id)
{
  if (id >= 0 && id < (int)OS_MAX_TASKS)
  {
    tasks[id].fn = NULL;
  }
}

void os_poll(void)
{
  uint32_t now = HAL_GetTick();

  for (uint32_t i = 0; i < OS_MAX_TASKS; i++)
  {
    /* Signed difference keeps this correct when the tick counter wraps */
    if (tasks[i].fn != NULL && (int32_t)(now - tasks[i].next_run) >= 0)
    {
      tasks[i].next_run += tasks[i].period;
      tasks[i].fn(tasks[i].arg);
    }
  }
}
