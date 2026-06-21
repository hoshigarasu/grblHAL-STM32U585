/**
 * triac_control.h
 * STM32U585 TRIAC直接位相制御 + センサー/ファン
 *
 * ハードウェア構成:
 *   DIM出力 : PB14 (D12) — GPIO_OUTPUT_PP, TRIAC点弧パルス
 *   Z-C入力 : PB13 (D13) — EXTI13, RISINGエッジでゼロクロス検出
 *   TEMP    : PA0  (A0)  — ADC1_CH5, 温度センサーアナログ出力
 *   CUR-S   : PA1  (A1)  — ADC1_CH6, 電流センサー(未配線, 読み値無意味)
 *   FAN     : PB15 (D11) — GPIO出力, ファンON/OFF
 *
 * Z-C信号実測値 (2026-06-21 ロジックアナライザ):
 *   アイドルレベル: LOW、ゼロクロス時: RISINGパルス (~99µs)
 *   パルス間隔: ~10ms (50Hz AC、関東/東日本)
 *
 * grblHAL Mコード (triac_mcodes.c):
 *   M810 OFF  M811 P<0-100>  M814 ENABLE  M815 DISABLE  M816 STATUS
 */

#ifndef TRIAC_CONTROL_H
#define TRIAC_CONTROL_H

#include <stdint.h>
#include <stdbool.h>

/* ── DIM / Z-C ピン ─────────────────────────────────────── */
#define TRIAC_DIM_PORT          GPIOB
#define TRIAC_DIM_PIN           14               /* PB14 = D12 */
#define TRIAC_DIM_PIN_Msk       (1u << TRIAC_DIM_PIN)

#define TRIAC_ZC_PORT           GPIOB
#define TRIAC_ZC_PIN            13               /* PB13 = D13 */
#define TRIAC_ZC_PIN_Msk        (1u << TRIAC_ZC_PIN)

/* ── AC電源パラメータ ────────────────────────────────────── */
#define TRIAC_AC_HZ             50U              /* 50Hz (関東/東日本, 実測確認済み) */
#define TRIAC_HALF_PERIOD_US    (1000000U / TRIAC_AC_HZ / 2U)  /* 10000µs */

/* ── TIM6 ─────────────────────────────────────────────────── */
/* PCLK1 = 160MHz (APB1 DIV1), TIM6入力クロック = 160MHz      */
#define TRIAC_TIM6_PSC          159U             /* 160MHz/160=1MHz → 1µs/tick */

/* ── DIM点弧パルス幅 (実機調整用) ────────────────────────── */
#define TRIAC_DIM_PULSE_US      100U             /* 100µs (TRIACラッチ確認後に調整) */

/* ── 最小点弧遅延 ────────────────────────────────────────── */
/* ゼロクロス直後はAC瞬時電圧がほぼ0VでTRIACの保持電流を確保できない。
 * rbdimmerの既知問題: 50µs遅延でAC~5V → ラッチ閾値未満で点弧失敗。
 * P100実測で0Vになったため150µsを下限とする(必要なら200/300µsに引き上げ)。 */
#define TRIAC_MIN_FIRE_DELAY_US 150U

/* ── Z-Cデバウンス ───────────────────────────────────────── */
#define TRIAC_ZC_DEBOUNCE_MS    3U               /* 3ms: TRIACノイズ対策 (rbdimmer準拠) */

/* ── センサー/ファンピン ─────────────────────────────────── */
#define TRIAC_FAN_PORT          GPIOB
#define TRIAC_FAN_PIN_Msk       GPIO_PIN_15      /* PB15 = D11 */

#define TRIAC_ADC_TEMP_CH       ADC_CHANNEL_5   /* PA0 = A0 : 温度センサー */
#define TRIAC_ADC_CUR_CH        ADC_CHANNEL_6   /* PA1 = A1 : 電流センサー(未配線) */

/* ── 出力レベル ──────────────────────────────────────────── */
#define TRIAC_LEVEL_MIN         0U
#define TRIAC_LEVEL_MAX         100U
#define TRIAC_LEVEL_SAFE_DFLT   10U   /* M811 Pワード省略時の安全既定値 */

/* ── 温度ADC閾値 ─────────────────────────────────────────── */
/* ADC 12bit (0-4095), Vref=3.3V → 実機TEMPピン電圧で校正すること */
#define TRIAC_TEMP_FAN_ON_ADC   1240U
#define TRIAC_TEMP_FAN_OFF_ADC  1000U
#define TRIAC_TEMP_OVERHEAT_ADC 2480U

/* ── ステータス ──────────────────────────────────────────── */
typedef struct {
    uint16_t temp_adc;
    uint16_t cur_adc;
    uint16_t delay_us;    /* 現在の位相遅延 (M816診断用) */
    bool     fan_on;
    bool     overheat;
    bool     init_error;  /* ADC初期化失敗 */
} triac_status_t;

/* ── 公開API ─────────────────────────────────────────────── */

/** 初期化 — driver_setup()完了後に呼ぶ */
void triac_init(void);

/** 出力レベル設定 (0-100)。範囲外はクランプ。次のゼロクロスから反映。 */
void triac_set_level(uint8_t level);

/** 現在の出力レベル取得 (0-100) */
uint8_t triac_get_level(void);

/** TRIAC出力停止 */
void triac_disable(void);

/** TRIAC出力再開 */
void triac_enable(void);

/**
 * センサー読み取り + ファン自動制御
 * 定期呼び出し推奨 (1秒ごと)
 * 過熱検出時はtriac_disable()を自動呼び出し
 */
triac_status_t triac_update_sensors(void);

/** 現在のステータス取得 (ADC読み取りなし) */
triac_status_t triac_get_status(void);

/** ADC診断: TEMPチャンネルを1回読み、HAL戻り値とADC1レジスタをreport_messageで出力 (M817用) */
void triac_adc_diag(void);

#endif /* TRIAC_CONTROL_H */
