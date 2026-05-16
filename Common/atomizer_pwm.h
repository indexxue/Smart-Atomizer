/**
 * @file atomizer_pwm.h
 * @brief PB4 (TIM3_CH1) 100 kHz PWM，四挡占空比 70/50/30/0%，按键加减挡不循环。
 */

#ifndef ATOMIZER_PWM_H
#define ATOMIZER_PWM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#define ATOMIZER_PWM_GEAR_COUNT  4u

void atomizer_pwm_init(void);

/** PC8 增加：0%→30%→50%→70%，已在最高挡时返回 false。 */
bool atomizer_pwm_gear_increase(void);

/** PC7 减少：70%→50%→30%→0%，已在最低挡时返回 false。 */
bool atomizer_pwm_gear_decrease(void);

/** 当前挡位索引 0=0% … 3=70%。 */
uint8_t atomizer_pwm_gear_index(void);

/** 当前幅度百分比 0/30/50/70。 */
uint8_t atomizer_pwm_amp_percent(void);

/** 保护锁：强制停 PWM（占空比 0），保留挡位；解除后按当前挡位恢复。 */
void atomizer_pwm_protect_set(bool protect);

/** 是否处于保护锁（输出被强制为 0）。 */
bool atomizer_pwm_protect_active(void);

#ifdef __cplusplus
}
#endif

#endif /* ATOMIZER_PWM_H */
