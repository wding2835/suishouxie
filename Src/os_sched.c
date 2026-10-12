#include "main.h"
#include "os_sched.h"

#define STACK_FILL 0xA5A5A5A5U

typedef enum { T_FREE = 0, T_READY, T_SLEEP } task_state_t;

typedef struct
{
  uint32_t sp; /* saved process stack pointer */
  uint32_t wake;
  uint8_t prio;
  uint8_t state;
} tcb_t;

static tcb_t tcbs[OS_MAX_TASKS];
static tcb_t boot_tcb;                  /* receives the context of the first, throw-away PendSV */
static tcb_t *cur = &boot_tcb;
static int cur_idx = -1;
static volatile uint32_t tick_count;
static volatile uint8_t running;

static uint32_t boot_stack[32] __attribute__((aligned(8)));
static uint32_t idle_stack[128] __attribute__((aligned(8)));

extern uint32_t _estack; /* top of RAM, defined by the linker script */

static void idle_task(void *arg)
{
  (void)arg;
  for (;;)
  {
    __WFI();
  }
}

static void os_task_exit(void)
{
  os_task_delete(-1);
}

void os_init(void)
{
  /* PendSV must be the lowest priority so it never preempts an interrupt handler */
  HAL_NVIC_SetPriority(PendSV_IRQn, 15, 0);
  os_task_create(idle_task, NULL, 0, idle_stack, sizeof(idle_stack) / sizeof(idle_stack[0]));
}

int os_task_create(os_task_fn fn, void *arg, uint8_t prio, uint32_t *stack, uint32_t stack_words)
{
  uint32_t primask = __get_PRIMASK();
  int id = -1;

  __disable_irq();
  for (uint32_t i = 0; i < OS_MAX_TASKS; i++)
  {
    if (tcbs[i].state == T_FREE)
    {
      id = (int)i;
      break;
    }
  }
  if (id >= 0)
  {
    uint32_t *sp;

    for (uint32_t w = 0; w < stack_words; w++)
    {
      stack[w] = STACK_FILL;
    }
    sp = (uint32_t *)(((uint32_t)(stack + stack_words)) & ~7U);

    /* Frame as PendSV expects to find it: hardware-stacked registers first (highest addresses) */
    *--sp = 0x01000000U;                   /* xPSR (Thumb bit) */
    *--sp = (uint32_t)fn & ~1U;            /* PC */
    *--sp = (uint32_t)os_task_exit;        /* LR: where the task goes if it returns */
    *--sp = 0U;                            /* R12 */
    *--sp = 0U;                            /* R3 */
    *--sp = 0U;                            /* R2 */
    *--sp = 0U;                            /* R1 */
    *--sp = (uint32_t)arg;                 /* R0 */
    *--sp = 0xFFFFFFFDU;                   /* EXC_RETURN: thread mode, PSP, no FPU frame */
    for (int r = 0; r < 8; r++)
    {
      *--sp = 0U;                          /* R11..R4 */
    }

    tcbs[id].sp = (uint32_t)sp;
    tcbs[id].prio = prio;
    tcbs[id].state = T_READY;
  }
  __set_PRIMASK(primask);
  return id;
}

void os_start(void)
{
  uint32_t psp_top = (uint32_t)&boot_stack[sizeof(boot_stack) / sizeof(boot_stack[0])];
  uint32_t msp_top = (uint32_t)&_estack;

  running = 1U;

  /* Move to PSP, give the exceptions the whole MSP stack again, and pend the first context switch.
     Nothing after the CONTROL write may use the old stack, so this stays in one asm block. */
  __asm volatile(
    "msr psp, %0        \n"
    "msr msp, %1        \n"
    "movs r0, #2        \n"
    "msr control, r0    \n"
    "isb                \n"
    "ldr r0, =0xE000ED04\n" /* SCB->ICSR */
    "ldr r1, =0x10000000\n" /* PENDSVSET */
    "str r1, [r0]       \n"
    "cpsie i            \n"
    "1: wfi             \n"
    "b 1b               \n"
    :
    : "r"(psp_top), "r"(msp_top)
    : "r0", "r1", "memory");
}

/* Called by PendSV_Handler with the outgoing task's stack pointer; returns the incoming one. */
uint32_t os_switch(uint32_t sp)
{
  int best = -1;
  int start = (cur_idx < 0) ? 0 : (cur_idx + 1);

  cur->sp = sp;

  /* Scan starting after the current task so equal priorities take turns */
  for (int k = 0; k < (int)OS_MAX_TASKS; k++)
  {
    int i = (start + k) % (int)OS_MAX_TASKS;
    if (tcbs[i].state == T_READY && (best < 0 || tcbs[i].prio > tcbs[best].prio))
    {
      best = i;
    }
  }
  if (best < 0)
  {
    for (;;) { } /* unreachable: the idle task is always ready */
  }
  cur_idx = best;
  cur = &tcbs[best];
  return cur->sp;
}

void os_tick(void)
{
  tick_count++;
  if (!running)
  {
    return;
  }
  for (uint32_t i = 0; i < OS_MAX_TASKS; i++)
  {
    if (tcbs[i].state == T_SLEEP && (int32_t)(tick_count - tcbs[i].wake) >= 0)
    {
      tcbs[i].state = T_READY;
    }
  }
  SCB->ICSR = SCB_ICSR_PENDSVSET_Msk; /* re-evaluate who should run, and rotate equal priorities */
}

void os_yield(void)
{
  SCB->ICSR = SCB_ICSR_PENDSVSET_Msk;
  __DSB();
  __ISB();
}

void os_delay(uint32_t ms)
{
  uint32_t primask;

  if (!running || ms == 0U)
  {
    os_yield();
    return;
  }
  primask = __get_PRIMASK();
  __disable_irq();
  cur->wake = tick_count + ms;
  cur->state = T_SLEEP;
  SCB->ICSR = SCB_ICSR_PENDSVSET_Msk;
  __set_PRIMASK(primask);
  __DSB();
  __ISB();
}

void os_task_delete(int id)
{
  int self = (id < 0);
  uint32_t primask = __get_PRIMASK();

  __disable_irq();
  if (self)
  {
    id = cur_idx;
  }
  if (id >= 0 && id < (int)OS_MAX_TASKS)
  {
    tcbs[id].state = T_FREE;
  }
  SCB->ICSR = SCB_ICSR_PENDSVSET_Msk;
  __set_PRIMASK(primask);
  __DSB();
  __ISB();
  while (self)
  {
    /* never scheduled again; PendSV switches away */
  }
}

uint32_t os_ticks(void)
{
  return tick_count;
}

int os_is_running(void)
{
  return running;
}

/* Context switch. Hardware has already stacked R0-R3, R12, LR, PC, xPSR on PSP. */
__asm__(
  ".syntax unified                      \n"
  ".pushsection .text.PendSV_Handler,\"ax\",%progbits \n"
  ".global PendSV_Handler               \n"
  ".type PendSV_Handler, %function      \n"
  ".thumb_func                          \n"
  "PendSV_Handler:                      \n"
  "  mrs r0, psp                        \n"
  "  tst lr, #0x10                      \n" /* bit 4 clear = FPU context is active */
  "  it eq                              \n"
  "  vstmdbeq r0!, {s16-s31}            \n"
  "  stmdb r0!, {r4-r11, lr}            \n"
  "  bl os_switch                       \n"
  "  ldmia r0!, {r4-r11, lr}            \n"
  "  tst lr, #0x10                      \n"
  "  it eq                              \n"
  "  vldmiaeq r0!, {s16-s31}            \n"
  "  msr psp, r0                        \n"
  "  isb                                \n"
  "  bx lr                              \n"
  ".size PendSV_Handler, .-PendSV_Handler \n"
  ".popsection                          \n"
);
