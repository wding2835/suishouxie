# UART_Receive_Transmit_Console – Project Overview

## 1. Purpose

Bare-metal firmware for the **Cortex-M4** core of the STM32MP157C-DK2 board. It retargets the C library `printf`/`getchar` to a UART, so a serial terminal can interact with the board:

1. Prints a banner and waits for the user to type `c`.
2. Shows a menu (`Test1`, `Test2`, `Test3`) via `Test_Validation_Menu()`.
3. Blinks LED7 fast or slow depending on the choice, or exits.
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
HAL_Init()
 └─ if engineering mode: HAL_RCC_DeInit(); SystemClock_Config()
MX_GPIO_Init()
MX_USART3_UART_Init()          // name kept from the generator; see UART section
BSP_LED_Init(LED7)
printf banner
loop: Serial_Scanf(name,1) until 'c'
Test_Validation_Menu()         // Test1 / Test2 / Test3
BSP_LED_On(LED7); printf stop banner
while(1) {}
```

### Key functions

| Function | Description |
|----------|-------------|
| `SystemClock_Config()` | Configures HSE/HSI/LSE and PLL1–PLL4, and the MPU/AXI/MCU/APB dividers. Engineering mode only. |
| `MX_USART3_UART_Init()` | Initializes `huart1` (see UART section). Disables FIFO mode. |
| `Serial_Scanf(char *ptr, int len)` | Reads up to `len` characters, ending at `\n` or `\r`. Returns the count, or `0xFF` on an invalid character. |
| `__write()` / `__io_putchar()` | printf output. Transmits one character per call with `HAL_UART_Transmit`. |
| `__io_getchar()` | scanf/getchar input. Clears the overrun flag, receives one character, and echoes it. |
| `Test_Validation_Menu()` | Menu loop. `Test1` toggles LED7 50× at 100 ms, `Test2` toggles it 10× at 500 ms, `Test3` exits. |
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

`stm32mp1xx_hal_msp.c` still contains the original USART3 branch (PB10 AF7 / PB12 AF8) alongside the added UART4 branch, so either instance can be initialized. The function name `MX_USART3_UART_Init` and the comments are historical. To go back to USART3 on CN2 as in the original readme, set `Instance = USART3` and restore the parity/word-length settings from ST's original example (kept in `main.c.bak` if you need a reference).

Always confirm that the chosen UART and pins are assigned to the M4 (not Linux) in the device tree / Resource Manager.

## 5. Project layout

| Path | Contents |
|------|----------|
| `Src/main.c` | Application, retargeting, menu. USER CODE sections must be preserved. |
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
2. At the menu, type `Test1`, `Test2` or `Test3` and press Enter.

## 8. Known issues and notes

- `Test_Validation_Menu()` declares `char menu_select[5]` but `strcpy(menu_select, "MENU0")` writes 6 bytes (including the null terminator). This overruns the buffer by one byte; increase the buffer to 6.
- `Serial_Scanf(menu_select, 5)` reads at most 5 characters and does not null-terminate. Comparison uses `strncmp(..., menu_option)`, so an empty input (count 0) matches `Test1` by accident.
- Test duration: Test1 runs 50 × 100 ms and Test2 runs 10 × 500 ms, so both last about 5 s, as the readme says.
- An STM32CubeIDE update once caused debug-connect failures; a clean reinstall of the IDE (1.18.1 era) resolved it.

## 9. Debug-session messages and what they mean

| Message | Meaning |
|---------|---------|
| `Unable to match requested speed 8000 kHz, using 4000 kHz` | Harmless; the probe fell back to a supported SWD clock. |
| `Failed to read memory at ...` / `Failed to write memory` | The target could not be accessed during reset or load. Check the IDE/OpenOCD version and that Linux isn't holding the M4. |
| `Error executing event examine-end ... ocd_process_reset` | The OpenOCD reset script failed; the M4 was left in an invalid startup state. |
