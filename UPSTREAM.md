# Upstream grblHAL Reference

This firmware is derived from the grblHAL STM32F4xx driver by Terje Io (GPLv3).

## Base repository

- **Upstream**: https://github.com/grblHAL/STM32F4xx
- **Base commit**: `ba99f20` — the `driver.c` in this repository was forked at this point
  (commit message: "fix(driver): correct hal.f_step_timer for STM32U585 (APB1 prescaler = 1)")
- **Note**: `serial.c`, `main.c`, `flash.c`, `flash.h` are also derived from the same upstream.

## STM32U585 patches applied (not in upstream)

| File | Change | Reason |
|------|--------|--------|
| `Src/driver.c` | `hal.f_step_timer = HAL_RCC_GetPCLK1Freq() / STEPPER_TIMER_DIV` (remove `*2`) | APB1 prescaler=1 on U585 |
| `Src/serial.c` | LPUART1 on PG7/PG8 (AF8); BRR = `256*CLK/baud` | Internal QRB2210 link |
| `Src/main.c` | `HAL_PWREx_EnableVddIO2()` before GPIO init; `HAL_Init()` first | Port G power isolation |
| `Src/flash.c` | Bank 2 detection (`0x081FE000` in Bank 2) | Dual-bank flash on U585 |
| `Core/Src/stm32u5xx_it.c` | `Driver_IncTick()` in `SysTick_Handler` USER CODE block | grblHAL delay counter |
| `Inc/my_machine.h` | UNO Q block: `SERIAL_PORT 90`, `FLASH_ENABLE 1`, `COREXY 1` | Board-specific config |

## UNO Q additions (not in upstream)

- `boards/uno_q_cnc_map.h` — pin map for Arduino UNO Q + CNC Shield V3
- `Src/triac_control.c` / `Inc/triac_control.h` — DimmerLink I2C + ADC/FAN control
- `Src/triac_mcodes.c` / `Inc/triac_mcodes.h` — grblHAL M810–M816 integration
- `STM32U585AIIXQ_FLASH.ld` — linker script for STM32U585AI (2 MB flash, dual-bank)
