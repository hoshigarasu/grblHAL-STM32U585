/**
 * triac_mcodes.c
 * grblHAL Mコード統合 — TRIACフェーズ角電圧制御
 *
 * M810 OFF / M811 20V / M812 30V / M813 40V
 * M814 ENABLE / M815 DISABLE / M816 STATUS
 */

#include "triac_mcodes.h"
#include "triac_control.h"
#include "grbl/grbl.h"
#include "grbl/core_handlers.h"
#include "grbl/system.h"
#include <stdio.h>
#include <string.h>

/* ── Mコード番号 ─────────────────────────────────────────── */
#define MCODE_TRIAC_OFF     810
#define MCODE_TRIAC_20V     811
#define MCODE_TRIAC_30V     812
#define MCODE_TRIAC_40V     813
#define MCODE_TRIAC_ENABLE  814
#define MCODE_TRIAC_DISABLE 815
#define MCODE_TRIAC_STATUS  816

/* ── センサー定期更新 ────────────────────────────────────── */
static uint32_t s_last_sensor_tick = 0;
#define SENSOR_UPDATE_MS 1000U

static on_execute_realtime_ptr s_prev_realtime = NULL;

static void triac_realtime_handler(sys_state_t state)
{
    if (s_prev_realtime) s_prev_realtime(state);

    uint32_t now = HAL_GetTick();
    if (now - s_last_sensor_tick >= SENSOR_UPDATE_MS) {
        s_last_sensor_tick = now;
        triac_status_t st = triac_update_sensors();
        if (st.overheat) {
            report_message("TRIAC OVERHEAT - output disabled", Message_Warning);
        }
    }
}

/* ── user_mcode コールバック ─────────────────────────────── */
static user_mcode_type_t triac_mcode_check(user_mcode_t mcode)
{
    switch ((uint16_t)mcode) {
        case MCODE_TRIAC_OFF:
        case MCODE_TRIAC_20V:
        case MCODE_TRIAC_30V:
        case MCODE_TRIAC_40V:
        case MCODE_TRIAC_ENABLE:
        case MCODE_TRIAC_DISABLE:
        case MCODE_TRIAC_STATUS:
            return UserMCode_Normal;
        default:
            return UserMCode_Unsupported;
    }
}

static status_code_t triac_mcode_validate(parser_block_t *gc_block)
{
    switch ((uint16_t)gc_block->user_mcode) {
        case MCODE_TRIAC_OFF:
        case MCODE_TRIAC_20V:
        case MCODE_TRIAC_30V:
        case MCODE_TRIAC_40V:
        case MCODE_TRIAC_ENABLE:
        case MCODE_TRIAC_DISABLE:
        case MCODE_TRIAC_STATUS:
            return Status_OK;
        default:
            return Status_Unhandled;
    }
}

static void triac_mcode_execute(sys_state_t state, parser_block_t *gc_block)
{
    (void)state;
    char buf[176];

    switch ((uint16_t)gc_block->user_mcode) {
        case MCODE_TRIAC_OFF:
            triac_set_voltage(TRIAC_VOLTAGE_OFF);
            report_message("TRIAC OFF", Message_Info);
            break;
        case MCODE_TRIAC_20V:
            triac_set_voltage(TRIAC_VOLTAGE_20V);
            report_message("TRIAC 20V", Message_Info);
            break;
        case MCODE_TRIAC_30V:
            triac_set_voltage(TRIAC_VOLTAGE_30V);
            report_message("TRIAC 30V", Message_Info);
            break;
        case MCODE_TRIAC_40V:
            triac_set_voltage(TRIAC_VOLTAGE_40V);
            report_message("TRIAC 40V", Message_Info);
            break;
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
                     "TRIAC temp=%u cur=%u fan=%s overheat=%s i2c_err=%s init_err=%s "
                     "i2c_init_err=%s adc_init_err=%s i2c_err_code=0x%02lX adc_err_code=0x%02lX",
                     st.temp_adc, st.cur_adc,
                     st.fan_on         ? "ON"  : "OFF",
                     st.overheat       ? "YES" : "NO",
                     st.i2c_error      ? "YES" : "NO",
                     st.init_error     ? "YES" : "NO",
                     st.i2c_init_error ? "YES" : "NO",
                     st.adc_init_error ? "YES" : "NO",
                     (unsigned long)st.i2c_error_code,
                     (unsigned long)st.adc_error_code);
            report_message(buf, Message_Info);
            snprintf(buf, sizeof(buf),
                     "TRIAC_ADC step=%u cr=0x%08lX postdis=0x%08lX postcal=0x%08lX isr=0x%08lX ccr=0x%08lX",
                     (unsigned)st.adc_fail_step,
                     (unsigned long)st.adc_cr,
                     (unsigned long)st.adc_cr_postdis,
                     (unsigned long)st.adc_cr_postcal,
                     (unsigned long)st.adc_isr,
                     (unsigned long)st.adc_ccr);
            report_message(buf, Message_Info);
            break;
        }
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
