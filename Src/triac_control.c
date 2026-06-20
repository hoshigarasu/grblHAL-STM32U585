/*
  triac_control.c - TRIAC power control for STM32U585 (Arduino UNO Q)

  DimmerLink I2C controller + ADC temperature/current sensing + FAN GPIO.
  Integrated into grblHAL via M810-M816 user M-codes (triac_mcodes.c).

  Part of grblHAL-STM32U585 (https://github.com/hoshigarasu/grblHAL-STM32U585)

  Copyright (c) 2025-2026 Koji Tokura

  grblHAL is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  grblHAL is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with grblHAL. If not, see <http://www.gnu.org/licenses/>.
*/

#include "triac_control.h"
#include "driver.h" /* grblと同じHAL+legacy+ボード定義の取り込み順序 (ADC_CLOCK_SYNC_PCLK_DIV4等) */
#include <string.h>

/* ── 内部状態 ────────────────────────────────────────────── */
static I2C_HandleTypeDef s_hi2c2;
static ADC_HandleTypeDef s_hadc1;

static volatile uint8_t         s_level   = 0; /* 現在の出力レベル 0-100 */
static volatile bool            s_enabled = false;
static triac_status_t           s_status  = {0};

/* ── I2C初期化 ───────────────────────────────────────────── */
static bool i2c_init(void)
{
    __HAL_RCC_GPIOB_CLK_ENABLE();
    TRIAC_I2C_CLK_EN();

    /* STM32U5: I2C2カーネルクロックを明示選択 (PCLK1 = リセット値と同じ、診断のため明示) */
    RCC_PeriphCLKInitTypeDef pclk_i2c2 = {0};
    pclk_i2c2.PeriphClockSelection = RCC_PERIPHCLK_I2C2;
    pclk_i2c2.I2c2ClockSelection   = RCC_I2C2CLKSOURCE_PCLK1;
    HAL_RCCEx_PeriphCLKConfig(&pclk_i2c2);

    GPIO_InitTypeDef g = {0};
    g.Pin       = TRIAC_I2C_SDA_PIN | TRIAC_I2C_SCL_PIN;
    g.Mode      = GPIO_MODE_AF_OD;
    g.Pull      = GPIO_PULLUP;
    g.Speed     = GPIO_SPEED_FREQ_LOW;
    g.Alternate = TRIAC_I2C_AF;
    HAL_GPIO_Init(TRIAC_I2C_PORT, &g);

    s_hi2c2.Instance              = TRIAC_I2C;
    s_hi2c2.Init.Timing           = 0x10909CEC; /* 100kHz @ 160MHz PCLK1 */
    s_hi2c2.Init.OwnAddress1      = 0;
    s_hi2c2.Init.AddressingMode   = I2C_ADDRESSINGMODE_7BIT;
    s_hi2c2.Init.DualAddressMode  = I2C_DUALADDRESS_DISABLED;
    s_hi2c2.Init.GeneralCallMode  = I2C_GENERALCALL_DISABLED;
    s_hi2c2.Init.NoStretchMode    = I2C_NOSTRETCH_DISABLED;
    if (HAL_I2C_Init(&s_hi2c2) != HAL_OK) {
        return false;
    }
    return true;
}

/* ── GPIO初期化 (FAN + アナログ) ────────────────────────── */
static void gpio_init(void)
{
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    GPIO_InitTypeDef g = {0};

    /* PB15: ファンON/OFF出力 */
    g.Pin   = TRIAC_FAN_PIN_Msk;
    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Pull  = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(TRIAC_FAN_PORT, &g);
    HAL_GPIO_WritePin(TRIAC_FAN_PORT, TRIAC_FAN_PIN_Msk, GPIO_PIN_RESET);

    /* PA0, PA1: アナログ入力 */
    g.Pin  = GPIO_PIN_0 | GPIO_PIN_1;
    g.Mode = GPIO_MODE_ANALOG;
    g.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &g);
}

/* ── ADC初期化 ───────────────────────────────────────────── */
/*
 * STM32U585はADC1とADC4が存在する。
 * PA0=ADC1_IN5, PA1=ADC1_IN6。
 * HAL定数はSTM32U5xxのstm32u5xx_hal_adc.hに従う。
 */
static bool adc_init(void)
{
    /* STM32U5のADCは非同期クロックモードのみ。非同期ドメイン(CKMODE=00)は
     * ADCDACカーネルclock muxから給電される。HCLKは非同期ドメインに供給されない
     * ため、MSI由来のMSIK(本機はMSIをRANGE_0=48MHzで常用)を有効化し源にする。 */
    RCC_OscInitTypeDef osc = {0};
    osc.OscillatorType = RCC_OSCILLATORTYPE_MSIK;
    osc.MSIKState      = RCC_MSIK_ON;
    osc.MSIKClockRange = RCC_MSIKRANGE_0; /* 48MHz */
    osc.PLL.PLLState   = RCC_PLL_NONE;    /* 既存PLL構成に触れない */
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) return false;

    RCC_PeriphCLKInitTypeDef pclk_adc = {0};
    pclk_adc.PeriphClockSelection = RCC_PERIPHCLK_ADCDAC;
    pclk_adc.AdcDacClockSelection = RCC_ADCDACCLKSOURCE_MSIK;
    if (HAL_RCCEx_PeriphCLKConfig(&pclk_adc) != HAL_OK) return false;

    __HAL_RCC_ADC1_CLK_ENABLE();

    s_hadc1.Instance                   = ADC1;
    s_hadc1.Init.ClockPrescaler        = ADC_CLOCK_ASYNC_DIV2; /* MSIK48MHz/2=24MHz, 仕様内 */
    s_hadc1.Init.Resolution            = ADC_RESOLUTION_12B;
    s_hadc1.Init.ScanConvMode          = ADC_SCAN_DISABLE;
    s_hadc1.Init.EOCSelection          = ADC_EOC_SINGLE_CONV;
    s_hadc1.Init.LowPowerAutoWait      = DISABLE;
    s_hadc1.Init.ContinuousConvMode    = DISABLE;
    s_hadc1.Init.NbrOfConversion       = 1;
    s_hadc1.Init.DiscontinuousConvMode = DISABLE;
    s_hadc1.Init.ExternalTrigConv      = ADC_SOFTWARE_START;
    s_hadc1.Init.ExternalTrigConvEdge  = ADC_EXTERNALTRIG_EDGE_NONE;
    s_hadc1.Init.DMAContinuousRequests = DISABLE;
    s_hadc1.Init.Overrun               = ADC_OVR_DATA_OVERWRITTEN;
    s_hadc1.Init.OversamplingMode      = DISABLE;
    if (HAL_ADC_Init(&s_hadc1) != HAL_OK) return false;

    /* キャリブレーションは意図的に行わない。
     * STM32U585(dev_id=0x482, rev>=0x3000)はHALの「拡張キャリブレーション」経路に入り、
     * 内部でADC_Enable→CALINDEX/CALFACT2操作→ADCAL待ちという低レベル手順を踏むが、
     * 本構成では完走せずHAL_ADC_ERROR_INTERNALになる。
     * キャリブレーションはオフセット精密補正(数LSB)であり、TEMP/CURのしきい値判定
     * (実機で電圧計校正する前提, triac_control.h参照)には影響しないため不要。
     * 変換自体(HAL_ADC_Start/Poll)はキャリブレーション無しで正常動作する。 */
    return true;
}

static uint16_t adc_read(uint32_t channel, bool *ok)
{
    ADC_ChannelConfTypeDef cfg = {0};
    cfg.Channel      = channel;
    cfg.Rank         = ADC_REGULAR_RANK_1;
    cfg.SamplingTime = ADC_SAMPLETIME_391CYCLES; /* ADC1用最大値、温度/電流センサーに十分 */
    cfg.SingleDiff   = ADC_SINGLE_ENDED;
    cfg.OffsetNumber = ADC_OFFSET_NONE;
    cfg.Offset       = 0;
    HAL_ADC_ConfigChannel(&s_hadc1, &cfg);
    if (HAL_ADC_Start(&s_hadc1) != HAL_OK) {
        *ok = false;
        return 0;
    }
    if (HAL_ADC_PollForConversion(&s_hadc1, 10) != HAL_OK) {
        *ok = false;
        return 0;
    }
    uint16_t v = (uint16_t)HAL_ADC_GetValue(&s_hadc1);
    /* HAL_ADC_Stopを呼ばずADENを維持する。STM32U5のADC_Enableは「ADEN=1なら
     * 前提チェックを素通り」するが、Stopで中途半端にdisableするとADEN残留状態で
     * 次のADC_Enable前提チェック(ADEN!=0で即HAL_ERROR)に引っかかる。
     * enable維持で単発変換を繰り返すことで2回目以降のenableを発生させない。 */
    *ok = true;
    return v;
}

/* ── DimmerLink I2C送信 ──────────────────────────────────── */
static bool dimmerlink_set_level(uint8_t level)
{
    if (level > 100U) level = 100U;
    uint8_t cmd[3] = {
        DIMMERLINK_CMD_SET_LEVEL,
        DIMMERLINK_CHANNEL,
        level
    };
    HAL_StatusTypeDef ret = HAL_I2C_Master_Transmit(
        &s_hi2c2,
        TRIAC_I2C_ADDR,
        cmd,
        sizeof(cmd),
        TRIAC_I2C_TIMEOUT_MS
    );
    s_status.i2c_error = (ret != HAL_OK);
    return (ret == HAL_OK);
}

/* ── 初期化 ──────────────────────────────────────────────── */
void triac_init(void)
{
    gpio_init();
    memset(&s_status, 0, sizeof(s_status));
    /* 初期化失敗は init_error に記録するが happy path の動作は変えない */
    bool i2c_ok = i2c_init();
    bool adc_ok = adc_init();
    s_status.init_error = !(i2c_ok && adc_ok);
    dimmerlink_set_level(0);
    s_enabled = false;
    s_level    = 0;
}

/* ── 出力レベル設定 (0-100) ──────────────────────────────── */
void triac_set_level(uint8_t level)
{
    if (level > TRIAC_LEVEL_MAX) level = TRIAC_LEVEL_MAX; /* クランプ */
    s_level = level;
    if (!s_enabled) return;
    dimmerlink_set_level(level);
}

uint8_t triac_get_level(void) { return s_level; }

/* ── 有効/無効 ───────────────────────────────────────────── */
void triac_enable(void)
{
    s_enabled = true;
    triac_set_level(s_level);
}

void triac_disable(void)
{
    s_enabled = false;
    dimmerlink_set_level(0);
}

/* ── センサー更新 + ファン制御 ───────────────────────────── */
triac_status_t triac_update_sensors(void)
{
    bool temp_ok, cur_ok;
    s_status.temp_adc = adc_read(TRIAC_ADC_TEMP_CH, &temp_ok);
    s_status.cur_adc  = adc_read(TRIAC_ADC_CUR_CH,  &cur_ok);

    /* ADC読み取り失敗 → フェイルセーフ: FAN強制ON + 出力停止 */
    if (!temp_ok || !cur_ok) {
        HAL_GPIO_WritePin(TRIAC_FAN_PORT, TRIAC_FAN_PIN_Msk, GPIO_PIN_SET);
        s_status.fan_on = true;
        if (!s_status.overheat) {
            s_status.overheat = true;
            triac_disable();
        }
        return s_status;
    }

    /* 過熱保護 */
    if (s_status.temp_adc >= TRIAC_TEMP_OVERHEAT_ADC) {
        if (!s_status.overheat) {
            s_status.overheat = true;
            triac_disable();
            HAL_GPIO_WritePin(TRIAC_FAN_PORT, TRIAC_FAN_PIN_Msk, GPIO_PIN_SET);
            s_status.fan_on = true;
        }
    } else {
        s_status.overheat = false;
    }

    /* ファン自動制御 (ヒステリシス) */
    if (!s_status.overheat) {
        if (!s_status.fan_on &&
            s_status.temp_adc >= TRIAC_TEMP_FAN_ON_ADC) {
            HAL_GPIO_WritePin(TRIAC_FAN_PORT, TRIAC_FAN_PIN_Msk, GPIO_PIN_SET);
            s_status.fan_on = true;
        } else if (s_status.fan_on &&
                   s_status.temp_adc < TRIAC_TEMP_FAN_OFF_ADC) {
            HAL_GPIO_WritePin(TRIAC_FAN_PORT, TRIAC_FAN_PIN_Msk, GPIO_PIN_RESET);
            s_status.fan_on = false;
        }
    }

    return s_status;
}

triac_status_t triac_get_status(void) { return s_status; }
