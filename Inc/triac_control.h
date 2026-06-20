/**
 * triac_control.h
 * STM32U585 TRIAC制御 — DimmerLink (I2C) + センサー/ファン
 *
 * ハードウェア構成:
 *   I2C SDA  : PB11 (D20) — I2C2_SDA, AF4, DimmerLink制御
 *   I2C SCL  : PB10 (D21) — I2C2_SCL, AF4
 *   TEMP     : PA0  (A0)  — ADC1_CH5, 温度センサーアナログ出力
 *   CUR-S    : PA1  (A1)  — ADC1_CH6, 電流センサーアナログ出力
 *   FAN      : PB15 (D11) — GPIO出力, ファンON/OFF
 *
 * DimmerLink I2C仕様:
 *   アドレス : 0x50
 *   速度     : 100kHz (Standard Mode)
 *   コマンド : SET_LEVEL [ch] [level 0-100] → 3バイト
 *   プルアップ: STM32U585内部プルアップ使用（短距離）
 *              長距離・ノイズ環境では外付け4.7kΩ推奨
 *
 * 電圧ステップ:
 *   20V → level ≈ 20 (DimmerLink内部でRMSカーブ変換)
 *   30V → level ≈ 30
 *   40V → level ≈ 40
 *   ※実機で電圧計を当てて level 値を校正すること
 *
 * grblHAL Mコード (triac_mcodes.c):
 *   M810 OFF  M811 20V  M812 30V  M813 40V
 *   M814 ENABLE  M815 DISABLE  M816 STATUS
 */

#ifndef TRIAC_CONTROL_H
#define TRIAC_CONTROL_H

#include <stdint.h>
#include <stdbool.h>
#include "stm32u5xx_hal.h"

/* ── I2C定義 ─────────────────────────────────────────────── */
#define TRIAC_I2C               I2C2
#define TRIAC_I2C_CLK_EN()      __HAL_RCC_I2C2_CLK_ENABLE()
#define TRIAC_I2C_ADDR          (0x50 << 1)   /* HAL は7bit左シフト */
#define TRIAC_I2C_TIMEOUT_MS    10U

/* I2C GPIO */
#define TRIAC_I2C_PORT          GPIOB
#define TRIAC_I2C_SDA_PIN       GPIO_PIN_11   /* PB11 = D20 */
#define TRIAC_I2C_SCL_PIN       GPIO_PIN_10   /* PB10 = D21 */
#define TRIAC_I2C_AF            GPIO_AF4_I2C2

/* ── センサー/ファンピン定義 ─────────────────────────────── */
#define TRIAC_FAN_PORT          GPIOB
#define TRIAC_FAN_PIN_Msk       GPIO_PIN_15   /* PB15 = D11 */

#define TRIAC_ADC_TEMP_CH       ADC_CHANNEL_5 /* PA0 = A0 : 温度センサー */
#define TRIAC_ADC_CUR_CH        ADC_CHANNEL_6 /* PA1 = A1 : 電流センサー */

/* ── DimmerLevelコマンド ─────────────────────────────────── */
/* UART/I2Cプロトコル: [CMD_SET_LEVEL=0x01] [channel=0x01] [level 0-100] */
#define DIMMERLINK_CMD_SET_LEVEL  0x01U
#define DIMMERLINK_CHANNEL        0x01U       /* 1ch構成 */

/* ── DimmerLink出力レベル (0-100) ─────────────────────────── */
/* levelはDimmerLinkの位相角/RMSカーブのパーセンテージで、実電圧(V)とは
 * 非線形対応。実電圧指定は将来 level↔V 変換テーブルを triac_set_level の
 * 呼び出し側に持たせて実現する(本ファイルはlevelを素通し)。 */
#define TRIAC_LEVEL_MIN         0U
#define TRIAC_LEVEL_MAX         100U
#define TRIAC_LEVEL_SAFE_DFLT   10U   /* M811をPワード無しで打った時の安全既定値 */

/* ── 温度・電流ADC閾値 ───────────────────────────────────── */
/* ADC 12bit (0-4095), Vref=3.3V → V = val*3.3/4095          */
/* 実機でモジュールのTEMPピン電圧を実測して調整すること       */
#define TRIAC_TEMP_FAN_ON_ADC   1240U   /* ファンON閾値  */
#define TRIAC_TEMP_FAN_OFF_ADC  1000U   /* ファンOFF閾値 (ヒステリシス) */
#define TRIAC_TEMP_OVERHEAT_ADC 2480U   /* 過熱保護閾値  */

/* ── ステータス ──────────────────────────────────────────── */
typedef struct {
    uint16_t temp_adc;
    uint16_t cur_adc;
    bool     fan_on;
    bool     overheat;
    bool     i2c_error;     /* DimmerLink通信エラー */
    bool     init_error;    /* 初期化失敗 (HAL_I2C_Init / HAL_ADC_Init) */
} triac_status_t;

/* ── 公開API ─────────────────────────────────────────────── */

/** 初期化 — driver_setup()完了後に呼ぶ */
void triac_init(void);

/** 出力レベル設定 (0-100)。範囲外はクランプ。
 *  enableされている時のみ実際にDimmerLinkへ送信する。 */
void triac_set_level(uint8_t level);

/** 現在の出力レベル取得 (0-100) */
uint8_t triac_get_level(void);

/** TRIAC出力停止 (level=0送信 + I2C無効化) */
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

#endif /* TRIAC_CONTROL_H */
