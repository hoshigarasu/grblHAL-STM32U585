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
#include "stm32u5xx_hal.h"
#include <string.h>

/* ── 内部状態 ────────────────────────────────────────────── */
static I2C_HandleTypeDef s_hi2c2;
static ADC_HandleTypeDef s_hadc1;

static volatile triac_voltage_t s_voltage = TRIAC_VOLTAGE_OFF;
static volatile bool            s_enabled = false;
static triac_status_t           s_status  = {0};

/* ── I2C初期化 ───────────────────────────────────────────── */
static void i2c_init(void)
{
    __HAL_RCC_GPIOB_CLK_ENABLE();
    TRIAC_I2C_CLK_EN();

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
    HAL_I2C_Init(&s_hi2c2);
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
static void adc_init(void)
{
    __HAL_RCC_ADC1_CLK_ENABLE();

    s_hadc1.Instance                   = ADC1;
    s_hadc1.Init.ClockPrescaler        = ADC_CLOCK_ASYNC;
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
    HAL_ADC_Init(&s_hadc1);
    HAL_ADCEx_Calibration_Start(&s_hadc1, ADC_CALIB_OFFSET, ADC_SINGLE_ENDED);
}

static uint16_t adc_read(uint32_t channel)
{
    ADC_ChannelConfTypeDef cfg = {0};
    cfg.Channel      = channel;
    cfg.Rank         = ADC_REGULAR_RANK_1;
    cfg.SamplingTime = ADC_SAMPLETIME_391CYCLES; /* ADC1用最大値、温度/電流センサーに十分 */
    cfg.SingleDiff   = ADC_SINGLE_ENDED;
    cfg.OffsetNumber = ADC_OFFSET_NONE;
    cfg.Offset       = 0;
    HAL_ADC_ConfigChannel(&s_hadc1, &cfg);
    HAL_ADC_Start(&s_hadc1);
    if (HAL_ADC_PollForConversion(&s_hadc1, 10) != HAL_OK) return 0;
    uint16_t v = (uint16_t)HAL_ADC_GetValue(&s_hadc1);
    HAL_ADC_Stop(&s_hadc1);
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
    i2c_init();
    adc_init();
    memset(&s_status, 0, sizeof(s_status));
    dimmerlink_set_level(0);
    s_enabled = false;
    s_voltage  = TRIAC_VOLTAGE_OFF;
}

/* ── 電圧設定 ────────────────────────────────────────────── */
void triac_set_voltage(triac_voltage_t v)
{
    s_voltage = v;
    if (!s_enabled) return;

    uint8_t level;
    switch (v) {
        case TRIAC_VOLTAGE_20V: level = TRIAC_LEVEL_20V; break;
        case TRIAC_VOLTAGE_30V: level = TRIAC_LEVEL_30V; break;
        case TRIAC_VOLTAGE_40V: level = TRIAC_LEVEL_40V; break;
        default:                level = 0;               break;
    }
    dimmerlink_set_level(level);
}

triac_voltage_t triac_get_voltage(void) { return s_voltage; }

/* ── 有効/無効 ───────────────────────────────────────────── */
void triac_enable(void)
{
    s_enabled = true;
    triac_set_voltage(s_voltage);
}

void triac_disable(void)
{
    s_enabled = false;
    dimmerlink_set_level(0);
}

/* ── センサー更新 + ファン制御 ───────────────────────────── */
triac_status_t triac_update_sensors(void)
{
    s_status.temp_adc = adc_read(TRIAC_ADC_TEMP_CH);
    s_status.cur_adc  = adc_read(TRIAC_ADC_CUR_CH);

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
