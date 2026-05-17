

https://github.com/user-attachments/assets/cd053115-391d-498e-9f35-c43bef3a23d7

# grblHAL-STM32U585

**grblHAL ported to STM32U585 — enabling the Arduino UNO Q as a standalone CNC controller**

This is the first known port of [grblHAL](https://github.com/grblHAL) to the STM32U585 MCU.
The immediate target is the [Arduino UNO Q](https://store.arduino.cc/products/uno-q), a dual-chip
board pairing a Qualcomm QRB2210 (Linux, `ttyHS1`) with an STM32U585 (real-time MCU).

With this port, the UNO Q becomes a self-contained CNC platform:

```
┌─────────────────────────────────────────────┐
│              Arduino UNO Q                  │
│                                             │
│  QRB2210 (Linux / Debian 13)                │
│    └─ rs274ngc G-code interpreter           │
│    └─ canon-grbl-bridge (Python)            │
│         │ LPUART1 / ttyHS1 (115200 baud)    │
│  STM32U585 (grblHAL)                        │
│    └─ real-time step generation             │
│    └─ Arduino CNC Shield V3                 │
└─────────────────────────────────────────────┘
```

No Mesa card. No external motion controller. One board.

---

## Hardware

| Item | Detail |
|------|--------|
| Board | Arduino UNO Q |
| MCU | STM32U585AI (2 MB flash, 784 KB RAM) |
| Linux SoC | Qualcomm QRB2210 |
| Internal UART | LPUART1 — PG7 (TX, AF8) / PG8 (RX, AF8) = `ttyHS1` |
| CNC shield | Arduino CNC Shield V3 (A4988 / DRV8825 drivers) |

### CNC Shield V3 pin assignment

| Signal | Arduino pin | Active level |
|--------|-------------|--------------|
| X step | D2 | — |
| Y step | D3 | — |
| Z step | D4 | — |
| X dir  | D5 | — |
| Y dir  | D6 | — |
| Z dir  | D7 | — |
| Enable | D8 | LOW |
| X/Y/Z limit | D9/D10/D11 | — |

---

## Build

**Environment**: Windows, STM32CubeIDE

```
Project path : C:\Tools\grblHAL_UNO_Q\
ELF output   : C:\Tools\grblHAL_UNO_Q\Debug\grblHAL_UNO_Q.elf
Build size   : ~303 KB (Debug)
```

Build configuration: default Debug. No special flags required beyond those in `my_machine.h`.

---

## Flash

The QRB2210 runs OpenOCD via GPIO bitbang (SWD). The `arduino-router` service must be
stopped before flashing to prevent the STM32 reset line from triggering a QRB2210 shutdown.

```bash
# 1. Stop services (mandatory)
ssh -t uno-q 'sudo systemctl stop arduino-router.service arduino-router-serial.service arduino-app-cli.service'

# 2. Transfer ELF from build host
scp /home/koji/public/grblHAL_UNO_Q.elf uno-q:/tmp/grblHAL_UNO_Q.elf

# 3. Flash
ssh -t uno-q \
  'sudo /opt/openocd/bin/openocd -s /opt/openocd -f openocd_gpiod.cfg \
   -c "stm32u5.ap0 configure -event examine-end {}" \
   -c "reset_config srst_only srst_push_pull srst_nogate connect_assert_srst" \
   -c "init; reset halt" \
   -c "flash write_image erase /tmp/grblHAL_UNO_Q.elf" \
   -c "reset; shutdown"'
```

> **`connect_assert_srst` + `srst_nogate` are mandatory.**
> Without these the CPU examine fails intermittently on cold start.

---

## UART connection check

```bash
# Stop service and enable level shifter
ssh -t uno-q 'sudo systemctl stop arduino-router.service'
ssh -t uno-q 'sudo gpioset -c /dev/gpiochip1 -t0 70=1'

# Terminal 1 — listen
ssh -t uno-q 'sudo bash -c "stty -F /dev/ttyHS1 115200 raw -echo -onlcr -crtscts; cat /dev/ttyHS1"'

# Terminal 2 — reset MCU (after Terminal 1 is blocking)
ssh -t uno-q \
  'sudo /opt/openocd/bin/openocd -s /opt/openocd -f openocd_gpiod.cfg \
   -c "stm32u5.ap0 configure -event examine-end {}" \
   -c "reset_config srst_only srst_push_pull srst_nogate" \
   -c "init; reset run; shutdown"'
```

Expected output in Terminal 1:
```
GrblHAL 1.1f ['$' or '$HELP' for help]
```

---

## Initial grblHAL settings

Apply once after flashing (settings survive reset via internal flash):

```python
# Run on QRB2210
import serial, time

cmds = [
    "$100=100.000", "$101=100.000", "$102=100.000",  # steps/mm (XYZ)
    "$110=5000.000", "$111=5000.000", "$112=1000.000", # max feed rate mm/min
    "$120=500.000",  "$121=500.000",  "$122=20.000",   # acceleration mm/s²
    "$130=200.000", "$131=200.000", "$132=200.000",  # max travel mm
]
s = serial.Serial("/dev/ttyHS1", 115200, timeout=2)
time.sleep(0.5)
s.reset_input_buffer()
for cmd in cmds:
    s.write((cmd + "\n").encode())
    time.sleep(0.3)
    print(f"{cmd} -> {s.read_all().decode(errors='replace').strip()}")
s.close()
```

Adjust `$100`/`$101` (steps/mm) to match your motor, microstepping, and belt/leadscrew pitch.

---

## GPIO reference (QRB2210 side)

| GPIO | Function | Note |
|------|----------|------|
| GPIO37 | BOOT0 | 0 = normal boot, 1 = STM32 bootloader |
| GPIO38 | QRB2210 shutdown line | **Never assert** |
| GPIO70 | Level shifter Enable | Set to 1 when arduino-router is stopped |

SSH alias (assumes `~/.ssh/config` entry for `uno-q`):
```
ssh -t uno-q
```

---

## Key registers (normal operation)

| Register | Address | Expected value |
|----------|---------|----------------|
| LPUART1_CR1 | 0x46002400 | 0x2D (UE, TE, RE, RXNEIE) |
| LPUART1_BRR | 0x4600240C | 0x56CE3 (160 MHz / 115200) |
| GPIOG_MODER | 0x42021800 | 0xFFFEBFFF (PG7, PG8 = AF) |
| GPIOG_AFRL  | 0x42021820 | 0x80000000 (PG7 = AF8) |
| GPIOG_AFRH  | 0x42021824 | 0x00000008 (PG8 = AF8) |
| PWR_SVMCR   | 0x46020810 | bit29 = 1 (VDDIO2RDY) |

---

## What was solved

Porting grblHAL to STM32U585 required resolving four distinct problem layers:

### Layer 1 — Flash procedure
OpenOCD CPU examine fails without `connect_assert_srst`.
**Fix**: add `srst_nogate connect_assert_srst` to the reset config.

### Layer 2 — Wrong UART
The original code targeted USART1 on PB6/PB7 (external Arduino header pins D0/D1),
not the internal QRB2210 link.
**Fix**: Switch to LPUART1 on PG7/PG8 (AF8), routed internally to `ttyHS1`.

### Layer 3 — STM32U585-specific differences
Four issues specific to STM32U5 vs STM32F4:

| Problem | Fix |
|---------|-----|
| Port G power-isolated by default | Call `HAL_PWREx_EnableVddIO2()` before GPIO init |
| `HAL_Init()` called too late | Move to first line of `main()` |
| LPUART BRR formula differs | Use `256 * CLK / baud` instead of standard UART formula |
| Register names changed (SR→ISR, DR→RDR/TDR) | Add `_U_SR` / `_U_RDR` / `_U_TDR` compatibility macros |

### Layer 4 — driver_setup() hangs forever (root cause)
`SysTick_Handler` in `stm32u5xx_it.c` was calling `HAL_IncTick()` but not `Driver_IncTick()`.
The grblHAL `driver_delay()` counter never decremented, causing `driver_setup()`'s 100 ms
wait loop to spin forever. grblHAL never sent its startup message.
**Fix**: Add `Driver_IncTick()` to `SysTick_Handler` in `Core/Src/stm32u5xx_it.c`.

### Flash write failure
`FLASH_ENABLE` was undefined (defaulting to 0), so `memcpy_to_flash` was compiled out.
Additionally, `_EEPROM_Emul_Start` at `0x081FE000` is in Flash Bank 2, but the erase
always targeted Bank 1.
**Fix**: Define `FLASH_ENABLE 1` in `my_machine.h`; add Bank 2 detection in `flash.c`.

---

## Companion project

[canon-grbl-bridge](https://github.com/hoshigarasu/canon-grbl-bridge) —
a Python bridge that runs on the QRB2210, feeds G-code through the `rs274ngc` interpreter
(from `linuxcnc-uspace`), and streams the resulting canonical motion commands to grblHAL
over `ttyHS1`. Canned cycles, subroutines, and coordinate transforms are handled by
`rs274ngc`; the bridge translates canon calls to `G0`/`G1`/`G2`/`G3` and sends them
with proper flow control (one command per `ok` response).

---

## Forbidden operations

| Action | Reason |
|--------|--------|
| OpenOCD reset without stopping services | STM32 reset triggers QRB2210 shutdown detection |
| `gpioset 38=1` | Immediate QRB2210 shutdown |
| Unbind `ttyHS1` | Cannot be rebound without reboot |
| Write APB registers while halted | APB clock gating makes writes ineffective |

---

## Roadmap

The Arduino UNO Q exposes a rich set of expansion connectors on its underside
(JDIGITAL, JANALOG, JMEDIA, and auxiliary interfaces) that go well beyond the
standard Arduino header footprint used by the CNC Shield V3.
These open a clear path toward a significantly more capable CNC platform:

### Axis expansion (target: 6 axes)
The current 3-axis configuration uses D2–D8 on the top Arduino headers.
The underside connectors provide additional GPIO and timer-capable pins that
could drive three more step/dir channels — reaching 6 axes (X Y Z A B C)
without external multiplexing or additional hardware.

### Position feedback
Closed-loop operation requires encoder inputs. The STM32U585 has hardware
timer channels suitable for quadrature decoding. Mapping these through the
underside connectors to external encoders (or direct motor feedback) would
enable position verification and stall detection.

### Peripheral expansion
The underside interfaces also include SPI, I2C, and additional UARTs — enough
to add:
- Trinamic stepper driver control (SPI/UART)
- Tool length probe and touch-off inputs
- Spindle encoder for rigid tapping (`G33`)
- I2C display or MPG pendant

### Software side
Firmware expansion on the STM32U585 is complemented by the
[canon-grbl-bridge](https://github.com/hoshigarasu/canon-grbl-bridge) evolving
to support position readback, feed hold/resume, and multi-axis coordination
on the QRB2210 side.

---

## License

grblHAL is licensed under GPLv3. This port inherits the same license.
See [LICENSE](LICENSE) for details.
