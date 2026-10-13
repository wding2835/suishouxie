# UART_Receive_Transmit_Console – Project Overview

## 1. Purpose

Bare-metal firmware for the **Cortex-M4** core of the STM32MP157C-DK2 board. It retargets the C library `printf`/`getchar` to a UART, so a serial terminal can interact with the board:

1. Prints a banner and waits for the user to type `c`.
2. Shows a menu (`Test1`, `Test2`, `Test3`) via `Test_Validation_Menu()`.
3. Asks for a blink duration (default 10 s), then blinks LED7 fast (100 ms) or slow (500 ms) in the background while the menu stays responsive; or exits.
4. Turns LED7 on at the end; blinks it (1 s toggle) if `Error_Handler()` is reached.

## 2. System context (dual core)

| Core | Role |
|------|------|
| Cortex-A7 | Typically runs Linux; in production mode configures the clocks and can load the M4 firmware via `remoteproc`. |
| Cortex-M4 | Runs this firmware from SRAM (no flash on the M4). |

- **Engineering mode** (debug via ST-Link/OpenOCD): `IS_ENGINEERING_BOOT_MODE()` is true, so `main()` calls `HAL_RCC_DeInit()` and `SystemClock_Config()` (PLL1–PLL4, MCU clock from PLL3).
- **Production mode** (Linux `remoteproc`): the A7 firmware configures the clocks; the M4 skips clock setup.
- Shared peripherals are protected with hardware semaphores: `PERIPH_LOCK()` / `PERIPH_UNLOCK()` from `lock_resource.h` (100 ms timeout, `Error_Handler()` on failure).

## 3. Program flow (`Src/main.c`)

```
main():
 HAL_Init()
  ?? if engineering mode: HAL_RCC_DeInit(); SystemClock_Config()
 MX_GPIO_Init(); MX_USART3_UART_Init()   // UART4, see UART section
 UART4 NVIC + HAL_UART_Receive_IT()
 BSP_LED_Init(LED7)
 os_init()                               // PendSV priority, idle task
 os_task_create(blink_task,   prio 3)
 os_task_create(console_task, prio 2)
 os_start()                              // never returns

console_task:  banner -> wait for 'c' -> Test_Validation_Menu() -> stop banner -> sleep forever
blink_task:    sleeps until start_blink() gives it toggles, toggles LED7, prints "finished"
idle task:     __WFI() loop (prio 0, inside the kernel)
```

### Key functions

| Function | Description |
|----------|-------------|
| `SystemClock_Config()` | Configures HSE/HSI/LSE and PLL1–PLL4, and the MPU/AXI/MCU/APB dividers. Engineering mode only. |
| `MX_USART3_UART_Init()` | Initializes `huart1` (see UART section). Disables FIFO mode. |
| `Serial_Scanf(char *ptr, int len)` | Reads up to `len` characters, ending at `\n` or `\r`. Returns the count, or `0xFF` on an invalid character. |
| `__write()` / `__io_putchar()` | printf output. Bytes go into a 256-byte TX ring buffer (`tx_enqueue`); `tx_kick` starts `HAL_UART_Transmit_IT` and `HAL_UART_TxCpltCallback` sends the next byte. |
| `__io_getchar()` | scanf/getchar input. Takes a byte from the 64-byte RX ring buffer (filled by `HAL_UART_RxCpltCallback`). While empty it sleeps on `rx_sem` (given by the RX callback per byte). Echoes through the TX ring. |
| `HAL_UART_RxCpltCallback()` / `HAL_UART_ErrorCallback()` | Push the received byte into the RX ring and re-arm reception; the error callback clears the overrun flag first. |
| `start_blink(period_ms, seconds, name)` | Hands a toggle count to the permanent `blink_task`. Refuses a second test while one runs. |
| `console_puts(s)` | Prints without stdio (stdio is not re-entrant); used by `blink_task`. |
| `ask_blink_seconds()` | Prompts `Duration in seconds (1-3600, Enter = 10):`. Enter or 0 gives 10 s; above 3600 is capped. `rx_eat_eol()` drops the Enter left after the menu text; `fflush(stdout)` makes the prompt appear. |
| `os_*` kernel API | Preemptive kernel in `Src/os_sched.c`; see section 5a. |
| `Test_Validation_Menu()` | Menu loop. `Test1` = fast blink (100 ms), `Test2` = slow blink (500 ms), each for a user-selected duration (default 10 s) while `blink_task` runs it in the background; `Temp` prints the die temperature from the DTS sensor (`dts_read_mdegc()`, `print_temperature()`, ~35 C verified on board); `Test3` exits. The menu strips the trailing Enter that `Serial_Scanf` stores, so entries longer than 5 chars match. |
| `Error_Handler()` | Toggles LED7 every 1 s forever. |

## 4. UART configuration

> **Important:** the original ST readme describes USART3 (PB10/PB12, odd parity) on connector CN2. **The current code differs:**

| Item | Current source |
|------|----------------|
| Instance | `UART4` (`huart1.Instance = UART4;` in `main.c`) |
| Pins | PG11 = TX (AF6), PB2 = RX (AF8), see `stm32mp1xx_hal_msp.c` |
| Baud rate | 115200 |
| Word length / parity / stop | 8 bits, **no parity**, 1 stop bit |
| Flow control | None |
| Oversampling | 8× |
| FIFO | Disabled |
| Interrupt | `UART4_IRQHandler` in `Src/stm32mp1xx_it.c` calls `HAL_UART_IRQHandler(&huart1)`; NVIC priority 5 |

`stm32mp1xx_hal_msp.c` still contains the original USART3 branch (PB10 AF7 / PB12 AF8) alongside the added UART4 branch, so either instance can be initialized. The function name `MX_USART3_UART_Init` and the comments are historical. To go back to USART3 on CN2 as in the original readme, set `Instance = USART3` and restore the parity/word-length settings from ST's original example (kept in `main.c.bak` if you need a reference).

Always confirm that the chosen UART and pins are assigned to the M4 (not Linux) in the device tree / Resource Manager.

## 5. Project layout

| Path | Contents |
|------|----------|
| `Src/main.c` | Application, interrupt-driven retargeting (RX/TX ring buffers), blink task, menu. USER CODE sections must be preserved. |
| `Src/os_sched.c`, `Inc/os_sched.h` | Preemptive kernel (up to 8 tasks incl. idle). Linked into the CubeIDE project via `STM32CubeIDE/CM4/.project`. |
| `Src/stm32mp1xx_hal_msp.c` | Low-level init: clocks, GPIO alternate functions, HSEM clock, with `PERIPH_LOCK` around GPIO access. |
| `Src/stm32mp1xx_it.c`, `Inc/stm32mp1xx_it.h` | Interrupt handlers. |
| `Src/system_stm32mp1xx.c` | CMSIS `SystemInit()`. |
| `Src/lock_resource.c`, `Inc/lock_resource.h` | HSEM-based peripheral locking. |
| `Inc/stm32mp1xx_hal_conf.h`, `Inc/stm32mp15xx_disco_conf.h` | HAL module selection and board configuration. |
| `Inc/main.h` | Application header. |
| `STM32CubeIDE/CM4/` | Eclipse project, linker script `STM32MP157CAAX_RAM.ld`, startup file, OpenOCD `.cfg` and `.launch` debug configuration. |
| `EWARM/`, `MDK-ARM/` | IAR and Keil project equivalents. |
| `Remoteproc/` | `fw_cortex_m4.sh` and README for loading the firmware from Linux. |
| `*.bak` | Backup copies of `main.c` and `stm32mp1xx_hal_msp.c`. |

## 5a. The preemptive kernel (`Src/os_sched.c`)

- **Scheduling:** fixed priority, 1-255 (higher runs first), 0 = idle. Tasks of equal priority take turns each 1 ms tick.
- **Tick:** `SysTick_Handler` calls `os_tick()`, which wakes sleeping tasks and pends PendSV.
- **Context switch:** `PendSV_Handler` (assembly, lowest interrupt priority 15) pushes r4-r11, LR and, if the task used the FPU, s16-s31 onto the task's stack, calls `os_switch()` to pick the next task, then restores that task. The generated `PendSV_Handler` was removed from `stm32mp1xx_it.c`.
- **Stacks:** each task runs on its own PSP stack; interrupts use MSP. `os_start()` abandons `main`'s stack, resets MSP to `_estack`, and triggers a first switch from a throw-away boot context.
- **Tasks** are created with a caller-supplied static stack and an initial fake exception frame; returning from a task calls `os_task_delete(-1)`.
- **API:** `os_init`, `os_task_create`, `os_start`, `os_delay`, `os_yield`, `os_task_delete`, `os_ticks`, `os_is_running`, `os_tick`. Plus `os_sem_init/give/take` (counting semaphore, timeout or `OS_WAIT_FOREVER`; `give` is ISR-safe) and `os_mutex_init/lock/unlock` (task context only, no priority inheritance, not recursive). No queues yet.

## 6. Memory map (`STM32MP157CAAX_RAM.ld`)

| Region | Origin | Length | Use |
|--------|--------|--------|-----|
| `m_interrupts` | `0x00000000` | `0x298` | Vector table (`.isr_vector`) |
| `m_text` | `0x10000000` | `0x20000` | Code and read-only data |
| `m_data` | `0x10020000` | `0x20000` | Data, bss, heap, stack, resource table |
| `m_ipc_shm` | `0x10040000` | `0x8000` | OpenAMP/rpmsg shared memory |

- Stack top `_estack = 0x10040000`; minimum heap `0x200`, minimum stack `0x400`.
- The reset handler lives in section `.startup_copro_fw.Reset_Handler`, which the Linux remoteproc framework expects.
- A halted core showing `pc: 0x00000008` with a tiny `msp` (such as `0x100`) is a failed-startup symptom. A healthy session shows a PC in `0x1000xxxx` and an MSP near `0x1003ff80`.

## 7. Build and run

**Build (STM32CubeIDE):** open `STM32CubeIDE/CM4`, then Project → Build (Debug), or `make -C STM32CubeIDE/CM4/Debug`.

**Debug (engineering mode):**
- Use launch config `UART_Receive_Transmit_Console_CM4` (OpenOCD, ST-Link, SWD, AP 2, GDB port 3333).
- Load mode: thru JTAG/SWD link. Startup commands: `monitor reset`, `monitor halt`; a breakpoint is set at `main`.
- OpenOCD script: `UART_Receive_Transmit_Console_CM4.cfg` (`stlink-dap`, `dapdirect_swd`, `stm32mp1x.cfg`, `reset_config srst_only`).

**Run from Linux (production mode):** copy the signed firmware and use `Remoteproc/fw_cortex_m4.sh start` / `stop`. Boot the A7 Linux first.

**Terminal:** open the board's serial port at 115200, 8N1 (matching the code above), then:
1. Type `c`.
2. At the menu, type `Test1`, `Test2` or `Test3`. For Test1/Test2 a duration prompt follows: type seconds and press Enter, or just Enter for 10 s.
3. The menu returns immediately while LED7 blinks; a message prints when the test finishes.

## 8. Known issues and notes

- Fixed: `menu_select` is now 6 bytes and null-terminated, and empty or invalid input is rejected.
- `Serial_Scanf` returns `0xFF` for characters >= 0xFF and has no terminator handling; the menu treats counts <= 0 or > 5 as invalid.
- After the 5-character menu entry, the trailing Enter stays in the RX ring. `rx_eat_eol(300)` drops it, waiting up to 300 ms; an Enter that arrives later would be read as an empty duration (default 10 s).
- UART4 is shared hardware. If Linux on the A7 also claims UART4, interrupts may not reach the M4.
- The kernel is preemptive. Only the console task should use `printf`/`scanf` (stdio is not re-entrant); other tasks use `console_puts`. A mutex is on the roadmap.
- LED7 is guarded by `led_mutex` (console and blink tasks).
- A full TX ring blocks the writing task on `tx_room_sem` (freed slots are given from the TX interrupt); no polling remains.
- Fixed task stacks (console 4 KB, blink 1 KB, idle 512 B); no overflow check yet (stacks are pre-filled with `0xA5A5A5A5` to help inspect usage).
- `readme.txt` is the original ST text and still describes USART3 with odd parity; it does not match the code (UART4, 8N1).
- The init function is still called `MX_USART3_UART_Init` and the handle `huart1` (generator leftovers).
- An STM32CubeIDE update once caused debug-connect failures; a clean reinstall of the IDE (1.18.1 era) resolved it. The root cause (IDE vs ST-Link firmware vs OpenOCD) was never confirmed.
- The ARM toolchain is only inside CubeIDE, so builds and tests are done from the IDE. After adding a source file, edit `.project` (link) and close/reopen the project.
## 9. Debug-session messages and what they mean

| Message | Meaning |
|---------|---------|
| `Unable to match requested speed 8000 kHz, using 4000 kHz` | Harmless; the probe fell back to a supported SWD clock. |
| `Failed to read memory at ...` / `Failed to write memory` | The target could not be accessed during reset or load. Check the IDE/OpenOCD version and that Linux isn't holding the M4. |
| `Error executing event examine-end ... ocd_process_reset` | The OpenOCD reset script failed; the M4 was left in an invalid startup state. |

## 10. Project status and next steps

Goal: a home-made, Linux-like but smaller OS with real-time capability. The M4 runs the real-time kernel; Linux stays on the A7; they talk over rpmsg/OpenAMP.

### Done (all tested on the board)

| Step | Commit |
|------|--------|
| Debug fix (IDE reinstall), project documentation | `ab5d671` |
| Interrupt-driven UART RX with ring buffer, menu overflow fix | `e982b46` |
| Interrupt-driven UART TX with ring buffer | `2cae4f9` |
| Cooperative scheduler, non-blocking LED tests, selectable blink duration (default 10 s) | `c51c9b2` |
| **Semaphores and mutex** (RX wait is now event-driven; LED mutex) | `a74ef80` |
| **TX ring-full wait via semaphore** (no polling left) | see git log |
| **Preemptive scheduler** (PendSV switch, SysTick tick, priorities, per-task stacks, idle task); console and blink tasks | see git log |

### Next (roadmap)

1. ~~Preemptive scheduler~~ done.
2. Semaphores and mutex done. TX wait is semaphore-based. **Next:** message queues (needed by a shell and rpmsg), a stdio lock once a second task prints, stack-overflow detection.
3. Heap allocator, stack-overflow detection, MPU protection.
4. Simple shell on the UART (commands instead of the fixed menu).
5. rpmsg/OpenAMP link to Linux on the A7.
6. Measure interrupt latency and jitter (e.g. DWT cycle counter) to prove the real-time behaviour.
7. Decide whether to evaluate FreeRTOS from the ST package first.

### Quick pick-up checklist

- Build and debug in STM32CubeIDE (`STM32CubeIDE/CM4`); terminal on COM11 at 115200 8N1 (Tera Term).
- Healthy debug state: PC around `0x1000xxxx`, MSP around `0x1003ff80`.
- Keep edits inside USER CODE sections; lock shared peripherals with `PERIPH_LOCK`/`PERIPH_UNLOCK`.
- Workflow so far: the AI assistant edits the code, the user builds and tests, then commit and push (with the Co-authored-by trailer).