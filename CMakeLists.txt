cmake_minimum_required(VERSION 3.20)
project(grblHAL_UNO_Q C ASM)

set(ELF_TARGET ${PROJECT_NAME}.elf)

# ── ソースファイル (inventory §5 Debug構成の明示リスト) ─────────

# Core/Src: 5ファイル。Core/Src/main.c は除外 (CubeIDE sourceEntries から)
set(CORE_SRC
    Core/Src/stm32u5xx_hal_msp.c
    Core/Src/stm32u5xx_it.c
    Core/Src/syscalls.c
    Core/Src/sysmem.c
    Core/Src/system_stm32u5xx.c
)

# Core/Startup: アセンブラ
set(STARTUP_SRC
    Core/Startup/startup_stm32u585aiixq.s
)

# Drivers/STM32U5xx_HAL_Driver/Src: 20ファイル (subdir.mk 実測)
set(HAL_SRC
    Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_hal.c
    Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_hal_adc.c
    Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_hal_adc_ex.c
    Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_hal_cortex.c
    Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_hal_dma.c
    Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_hal_dma_ex.c
    Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_hal_exti.c
    Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_hal_flash.c
    Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_hal_flash_ex.c
    Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_hal_gpio.c
    Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_hal_gtzc.c
    Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_hal_i2c.c
    Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_hal_i2c_ex.c
    Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_hal_icache.c
    Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_hal_pwr.c
    Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_hal_pwr_ex.c
    Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_hal_rcc.c
    Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_hal_rcc_ex.c
    Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_hal_uart.c
    Drivers/STM32U5xx_HAL_Driver/Src/stm32u5xx_hal_uart_ex.c
)

# Src/: 26ファイル。system_stm32u5xx.c / w5x00_ll_driver.c / pwm.c は除外
# Src/syscalls.c と Src/sysmem.c は #ifndef STM32U585xx でガード済み(空のTU)
set(SRC_SRC
    Src/can.c
    Src/diskio.c
    Src/driver.c
    Src/driver_spindles.c
    Src/encoders.c
    Src/enet.c
    Src/flash.c
    Src/i2c.c
    Src/ioports.c
    Src/ioports_analog.c
    Src/main.c
    Src/neopixel_gpo.c
    Src/neopixel_pwm.c
    Src/neopixel_spi.c
    Src/serial.c
    Src/spi.c
    Src/stm32f4xx_hal_msp.c
    Src/stm32f4xx_it.c
    Src/syscalls.c
    Src/sysmem.c
    Src/thcad2.c
    Src/timers.c
    Src/tmc_spi.c
    Src/tmc_uart.c
    Src/triac_control.c
    Src/triac_mcodes.c
    Src/usb_serial.c
)

# grbl/: 外部管理リポジトリ。GLOB で全 .c を取得 (inventory §8)
file(GLOB GRBL_SRC     CONFIGURE_DEPENDS grbl/*.c)
file(GLOB GRBL_KIN_SRC CONFIGURE_DEPENDS grbl/kinematics/*.c)

add_executable(${ELF_TARGET}
    ${CORE_SRC}
    ${STARTUP_SRC}
    ${HAL_SRC}
    ${SRC_SRC}
    ${GRBL_SRC}
    ${GRBL_KIN_SRC}
)

# ── プリプロセッサ定義 (inventory §1 Debug) ──────────────────
target_compile_definitions(${ELF_TARGET} PRIVATE
    DEBUG
    COREXY=1
    BOARD_UNO_Q_CNC
    USE_HAL_DRIVER
    STM32U585xx
)

# ── インクルードパス (inventory §2 Debug) ────────────────────
# CubeIDE の ".." はプロジェクトルートを指す (Inc/ grbl/ への直接参照に必要)
target_include_directories(${ELF_TARGET} PRIVATE
    Core/Inc
    ${CMAKE_SOURCE_DIR}
    Drivers/STM32U5xx_HAL_Driver/Inc
    Drivers/STM32U5xx_HAL_Driver/Inc/Legacy
    Drivers/CMSIS/Device/ST/STM32U5xx/Include
    Drivers/CMSIS/Include
    grbl
    Inc
    Src
)

# ── コンパイルオプション (inventory §3) ──────────────────────
target_compile_options(${ELF_TARGET} PRIVATE
    $<$<COMPILE_LANGUAGE:C>:
        -mcpu=cortex-m33
        -mthumb
        -mfpu=fpv5-sp-d16
        -mfloat-abi=hard
        -std=gnu11
        -O0
        -g3
        -ffunction-sections
        -fdata-sections
        -Wall
        -fstack-usage
        --specs=nano.specs
    >
    $<$<COMPILE_LANGUAGE:ASM>:
        -mcpu=cortex-m33
        -mthumb
        -mfpu=fpv5-sp-d16
        -mfloat-abi=hard
        -g3
        -x assembler-with-cpp
        --specs=nano.specs
    >
)

# ── リンカオプション (inventory §4) ──────────────────────────
set(LINKER_SCRIPT ${CMAKE_SOURCE_DIR}/STM32U585AIIXQ_FLASH.ld)

target_link_options(${ELF_TARGET} PRIVATE
    -mcpu=cortex-m33
    -mthumb
    -mfpu=fpv5-sp-d16
    -mfloat-abi=hard
    -T${LINKER_SCRIPT}
    --specs=nosys.specs
    --specs=nano.specs
    -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.map
    -Wl,--gc-sections
    -static
    SHELL:-Wl,--start-group -lc -lm -Wl,--end-group
)

set_target_properties(${ELF_TARGET} PROPERTIES LINK_DEPENDS ${LINKER_SCRIPT})

# ── ビルド後: size表示 ────────────────────────────────────────
add_custom_command(TARGET ${ELF_TARGET} POST_BUILD
    COMMAND arm-none-eabi-size ${ELF_TARGET}
    COMMENT "ELF size:"
)
