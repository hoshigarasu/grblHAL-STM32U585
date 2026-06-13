/**
 * triac_mcodes.h
 */
#ifndef TRIAC_MCODES_H
#define TRIAC_MCODES_H

/**
 * @brief grblHAL Mコード登録 + TRIACハードウェア初期化
 *        driver.c の driver_setup() 末尾から呼ぶこと
 */
void triac_mcodes_register(void);

#endif /* TRIAC_MCODES_H */
