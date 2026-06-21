/**
 * triac_mcodes.c
 * grblHAL Mコード統合 — TRIAC出力レベル制御 (DimmerLink)
 *
 * M810           OFF (level=0)
 * M811 P<0-100>  出力レベル設定 (P省略時は安全既定値 level=10)
 * M814 ENABLE / M815 DISABLE / M816 STATUS
 */

#include "triac_mcodes.h"
#include "triac_control.h"
#include "driver.h"
#include "grbl/grbl.h"
#include "grbl/core_handlers.h"
#include "grbl/system.h"
#include <stdio.h>
#include <string.h>

/* ── Mコード番号 ─────────────────────────────────────────── */
#define MCODE_TRIAC_OFF     810
#define MCODE_TRIAC_LEVEL   811   /* M811 P<0-100> : 出力レベル設定 (P省略時は安全既定値) */
#define MCODE_TRIAC_ENABLE  814
#define MCODE_TRIAC_DISABLE 815
#define MCODE_TRIAC_STATUS  816
#define MCODE_TRIAC_ADCDIAG 817   /* ADC診断: TEMPチャンネル単発読み+レジスタダンプ (一時的) */

/* ── センサー定期更新 ────────────────────────────────────── */
static uint32_t s_last_sensor_tick = 0;
static bool s_prev_overheat = false;
#define SENSOR_UPDATE_MS 1000U

static on_execute_realtime_ptr s_prev_realtime = NULL;

static void triac_realtime_handler(sys_state_t state)
{
    if (s_prev_realtime) s_prev_realtime(state);

    uint32_t now = HAL_GetTick();
    if (now - s_last_sensor_tick >= SENSOR_UPDATE_MS) {
        s_last_sensor_tick = now;
        triac_status_t st = triac_update_sensors();
        if (st.overheat && !s_prev_overheat)
            report_message("TRIAC OVERHEAT - output disabled", Message_Warning);
        else if (!st.overheat && s_prev_overheat)
            report_message("TRIAC temperature recovered", Message_Info);
        s_prev_overheat = st.overheat;
    }
}

/* ── user_mcode コールバック ─────────────────────────────── */
static user_mcode_type_t triac_mcode_check(user_mcode_t mcode)
{
    switch ((uint16_t)mcode) {
        case MCODE_TRIAC_OFF:
        case MCODE_TRIAC_LEVEL:
        case MCODE_TRIAC_ENABLE:
        case MCODE_TRIAC_DISABLE:
        case MCODE_TRIAC_STATUS:
        case MCODE_TRIAC_ADCDIAG:
            return UserMCode_Normal;
        default:
            return UserMCode_Unsupported;
    }
}

static status_code_t triac_mcode_validate(parser_block_t *gc_block)
{
    switch ((uint16_t)gc_block->user_mcode) {
        case MCODE_TRIAC_LEVEL:
            /* M811 P<0-100>: Pワードは任意。
             * 指定時は範囲検証してwordを消費。省略時はvalues.pに安全既定値を入れて
             * 正規化する(非syncなのでvalidate→executeは同一gc_blockで連続実行)。 */
            if (gc_block->words.p) {
                if (gc_block->values.p < 0.0f || gc_block->values.p > (float)TRIAC_LEVEL_MAX)
                    return Status_GcodeValueOutOfRange;
                gc_block->words.p = Off; /* 消費 */
            } else {
                gc_block->values.p = (float)TRIAC_LEVEL_SAFE_DFLT;
            }
            return Status_OK;
        case MCODE_TRIAC_OFF:
        case MCODE_TRIAC_ENABLE:
        case MCODE_TRIAC_DISABLE:
        case MCODE_TRIAC_STATUS:
        case MCODE_TRIAC_ADCDIAG:
            return Status_OK;
        default:
            return Status_Unhandled;
    }
}

static void triac_mcode_execute(sys_state_t state, parser_block_t *gc_block)
{
    (void)state;
    char buf[80];

    switch ((uint16_t)gc_block->user_mcode) {
        case MCODE_TRIAC_OFF:
            triac_set_level(0);
            report_message("TRIAC OFF", Message_Info);
            break;
        case MCODE_TRIAC_LEVEL: {
            /* validateで正規化済み: values.p は 0-100 (P省略時は安全既定値) */
            uint8_t level = (uint8_t)gc_block->values.p;
            triac_set_level(level);
            snprintf(buf, sizeof(buf), "TRIAC LEVEL=%u", (unsigned)level);
            report_message(buf, Message_Info);
            break;
        }
        case MCODE_TRIAC_ENABLE:
            triac_enable();
            report_message("TRIAC ENABLED", Message_Info);
            break;
        case MCODE_TRIAC_DISABLE:
            triac_disable();
            report_message("TRIAC DISABLED", Message_Info);
            break;
        case MCODE_TRIAC_STATUS: {
            triac_status_t st = triac_get_status();
            snprintf(buf, sizeof(buf),
                     "TRIAC temp=%u cur=%u fan=%s overheat=%s delay_us=%u init_err=%s",
                     st.temp_adc, st.cur_adc,
                     st.fan_on     ? "ON"  : "OFF",
                     st.overheat   ? "YES" : "NO",
                     (unsigned)st.delay_us,
                     st.init_error ? "YES" : "NO");
            report_message(buf, Message_Info);
            break;
        }
        case MCODE_TRIAC_ADCDIAG:
            triac_adc_diag();
            break;
        default:
            break;
    }
}

/* ── リセットフック ──────────────────────────────────────── */
static on_reset_ptr s_prev_reset = NULL;

static void triac_on_reset(void)
{
    if (s_prev_reset) s_prev_reset();
    triac_disable();
}

/* ── 登録 ────────────────────────────────────────────────── */
static user_mcode_ptrs_t s_prev_mcode = {0}; /* 前段 user_mcode チェーン */

static user_mcode_type_t triac_mcode_check_chain(user_mcode_t mcode)
{
    user_mcode_type_t r = triac_mcode_check(mcode);
    if (r != UserMCode_Unsupported) return r;
    return s_prev_mcode.check ? s_prev_mcode.check(mcode) : UserMCode_Unsupported;
}

static status_code_t triac_mcode_validate_chain(parser_block_t *gc_block)
{
    if (triac_mcode_check((user_mcode_t)gc_block->user_mcode) != UserMCode_Unsupported)
        return triac_mcode_validate(gc_block);
    return s_prev_mcode.validate ? s_prev_mcode.validate(gc_block) : Status_Unhandled;
}

static void triac_mcode_execute_chain(sys_state_t state, parser_block_t *gc_block)
{
    if (triac_mcode_check((user_mcode_t)gc_block->user_mcode) != UserMCode_Unsupported)
        triac_mcode_execute(state, gc_block);
    else if (s_prev_mcode.execute)
        s_prev_mcode.execute(state, gc_block);
}

static const user_mcode_ptrs_t triac_mcodes_chained = {
    .check    = triac_mcode_check_chain,
    .validate = triac_mcode_validate_chain,
    .execute  = triac_mcode_execute_chain,
};

void triac_mcodes_register(void)
{
    triac_init();

    s_prev_mcode      = grbl.user_mcode; /* 前段を保存してからチェーン登録 */
    grbl.user_mcode   = triac_mcodes_chained;

    s_prev_reset      = grbl.on_reset;
    grbl.on_reset     = triac_on_reset;

    s_prev_realtime          = grbl.on_execute_realtime;
    grbl.on_execute_realtime = triac_realtime_handler;

    s_last_sensor_tick = HAL_GetTick();
}
