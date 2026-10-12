# STM32MP157C-DK2 UART Console Example

## Project architecture

This example runs firmware on the STM32MP157C Cortex-M4 core. The Cortex-A7
typically runs Linux and can load or stop the M4 firmware using remoteproc.
Keep changes consistent with this dual-core setup and preserve STM32CubeMX
`USER CODE` sections when editing generated source.

## Source and project layout

- `Src/` and `Inc/` contain the application source and headers.
- `STM32CubeIDE/CM4/` contains the STM32CubeIDE project, linker script,
  startup assembly, HAL/BSP dependencies, and remoteproc support files.
- `EWARM/` and `MDK-ARM/` contain the IAR and Keil project files.
- `Remoteproc/` contains the example's top-level remoteproc documentation and
  related support files.

Use the IDE project files for builds; do not assume a standalone Makefile or
source paths outside this repository are available. STM32CubeIDE generates
makefiles and output files under `STM32CubeIDE/CM4/Debug/`.

## Hardware and UART configuration

- Target board: STM32MP157C-DK2.
- The application initializes USART3 on PB10 (TX) and PB12 (RX).
- UART settings in `Src/main.c`: 115200 baud, 8 data bits, no parity, 1 stop
  bit, transmit and receive enabled, hardware flow control disabled (8N1).
- The example retargets standard input/output to the UART. Keep console I/O
  through the existing retargeting rather than introducing a second output
  path without a specific need.
- The application uses BSP LED7 for its interactive test menu and completion
  indication.

## Dual-core and peripheral access

- `IS_ENGINEERING_BOOT_MODE()` gates the M4 clock setup in `Src/main.c`.
  Do not remove this check or assume the M4 owns clock configuration in every
  boot mode.
- `Src/lock_resource.c` and `Inc/lock_resource.h` provide HSEM-backed
  `PERIPH_LOCK()` / `PERIPH_UNLOCK()` helpers. The configured lock timeout is
  100 ms; lock failure invokes `Error_Handler()`.
- Preserve the existing locks around shared GPIO access in the HAL MSP code.
  Coordinate peripheral ownership with the A7/Linux side before changing
  assignments or initialization.

## Memory and firmware loading

- The STM32CubeIDE M4 project uses `STM32CubeIDE/CM4/STM32MP157CAAX_RAM.ld`.
- The M4 image is loaded by Linux remoteproc; follow the instructions in
  `STM32CubeIDE/CM4/Application/RemoteProc/README` and its script when
  deploying. The script expects firmware in a `lib/firmware/` directory
  relative to its working directory, so confirm the image is staged there
  before starting the firmware.

## Build artifacts and version control

CubeIDE build outputs such as `.elf`, `.map`, `.list`, and object files are
generated and ignored by `.gitignore`. Commit source, project configuration,
linker scripts, and intentional debug configuration changes; do not commit
generated build output.
