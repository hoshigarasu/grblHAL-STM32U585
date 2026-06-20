# CMake移行 ビルド設定インベントリ

出典: `.cproject` (XML) および `Debug/` 以下の自動生成 `*.mk` ファイル。
ツールチェーンバージョンは各 `subdir.mk` 冒頭コメントより。

> **注意: Release構成は不完全**（後述 §5 参照）。実用上は Debug のみ使用中。

---

## 1. プリプロセッサ定義

### Debug

| マクロ | 値 | 出典 |
|--------|-----|------|
| `DEBUG` | (値なし) | .cproject アセンブラ + Cコンパイラ |
| `COREXY` | `1` | .cproject Cコンパイラ |
| `BOARD_UNO_Q_CNC` | (値なし) | .cproject Cコンパイラ |
| `USE_HAL_DRIVER` | (値なし) | .cproject Cコンパイラ |
| `STM32U585xx` | (値なし) | .cproject Cコンパイラ |

### Release

| マクロ | 値 | 備考 |
|--------|-----|------|
| `USE_HAL_DRIVER` | (値なし) | |
| `COREXY` | `1` | |
| `STM32U585xx` | (値なし) | |
| `DEBUG` | — | **なし** |
| `BOARD_UNO_Q_CNC` | — | **なし(欠落)** — ボードマップが選択されない。Release構成は不完全。 |

---

## 2. インクルードパス

### Debug（Cコンパイラ）

```
../Core/Inc
..                                              ← プロジェクトルート(Inc/ や grbl/ への相対参照に必要)
../Drivers/STM32U5xx_HAL_Driver/Inc
../Drivers/STM32U5xx_HAL_Driver/Inc/Legacy
../Drivers/CMSIS/Device/ST/STM32U5xx/Include
../Drivers/CMSIS/Include
../grbl
../Inc
../Src
```

### Release（Cコンパイラ）

```
../Core/Inc
../Drivers/STM32U5xx_HAL_Driver/Inc
../Drivers/STM32U5xx_HAL_Driver/Inc/Legacy
../Drivers/CMSIS/Device/ST/STM32U5xx/Include
../Drivers/CMSIS/Include
```

`..`, `../grbl`, `../Inc`, `../Src` が**欠落**。Release構成は不完全。

---

## 3. コンパイラフラグ

全フラグは `Debug/Src/subdir.mk` の実コマンドラインより抽出（Debug構成）。

### Cコンパイラ (`arm-none-eabi-gcc`)

```
-mcpu=cortex-m33
-std=gnu11
-g3
-O0
-ffunction-sections
-fdata-sections
-Wall
-fstack-usage
-fcyclomatic-complexity
--specs=nano.specs
-mfpu=fpv5-sp-d16
-mfloat-abi=hard
-mthumb
```

Release では `-g3 → -g0`、`-O0 → -Os` に変わる。

### アセンブラ (`arm-none-eabi-gcc -x assembler-with-cpp`)

```
-mcpu=cortex-m33
-g3
-DDEBUG
-c
-x assembler-with-cpp
--specs=nano.specs
-mfpu=fpv5-sp-d16
-mfloat-abi=hard
-mthumb
```

---

## 4. リンカ

### リンカスクリプト

| 項目 | 値 |
|------|----|
| ファイル | `STM32U585AIIXQ_FLASH.ld`（プロジェクトルート直下） |
| .cproject内表記 | `${workspace_loc:/${ProjName}/STM32U585AIIXQ_FLASH.ld}` |

### メモリマップ（リンカスクリプトより）

| 領域 | 開始アドレス | サイズ | 属性 |
|------|-------------|--------|------|
| FLASH | `0x08000000` | 2048 KB | rx |
| RAM (SRAM1+2) | `0x20000000` | 768 KB | xrw |
| SRAM4 | `0x28000000` | 16 KB | xrw |
| EEPROM emulation | FLASH末尾8KB | 8 KB | — |

### リンカフラグ（`Debug/makefile` の `arm-none-eabi-gcc` リンカ呼び出しより）

```
-mcpu=cortex-m33
-T"STM32U585AIIXQ_FLASH.ld"
--specs=nosys.specs
--specs=nano.specs
-Wl,-Map="grblHAL_UNO_Q.map"
-Wl,--gc-sections
-static
-mfpu=fpv5-sp-d16
-mfloat-abi=hard
-mthumb
-Wl,--start-group -lc -lm -Wl,--end-group
```

---

## 5. ソースファイル/ディレクトリ構成

`Debug/sources.mk` の `SUBDIRS` が正式なビルド対象。

### Debug でコンパイルされるディレクトリ

| ディレクトリ | 内容 | 備考 |
|-------------|------|------|
| `Core/Src/` | stm32u5xx_hal_msp.c, stm32u5xx_it.c, syscalls.c, sysmem.c, system_stm32u5xx.c | `Core/Src/main.c` は除外 |
| `Core/Startup/` | startup_stm32u585aiixq.s | アセンブラ |
| `Drivers/STM32U5xx_HAL_Driver/Src/` | HALドライバ 18ファイル（下記） | |
| `Src/` | 26ファイル（下記） | 3ファイル除外あり |
| `grbl/` | 44ファイル（下記） | |
| `grbl/kinematics/` | corexy.c, delta.c, maslow.c, polar.c, wall_plotter.c | |

#### `Drivers/STM32U5xx_HAL_Driver/Src/` コンパイル対象 (18ファイル)

stm32u5xx_hal.c, stm32u5xx_hal_adc.c, stm32u5xx_hal_adc_ex.c,
stm32u5xx_hal_cortex.c, stm32u5xx_hal_dma.c, stm32u5xx_hal_dma_ex.c,
stm32u5xx_hal_exti.c, stm32u5xx_hal_flash.c, stm32u5xx_hal_flash_ex.c,
stm32u5xx_hal_gpio.c, stm32u5xx_hal_gtzc.c, stm32u5xx_hal_i2c.c,
stm32u5xx_hal_i2c_ex.c, stm32u5xx_hal_icache.c, stm32u5xx_hal_pwr.c,
stm32u5xx_hal_pwr_ex.c, stm32u5xx_hal_rcc.c, stm32u5xx_hal_rcc_ex.c,
stm32u5xx_hal_uart.c, stm32u5xx_hal_uart_ex.c

#### `Src/` コンパイル対象 (26ファイル)、除外 (3ファイル)

**対象:** can.c, diskio.c, driver.c, driver_spindles.c, encoders.c, enet.c,
flash.c, i2c.c, ioports.c, ioports_analog.c, main.c, neopixel_gpo.c,
neopixel_pwm.c, neopixel_spi.c, serial.c, spi.c, stm32f4xx_hal_msp.c,
stm32f4xx_it.c, syscalls.c, sysmem.c, thcad2.c, timers.c, tmc_spi.c,
tmc_uart.c, triac_control.c, triac_mcodes.c, usb_serial.c

**除外(`excluding=`):** `system_stm32u5xx.c`, `w5x00_ll_driver.c`, `pwm.c`
（物理的には Src/ に存在するが .cproject で明示除外）

> `stm32f4xx_hal_msp.c`, `stm32f4xx_it.c` はファイル名に F4xx とあるが
> STM32U5プロジェクトに混在している（vendored時の名前残り）。実態はU5用の内容。

#### `grbl/` コンパイル対象 (44ファイル)

alarms.c, canbus.c, coolant_control.c, crc.c, crossbar.c, encoders.c,
errors.c, fs_device.c, gcode.c, grbllib.c, ioports.c, machine_limits.c,
messages.c, modbus.c, modbus_rtu.c, motion_control.c, my_plugin.c,
ngc_expr.c, ngc_flowctrl.c, ngc_params.c, nuts_bolts.c, nvs_buffer.c,
override.c, pid.c, planner.c, probe.c, protocol.c, regex.c, report.c,
settings.c, sleep.c, spindle_control.c, state_machine.c, stepper.c,
stepper2.c, stream.c, stream_file.c, stream_json.c, stream_passthru.c,
strutils.c, system.c, tool_change.c, utf8.c, vfs.c

### Release でコンパイルされるディレクトリ

`Core/` と `Drivers/` のみ。`Src/` と `grbl/` が**含まれない**。
Release構成は実用不可（CubeIDE上での設定が未完了）。

---

## 6. TrustZone 関連設定

`.cproject` の `defaults` 文字列に以下の記述あり：

```
|| NonSecure || || secure_nsclib.o || || None
```

| 項目 | 値 | 解釈 |
|------|----|------|
| TrustZone区分 | `NonSecure` | CubeIDE上でTrustZone NonSecureプロジェクトとして分類 |
| secure_nsclib.o | 文字列に記載あり | **実際にはリンクされていない** |

**実態（重要）:**
- `Debug/objects.mk` を確認すると `USER_OBJS :=`（空）、`LIBS :=`（空）。
- `secure_nsclib.o` はディスク上に存在しない。
- リンカスクリプト `STM32U585AIIXQ_FLASH.ld` にTrustZone分割なし（FLASH/RAMは単一領域）。
- **結論: TrustZone は実質的に無効。** `NonSecure` はCubeIDEプロジェクトテンプレートの
  デフォルト属性が残っているだけで、Secure/NS境界・NSC gateway・`secure_nsclib.o` は
  現行ビルドに影響していない。CMake移行時にこの設定は再現不要。

---

## 7. ツールチェーン

| 項目 | 値 | 出典 |
|------|----|------|
| CubeIDE内蔵ツールチェーン名 | **GNU Tools for STM32 14.3.rel1** | 各 `subdir.mk` ヘッダコメント |
| ツールチェーンプレフィックス | `arm-none-eabi-` | .cproject defaults |
| ツールチェーンパス変数 | `${gnu_tools_for_stm32_compiler_path}` | .cproject defaults（CubeIDE内部変数） |
| ホスト上での gcc-arm-none-eabi | **13.2.1 20231009** (パッケージ: 15:13.2.rel1-2) | `arm-none-eabi-gcc --version` |
| ホスト上での arm-none-eabi-size | **2.42** (binutils 2.42-1ubuntu1+23) | `arm-none-eabi-size --version` |

> CMake移行で使用するホスト側ツールチェーンは `apt install gcc-arm-none-eabi` で入る
> Ubuntu公式パッケージ版。CubeIDE内蔵の 14.3.rel1 とはバージョンが異なる可能性がある。
> バージョン不一致が問題になる場合は arm-developer.arm.com から tarball を直接取得する。

---

## 8. grbl/ サブツリーの組み込み方式

- `grbl/` は grblHAL コアの別管理リポジトリ（CLAUDE.md 参照）。CMakeLists.txt を持たない。
- CubeIDE の `sourceEntries` で `name="grbl"` をソースディレクトリとして丸ごと追加し、
  自動生成 `grbl/subdir.mk` + `grbl/kinematics/subdir.mk` が全 `.c` をコンパイル対象にしている。
- CMake化では `grbl/*.c` と `grbl/kinematics/*.c` を `GLOB` または明示リストで取り込む。
  `grbl/` に CMakeLists.txt を置いてサブディレクトリ化する必要はない（自作対象外）。

---

## 9. 成果物

| 項目 | パス |
|------|------|
| ELF | `Debug/grblHAL_UNO_Q.elf` |
| MAP | `Debug/grblHAL_UNO_Q.map` |
| LIST (objdump) | `Debug/grblHAL_UNO_Q.list` |
