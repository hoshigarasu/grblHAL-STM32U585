/*
  triac_control.c - TRIAC直接位相制御 for STM32U585 (Arduino UNO Q)

  AC Dimmerモジュールをゼロクロス(PB13/EXTI13)+TIM6ワンショットで位相制御する。
  DimmerLink I2C廃止版。ADC温度センサー/ファン制御は流用。

  ピン割当:
    DIM出力 : PB14 (D12) — GPIO_OUTPUT_PP, 点弧パルス
    Z-C入力 : PB13 (D13) — EXTI13, RISINGエッジ(実測確定)
    TEMP    : PA0         — ADC1_CH5
    FAN     : PB15        — GPIO_OUTPUT_PP

  Part of grblHAL-STM32U585 (https://github.com/hoshigarasu/grblHAL-STM32U585)
  Copyright (c) 2025-2026 Koji Tokura
  Licensed under GPLv3 (see LICENSE)
*/

#include "triac_control.h"
#include "driver.h"   /* grblと同じHAL+legacy+ボード定義の取り込み順序 */
#include <string.h>
#include <math.h>     /* sinf() — libm (-lm) 経由でリンク済み */

/* ── 内部状態 ────────────────────────────────────────────── */
static ADC_HandleTypeDef s_hadc1;

static volatile uint8_t  s_level    = 0;     /* 出力レベル 0-100 */
static volatile bool     s_enabled  = false;
static triac_status_t    s_status   = {0};

/* 位相遅延テーブル: s_delay_us[level] µs (level 0-100) */
static uint16_t          s_delay_us[101];

/* ゼロクロス debounce 用タイムスタンプ (HAL_GetTick() ms) */
static volatile uint32_t s_last_zc_tick = 0;

/* TIM6 ISR二段制御: false=位相待ち, true=パルス幅待ち */
static volatile bool     s_dim_phase = false;

/* ── レベル→位相遅延テーブル計算 ──────────────────────── */
/*
 * TRIAC位相制御の実効電圧:
 *   f(α) = 1 - α/π + sin(2α)/(2π)  (0≦α≦π)
 *   level=100 → α=0     → delay=0µs  (最大出力)
 *   level=50  → α=π/2   → delay=5000µs
 *   level=0   → 発火なし (sentinel=0xFFFF)
 * 各レベルに対するαを二分探索で求める。
 */
static void compute_delay_table(void)
{
    static const float PI = 3.14159265f;

    s_delay_us[0]   = 0xFFFFU;                 /* level 0: 発火しない (sentinel) */
    s_delay_us[100] = TRIAC_MIN_FIRE_DELAY_US; /* level 100: ラッチ保証下限遅延 */

    for (int L = 1; L <= 99; L++) {
        float target = (float)L / 100.0f;
        float lo = 0.0f, hi = PI;
        for (int i = 0; i < 32; i++) {
            float mid = (lo + hi) * 0.5f;
            float val = 1.0f - mid / PI + sinf(2.0f * mid) / (2.0f * PI);
            if (val > target) lo = mid;
            else              hi = mid;
        }
        float alpha = (lo + hi) * 0.5f;
        uint32_t d  = (uint32_t)(alpha / PI * (float)TRIAC_HALF_PERIOD_US + 0.5f);
        if (d < TRIAC_MIN_FIRE_DELAY_US)    d = TRIAC_MIN_FIRE_DELAY_US;
        if (d > TRIAC_HALF_PERIOD_US - 1U)  d = TRIAC_HALF_PERIOD_US - 1U;
        s_delay_us[L] = (uint16_t)d;
    }
}

/* ── DIM出力 GPIO初期化 (PB14) ──────────────────────────── */
static void dim_gpio_init(void)
{
    __HAL_RCC_GPIOB_CLK_ENABLE();
    GPIO_InitTypeDef g = {0};
    g.Pin   = TRIAC_DIM_PIN_Msk;
    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Pull  = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(TRIAC_DIM_PORT, &g);
    TRIAC_DIM_PORT->BRR = TRIAC_DIM_PIN_Msk;  /* 初期: LOW */
}

/* ── Z-C入力 EXTI初期化 (PB13, RISINGエッジ) ────────────── */
static void zc_exti_init(void)
{
    __HAL_RCC_GPIOB_CLK_ENABLE();
    GPIO_InitTypeDef g = {0};
    g.Pin   = TRIAC_ZC_PIN_Msk;
    g.Mode  = GPIO_MODE_IT_RISING;
    g.Pull  = GPIO_PULLDOWN;   /* アイドル時LOW維持 (実測: アイドルLOW) */
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(TRIAC_ZC_PORT, &g);
    /* NVIC は triac_enable() で有効化する(初期は無効) */
    HAL_NVIC_SetPriority(EXTI13_IRQn, 2, 0);
    /* EnableIRQ はここでは呼ばない */
}

/* ── TIM6 ワンショットタイマー初期化 ────────────────────── */
static void tim6_init(void)
{
    __HAL_RCC_TIM6_CLK_ENABLE();
    TIM6->CR1  = 0;
    TIM6->PSC  = TRIAC_TIM6_PSC;             /* 159 → 1MHz → 1µs/tick */
    TIM6->ARR  = TRIAC_HALF_PERIOD_US - 1U;  /* 初期値 (実行時に毎回上書き) */
    TIM6->CNT  = 0;
    TIM6->CR1  = TIM_CR1_OPM;               /* ワンショット: 溢れ後にCEN自動クリア */
    TIM6->DIER = TIM_DIER_UIE;              /* Update割り込み有効 */
    TIM6->EGR  = TIM_EGR_UG;               /* PSC/ARRを影響レジスタへ強制ロード */
    TIM6->SR   = 0;                          /* UGで立ったUIF をクリア */
    HAL_NVIC_SetPriority(TIM6_IRQn, 1, 0);  /* EXTIより高優先 (点弧タイミング精度) */
    HAL_NVIC_EnableIRQ(TIM6_IRQn);
}

/* ── FAN + アナログGPIO初期化 ───────────────────────────── */
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

    /* PA0 (TEMP): NTC-to-GND回路のため内部プルアップ必要 (STM32U5はANALOGモードでもPUPDR有効) */
    g.Pin  = GPIO_PIN_0;
    g.Mode = GPIO_MODE_ANALOG;
    g.Pull = GPIO_PULLUP;
    HAL_GPIO_Init(GPIOA, &g);
    /* PA1 (CUR-S): 未配線のためNOPULL */
    g.Pin  = GPIO_PIN_1;
    g.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &g);
}

/* ── ADC初期化 ───────────────────────────────────────────── */
static bool adc_init(void)
{
    /* STM32U5のADCは非同期クロックモードのみ。非同期ドメイン(CKMODE=00)は
     * ADCDACカーネルclock muxから給電される。HCLKは非同期ドメインに供給されない
     * ため、MSI由来のMSIK(本機はMSIをRANGE_0=48MHzで常用)を有効化し源にする。 */
    RCC_OscInitTypeDef osc = {0};
    osc.OscillatorType = RCC_OSCILLATORTYPE_MSIK;
    osc.MSIKState      = RCC_MSIK_ON;
    osc.MSIKClockRange = RCC_MSIKRANGE_0;  /* 48MHz */
    osc.PLL.PLLState   = RCC_PLL_NONE;     /* 既存PLL構成に触れない */
    if (HAL_RCC_OscConfig(&osc) != HAL_OK) return false;

    RCC_PeriphCLKInitTypeDef pclk_adc = {0};
    pclk_adc.PeriphClockSelection = RCC_PERIPHCLK_ADCDAC;
    pclk_adc.AdcDacClockSelection = RCC_ADCDACCLKSOURCE_MSIK;
    if (HAL_RCCEx_PeriphCLKConfig(&pclk_adc) != HAL_OK) return false;

    __HAL_RCC_ADC1_CLK_ENABLE();

    s_hadc1.Instance                   = ADC1;
    s_hadc1.Init.ClockPrescaler        = ADC_CLOCK_ASYNC_DIV2;  /* MSIK48MHz/2=24MHz */
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
     * 本構成では完走せずHAL_ADC_ERROR_INTERNALになる。しきい値判定には不要。 */
    return true;
}

static uint16_t adc_read(uint32_t channel, bool *ok)
{
    ADC_ChannelConfTypeDef cfg = {0};
    cfg.Channel      = channel;
    cfg.Rank         = ADC_REGULAR_RANK_1;
    cfg.SamplingTime = ADC_SAMPLETIME_391CYCLES;
    cfg.SingleDiff   = ADC_SINGLE_ENDED;
    cfg.OffsetNumber = ADC_OFFSET_NONE;
    cfg.Offset       = 0;
    HAL_ADC_ConfigChannel(&s_hadc1, &cfg);
    if (HAL_ADC_Start(&s_hadc1) != HAL_OK) { *ok = false; return 0; }
    if (HAL_ADC_PollForConversion(&s_hadc1, 10) != HAL_OK) { *ok = false; return 0; }
    uint16_t v = (uint16_t)HAL_ADC_GetValue(&s_hadc1);
    /* HAL_ADC_Stopを呼ばずADENを維持する (CLAUDE.md §4 ADC節参照) */
    *ok = true;
    return v;
}

/* ── TIM6 ワンショット起動ヘルパー ────────────────────────── */
static inline void tim6_start(uint16_t delay_us)
{
    TIM6->CR1 &= ~TIM_CR1_CEN;    /* 停止 */
    TIM6->CNT  = 0;
    TIM6->ARR  = delay_us - 1U;
    TIM6->SR   = 0;               /* 既存フラグクリア */
    TIM6->CR1 |= TIM_CR1_CEN;    /* 起動 (OPMで溢れ後自動停止) */
}

/* ── ゼロクロス EXTI ハンドラ (PB13 → EXTI13) ──────────── */
void EXTI13_IRQHandler(void)
{
    /* EXTI13 pending クリア (STM32U5: RPR1/FPR1 write-to-clear) */
    __HAL_GPIO_EXTI_CLEAR_IT(TRIAC_ZC_PIN_Msk);

    /* デバウンス: 直前の有効エッジから3ms以内は無視 */
    uint32_t now = HAL_GetTick();
    if (now - s_last_zc_tick < TRIAC_ZC_DEBOUNCE_MS) return;
    s_last_zc_tick = now;

    if (!s_enabled) return;

    uint16_t d = s_delay_us[s_level];
    if (d == 0xFFFFU) return;  /* level 0: 発火しない */

    /* TIM6を位相遅延モードで起動 */
    s_dim_phase = false;
    tim6_start(d);
}

/* ── TIM6 Update ハンドラ (位相遅延満了 / パルス幅満了) ─── */
void TIM6_IRQHandler(void)
{
    TIM6->SR = 0;  /* UIF クリア */

    if (!s_dim_phase) {
        /* 位相遅延満了 → DIM HIGH + パルス幅タイマー起動 */
        s_dim_phase = true;
        TRIAC_DIM_PORT->BSRR = TRIAC_DIM_PIN_Msk;        /* DIM = HIGH */
        tim6_start((uint16_t)TRIAC_DIM_PULSE_US);
    } else {
        /* パルス幅満了 → DIM LOW */
        s_dim_phase = false;
        TRIAC_DIM_PORT->BRR  = TRIAC_DIM_PIN_Msk;        /* DIM = LOW */
        /* TIM6はOPMで既に停止済み */
    }
}

/* ── 初期化 ──────────────────────────────────────────────── */
void triac_init(void)
{
    memset(&s_status, 0, sizeof(s_status));
    compute_delay_table();
    gpio_init();
    dim_gpio_init();
    zc_exti_init();
    tim6_init();
    bool adc_ok = adc_init();
    s_status.init_error = !adc_ok;
    s_enabled = false;
    s_level   = 0;
}

/* ── 出力レベル設定 (0-100) ──────────────────────────────── */
void triac_set_level(uint8_t level)
{
    if (level > TRIAC_LEVEL_MAX) level = TRIAC_LEVEL_MAX;
    s_level = level;
    /* 次のゼロクロスから s_delay_us[s_level] が適用される */
}

uint8_t triac_get_level(void) { return s_level; }

/* ── 有効/無効 ───────────────────────────────────────────── */
void triac_enable(void)
{
    s_enabled = true;
    __HAL_GPIO_EXTI_CLEAR_IT(TRIAC_ZC_PIN_Msk);  /* 残留フラグをクリア */
    s_last_zc_tick = HAL_GetTick();               /* デバウンス初期化 */
    HAL_NVIC_EnableIRQ(EXTI13_IRQn);
}

void triac_disable(void)
{
    s_enabled = false;
    HAL_NVIC_DisableIRQ(EXTI13_IRQn);
    TIM6->CR1 &= ~TIM_CR1_CEN;           /* TIM6 停止 */
    TIM6->SR   = 0;
    s_dim_phase = false;
    TRIAC_DIM_PORT->BRR = TRIAC_DIM_PIN_Msk;  /* DIM = LOW (安全) */
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

    s_status.delay_us = (s_level < 101U) ? s_delay_us[s_level] : 0xFFFFU;
    return s_status;
}

triac_status_t triac_get_status(void)
{
    s_status.delay_us = (s_level < 101U) ? s_delay_us[s_level] : 0xFFFFU;
    return s_status;
}

