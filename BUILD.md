# Build Requirements

## Environment

| Item | Requirement |
|------|-------------|
| IDE | STM32CubeIDE (Windows) |
| Project path | `C:\Tools\grblHAL_UNO_Q\` |
| ELF output | `C:\Tools\grblHAL_UNO_Q\Debug\grblHAL_UNO_Q.elf` |
| Build size | ~303 KB (Debug) |

## Mandatory preprocessor definitions

These **must** be set in STM32CubeIDE:
`Project → Properties → C/C++ Build → Settings → MCU GCC Compiler → Preprocessor`

| Symbol | Value | Purpose |
|--------|-------|---------|
| `BOARD_UNO_Q_CNC` | *(defined, no value)* | Selects `boards/uno_q_cnc_map.h` pin map |
| `COREXY` | `1` | CoreXY kinematics (also set in `my_machine.h`) |
| `STM32U585xx` | *(defined by CubeMX)* | MCU family selection |

> **Note**: `BOARD_UNO_Q_CNC` is **not** defined in `my_machine.h` — it must be in the
> CubeIDE preprocessor settings. Omitting it causes `boards/generic_map.h` to be used
> (which does not exist in this repo → compile error).

## Required HAL modules

Enable in `Core/Inc/stm32u5xx_hal_conf.h`:

```c
#define HAL_ADC_MODULE_ENABLED
#define HAL_I2C_MODULE_ENABLED
```

These are required by `triac_control.c`. The corresponding HAL source files
(`stm32u5xx_hal_adc.c`, `stm32u5xx_hal_adc_ex.c`, `stm32u5xx_hal_i2c.c`) must be
included in the CubeIDE project from `Drivers/STM32U5xx_HAL_Driver/Src/`.

## Critical invariants (do not change)

1. **`hal.f_step_timer = HAL_RCC_GetPCLK1Freq() / STEPPER_TIMER_DIV`** (`driver.c` ~2789)
   — STM32U585 APB1 prescaler=1; the `*2` factor used on STM32F4xx causes 50% speed error.

2. **`Driver_IncTick()` in `SysTick_Handler`** (`Core/Src/stm32u5xx_it.c` ~194)
   — Required for grblHAL delay counter; omitting causes `driver_setup()` infinite loop.

3. **`HAL_PWREx_EnableVddIO2()`** must be called before any Port G GPIO init
   (`main.c`) — Port G is power-isolated by default on STM32U585.

4. **Power cycle required after SWD flash** — OpenOCD `reset halt` interrupts the
   STM32U585 Boot ROM (RSS); `reset run` does not recover it. One full power cycle
   after flashing is mandatory.

## Timer allocation

| Timer | Role | Notes |
|-------|------|-------|
| TIM5 | `STEPPER_TIMER` | Defined in `driver.h` (32-bit, overrides board map) |
| TIM2 | `STEPPER_TIMER_N` in board map | **Overridden by `driver.h` — dead code in `uno_q_cnc_map.h`** |
| TIM3 | `PULSE_TIMER_N` in board map | **Overridden by `driver.h` — dead code in `uno_q_cnc_map.h`** |

> See `driver.h` line 227: `#define STEPPER_TIMER_N 5` (no `#ifndef` guard).
> The TIM2/TIM3 definitions in `boards/uno_q_cnc_map.h` have no effect.
> HANDOFF documentation stating "TIM2=STEPPER/TIM3=PULSE" is stale.

## Flash procedure

See `README.md §Flash`. Key points:
- `connect_assert_srst` + `srst_nogate` are mandatory (CPU examine fails without them)
- Physical power cycle required after every flash operation
