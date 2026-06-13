/**
 * uno_q_cnc_map.h
 * Arduino UNO Q + CNCシールドV3 ボードマップ
 */
#ifndef BOARD_UNO_Q_CNC_MAP_H
#define BOARD_UNO_Q_CNC_MAP_H

#define BOARD_NAME "Arduino UNO Q + CNC Shield V3"
#define STM32_PLATFORM

/* USB CDC 無効 */
#undef  USB_SERIAL_CDC
#define USB_SERIAL_CDC  0

/* --- X軸 (D2=STEP, D5=DIR) --- */
#define X_STEP_PORT         GPIOB
#define X_STEP_PIN          3        /* PB3 = D2 (確定) */
#define X_STEP_BIT          (1u << X_STEP_PIN)

#define X_DIRECTION_PORT    GPIOA
#define X_DIRECTION_PIN     11       /* PA11 = D5 (確定) */
#define X_DIRECTION_BIT     (1u << X_DIRECTION_PIN)

/* --- Y軸 (D3=STEP, D6=DIR) --- */
#define Y_STEP_PORT         GPIOB
#define Y_STEP_PIN          0        /* PB0 = D3 (確定) */
#define Y_STEP_BIT          (1u << Y_STEP_PIN)

#define Y_DIRECTION_PORT    GPIOB
#define Y_DIRECTION_PIN     1        /* PB1 = D6 (確定) */
#define Y_DIRECTION_BIT     (1u << Y_DIRECTION_PIN)

/* --- Z軸 (D4=STEP, D7=DIR) --- */
#define Z_STEP_PORT         GPIOA
#define Z_STEP_PIN          12       /* PA12 = D4 (確定) */
#define Z_STEP_BIT          (1u << Z_STEP_PIN)

#define Z_DIRECTION_PORT    GPIOB
#define Z_DIRECTION_PIN     2        /* PB2 = D7 (確定) */
#define Z_DIRECTION_BIT     (1u << Z_DIRECTION_PIN)

/* --- Enable (D8, ActiveLow) --- */
#define STEPPERS_ENABLE_PORT    GPIOB
#define STEPPERS_ENABLE_PIN     4        /* PB4 = D8 (確定) */
#define STEPPERS_ENABLE_BIT     (1u << STEPPERS_ENABLE_PIN)

/* --- 出力モード変更 --- */
#define STEP_OUTMODE      GPIO_BITBAND  /* STEP: Z軸がGPIOAのため個別DIGITAL_OUT呼び出し */
#define DIRECTION_OUTMODE GPIO_BITBAND  /* DIR:  異ポートのためDIGITAL_OUT個別呼び出し */
                                        /* ↑ DIGITAL_OUTはbitband_compat.hのBSSR版が使われる */

/* --- STEPマスク (GPIOB) --- */
/* STEP_MASK: X(PB3)+Y(PB0) on GPIOB, Z(PA12) on GPIOA → split port, used for GPIO init only */
#define STEP_MASK     (X_STEP_BIT | Y_STEP_BIT | Z_STEP_BIT)

/* --- タイマー --- */
#define STEPPER_TIMER_N     2
#define STEPPER_TIMER       TIM2
#define STEPPER_TIMER_IRQ   TIM2_IRQn

#define PULSE_TIMER_N       3
#define PULSE_TIMER         TIM3
#define PULSE_TIMER_IRQ     TIM3_IRQn

/* STEPパルス幅補正値 (160MHz用、要校正) */
#define STEP_PULSE_TOFF     16

#define N_AXIS  3

#endif /* BOARD_UNO_Q_CNC_MAP_H */

/* --- プローブ無効 --- */
#define PROBE_ENABLED       0
#define PROBE_ENABLE        0
#define PROBE_PORT          GPIOC
#define PROBE_PIN           3
#define PROBE_BIT           (1u << PROBE_PIN)

/* --- リミットスイッチ（初期は未接続、ダミー定義） --- */
#define X_LIMIT_PORT        GPIOC
#define X_LIMIT_PIN         6
#define X_LIMIT_BIT         (1u << X_LIMIT_PIN)
#define Y_LIMIT_PORT        GPIOC
#define Y_LIMIT_PIN         7
#define Y_LIMIT_BIT         (1u << Y_LIMIT_PIN)
#define Z_LIMIT_PORT        GPIOC
#define Z_LIMIT_PIN         8
#define Z_LIMIT_BIT         (1u << Z_LIMIT_PIN)
#define LIMIT_PORT          GPIOC
#define LIMIT_MASK          (X_LIMIT_BIT|Y_LIMIT_BIT|Z_LIMIT_BIT)
#define LIMIT_INMODE        0

/* --- シリアルストリーム --- */
#define SERIAL_STREAM       0
