#!/usr/bin/env bash
# check-completeness.sh
# Verifies that all owned/self-authored files are present after a clean clone.
# Run from the repository root.
# Upstream grblHAL / CubeMX files (driver.h, grbl/*, Drivers/*) are NOT checked here
# — they are provided by the STM32CubeIDE project at C:\Tools\grblHAL_UNO_Q\.

set -euo pipefail
PASS=0; FAIL=0

check() {
    if [ -f "$1" ]; then
        echo "OK   $1"
        PASS=$((PASS+1))
    else
        echo "MISSING $1"
        FAIL=$((FAIL+1))
    fi
}

echo "=== grblHAL-STM32U585 completeness check ==="
echo ""
echo "--- UNO Q owned files ---"
check "Inc/boards/uno_q_cnc_map.h"
check "Inc/triac_control.h"
check "Inc/triac_mcodes.h"
check "Src/triac_control.c"
check "Src/triac_mcodes.c"

echo ""
echo "--- grblHAL upstream files (in this repo) ---"
check "Src/driver.c"
check "Src/serial.c"
check "Src/main.c"
check "Src/flash.c"
check "Inc/flash.h"
check "Inc/my_machine.h"
check "Core/Src/stm32u5xx_it.c"
check "STM32U585AIIXQ_FLASH.ld"

echo ""
echo "--- triac_mcodes_register() integration in driver.c ---"
if grep -q 'triac_mcodes_register' Src/driver.c; then
    echo "OK   Src/driver.c contains triac_mcodes_register()"
else
    echo "MISSING triac_mcodes_register() call in Src/driver.c"
    FAIL=$((FAIL+1))
fi

echo ""
echo "--- Result ---"
echo "PASS: $PASS  FAIL: $FAIL"
[ "$FAIL" -eq 0 ] && echo "All OK." && exit 0
echo "Above MISSING items must be added before building." && exit 1
