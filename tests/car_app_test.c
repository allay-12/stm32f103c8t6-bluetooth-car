#include "car_app.h"
#include "car_config.h"
#include "main.h"
#include "tim.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

GPIO_TypeDef fake_gpioa, fake_gpiob;
TIM_TypeDef fake_tim2, fake_tim3;
TIM_HandleTypeDef htim2, htim3;
static uint32_t now_ms, irq_mask, trigger_count, counter_reads;
static uint32_t simulated_trigger, simulated_pulse_start;

uint32_t __get_PRIMASK(void) { return irq_mask; }
void __disable_irq(void) { irq_mask = 1U; }
void __set_PRIMASK(uint32_t mask) { irq_mask = mask; }
uint32_t HAL_GetTick(void) { return now_ms; }
uint32_t HAL_RCC_GetPCLK1Freq(void) { return 32000000U; }
uint32_t HAL_RCC_GetHCLKFreq(void) { return 64000000U; }
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *gpio, uint16_t pin)
{
    return (gpio->IDR & pin) != 0U ? GPIO_PIN_SET : GPIO_PIN_RESET;
}
void HAL_GPIO_WritePin(GPIO_TypeDef *gpio, uint16_t pin, GPIO_PinState value)
{
    if (value == GPIO_PIN_SET) {
        gpio->ODR |= pin;
        if (gpio == GPIOA && pin == US_TRIG_Pin) ++trigger_count;
    } else gpio->ODR &= ~pin;
}
uint32_t HAL_TIM_ReadCapturedValue(TIM_HandleTypeDef *timer, uint32_t channel)
{
    (void)channel;
    return timer->Instance->CCR1;
}
HAL_StatusTypeDef HAL_TIM_IC_Start_IT(TIM_HandleTypeDef *timer, uint32_t channel)
{
    (void)channel;
    timer->Instance->DIER |= TIM_IT_CC1;
    return HAL_OK;
}
HAL_StatusTypeDef HAL_TIM_PWM_Start(TIM_HandleTypeDef *timer, uint32_t channel)
{
    (void)timer;
    (void)channel;
    return HAL_OK;
}
uint32_t Fake_GetCounter(TIM_HandleTypeDef *timer)
{
    ++counter_reads;
    return timer->Instance->CNT++ & 65535U;
}
void Fake_SetCompare(TIM_HandleTypeDef *timer, uint32_t channel, uint32_t value)
{
    if (channel == TIM_CHANNEL_1) timer->Instance->CCR1 = value;
    else timer->Instance->CCR2 = value;
}
void Error_Handler(void) { assert(!"Unexpected HAL initialization error"); }

static void reset(void)
{
    memset(&fake_gpioa, 0, sizeof(fake_gpioa));
    memset(&fake_gpiob, 0, sizeof(fake_gpiob));
    memset(&fake_tim2, 0, sizeof(fake_tim2));
    memset(&fake_tim3, 0, sizeof(fake_tim3));
    memset(&htim2, 0, sizeof(htim2));
    memset(&htim3, 0, sizeof(htim3));
    memset((void *)&g_car_debug, 0, sizeof(g_car_debug));
    now_ms = irq_mask = trigger_count = counter_reads = 0U;
    simulated_trigger = simulated_pulse_start = 0U;
    htim2.Instance = TIM2;
    htim2.Init.Prescaler = 63U;
    htim2.Init.Period = 65535U;
    htim3.Instance = TIM3;
    htim3.Init.Period = 3199U;
    GPIOB->IDR = IR_LEFT_Pin | IR_RIGHT_Pin | IR_LEFT_OUTER_Pin | IR_RIGHT_OUTER_Pin;
    CarApp_Init();
}

static void capture(uint16_t value, uint8_t high)
{
    if (high) GPIOA->IDR |= US_ECHO_Pin;
    else GPIOA->IDR &= ~US_ECHO_Pin;
    if (!(TIM2->DIER & TIM_IT_CC1)) return;
    assert(irq_mask == 0U);
    htim2.Channel = HAL_TIM_ACTIVE_CHANNEL_1;
    TIM2->CCR1 = value;
    HAL_TIM_IC_CaptureCallback(&htim2);
}

static void test_wrapped_echo(void)
{
    reset();
    CarApp_Poll();
    assert(trigger_count == 1U && counter_reads >= 12U && counter_reads < 30U);
    now_ms = 1U;
    capture(65000U, 1U);
    assert(TIM2->polarity == TIM_INPUTCHANNELPOLARITY_FALLING);
    now_ms = 2U;
    capture(464U, 0U); /* 65000 + 1000, modulo 65536. */
    CarApp_Poll();
    assert(g_car_debug.echo_us == 1000U);
    assert(g_car_debug.distance_mm == 172U);
    assert(g_car_debug.range_status == CAR_RANGE_VALID);
    assert(!(TIM2->DIER & TIM_IT_CC1));
    assert(irq_mask == 0U);
}

static uint8_t test_recorded_white_paper_levels(void)
{
    unsigned i;
    reset();
    /* Hardware observation: both DO pins read 0x00 over white paper. */
    GPIOB->IDR &= ~(IR_LEFT_Pin | IR_RIGHT_Pin);
    for (i = 0U; i < CAR_LINE_SAMPLE_MS * (CAR_LINE_STABLE_SAMPLES + 1U); ++i) {
        ++now_ms;
        CarApp_Poll();
    }
    if (g_car_debug.left_black || g_car_debug.right_black) {
        fputs("FAIL: recorded white-paper GPIO levels were classified as black\n", stderr);
        return 0U;
    }
    return 1U;
}

static void run_app4(uint32_t duration, uint8_t mask)
{
    uint32_t i;
    GPIOB->IDR = ((mask & 8U) ? IR_LEFT_OUTER_Pin : 0U) |
                 ((mask & 4U) ? IR_LEFT_Pin : 0U) |
                 ((mask & 2U) ? IR_RIGHT_Pin : 0U) |
                 ((mask & 1U) ? IR_RIGHT_OUTER_Pin : 0U);
    for (i = 0U; i < duration; ++i) {
        ++now_ms;
        /* Preserve an in-flight echo across successive stimulus segments. */
        if (simulated_trigger && now_ms - simulated_pulse_start == 1U) capture(65000U, 1U);
        if (simulated_trigger && now_ms - simulated_pulse_start == 7U) capture(5464U, 0U);
        CarApp_Poll();
        if (trigger_count != simulated_trigger) {
            simulated_trigger = trigger_count;
            simulated_pulse_start = now_ms;
        }
    }
}

static void run_app(uint32_t duration, uint8_t left, uint8_t right)
{
    run_app4(duration, (uint8_t)((left << 2) | (right << 1)));
}

static void assert_direction_pins(void)
{
    uint32_t expected = 0U;
    uint32_t all = MOTOR_AIN1_Pin | MOTOR_AIN2_Pin | MOTOR_BIN1_Pin | MOTOR_BIN2_Pin;
    if (g_car_debug.duty_left == 0 && g_car_debug.duty_right != 0)
        expected |= MOTOR_AIN1_Pin | MOTOR_AIN2_Pin;
    if (g_car_debug.duty_right == 0 && g_car_debug.duty_left != 0)
        expected |= MOTOR_BIN1_Pin | MOTOR_BIN2_Pin;
    if (g_car_debug.duty_left > 0) expected |= CAR_LEFT_FORWARD_IN1 ? MOTOR_AIN1_Pin : MOTOR_AIN2_Pin;
    if (g_car_debug.duty_left < 0) expected |= CAR_LEFT_FORWARD_IN1 ? MOTOR_AIN2_Pin : MOTOR_AIN1_Pin;
    if (g_car_debug.duty_right > 0) expected |= CAR_RIGHT_FORWARD_IN1 ? MOTOR_BIN1_Pin : MOTOR_BIN2_Pin;
    if (g_car_debug.duty_right < 0) expected |= CAR_RIGHT_FORWARD_IN1 ? MOTOR_BIN2_Pin : MOTOR_BIN1_Pin;
    assert((GPIOB->BSRR & all) == expected);
    assert(((GPIOB->BSRR >> 16) & all) == (all ^ expected));
}

static void test_confirmed_mounting_and_motor_channels(void)
{
    reset();
    run_app4(CAR_STARTUP_MS + 600U, 6U);
    assert(g_car_debug.state == CAR_STARTUP && TIM3->CCR1 == 0U && TIM3->CCR2 == 0U);
    run_app4(600U, 15U);
    assert(g_car_debug.state == CAR_FOLLOW && g_car_debug.marker_count == 1U);
    assert(g_car_debug.raw_left_outer_level && g_car_debug.raw_right_outer_level);
    run_app4(600U, 6U);
    assert(g_car_debug.duty_left == CAR_CRUISE_DUTY && TIM3->CCR1 == TIM3->CCR2);
    assert_direction_pins();
    /* Corrected rule: inner-left detects black => physical LEFT turn. */
    run_app4(400U, 4U);
    assert(TIM3->CCR1 == (uint32_t)CAR_TURN_SLOW_DUTY * 3200U / 1000U && TIM3->CCR2 == (uint32_t)CAR_TURN_FAST_DUTY * 3200U / 1000U);
    assert(g_car_debug.duty_left == CAR_TURN_SLOW_DUTY && g_car_debug.duty_right == CAR_TURN_FAST_DUTY);
    assert_direction_pins();
    assert(g_car_debug.target_right == CAR_TURN_FAST_DUTY);
    run_app4(400U, 12U);
    assert(g_car_debug.target_left == CAR_SHARP_SLOW_DUTY);
    assert(g_car_debug.target_right == CAR_SHARP_FAST_DUTY);
    assert(g_car_debug.duty_left == CAR_SHARP_SLOW_DUTY && g_car_debug.duty_right == CAR_SHARP_FAST_DUTY);
    assert(TIM3->CCR1 == (uint32_t)CAR_SHARP_SLOW_DUTY * 3200U / 1000U && TIM3->CCR2 == (uint32_t)CAR_SHARP_FAST_DUTY * 3200U / 1000U);
    assert_direction_pins();
    run_app4(400U, 3U);
    assert(g_car_debug.duty_left == CAR_SHARP_FAST_DUTY && g_car_debug.duty_right == CAR_SHARP_SLOW_DUTY);
    assert(TIM3->CCR1 == (uint32_t)CAR_SHARP_FAST_DUTY * 3200U / 1000U && TIM3->CCR2 == (uint32_t)CAR_SHARP_SLOW_DUTY * 3200U / 1000U);
    assert_direction_pins();
    run_app4(800U, 0U);
    assert(g_car_debug.duty_left == CAR_CRUISE_DUTY && g_car_debug.duty_right == CAR_CRUISE_DUTY);
    run_app4(100U, 15U);
    assert(g_car_debug.state == CAR_FINISHED && g_car_debug.marker_count == 2U);
    assert(TIM3->CCR1 == 0U && TIM3->CCR2 == 0U);
    assert(GPIOB->BRR == MOTOR_STBY_Pin);
    run_app4(600U, 0U);
    assert(g_car_debug.state == CAR_FINISHED && TIM3->CCR1 == 0U);
}

static void test_finite_motor_bench_mode(void)
{
    uint32_t i;
    reset();
    /* Deliberately no valid echo. The bench test must isolate motor wiring. */
    for (i = 0U; i < CAR_STARTUP_MS + 600U; ++i) {
        ++now_ms;
        CarApp_Poll();
        if (now_ms < CAR_STARTUP_MS) assert(TIM3->CCR1 == 0U && TIM3->CCR2 == 0U);
    }
    assert(g_car_debug.state == CAR_MOTOR_TEST);
    assert(TIM3->CCR1 == ((CAR_MOTOR_TEST_MODE == 1U || CAR_MOTOR_TEST_MODE == 3U) ?
                        CAR_MOTOR_TEST_DUTY * 3200U / 1000U : 0U));
    assert(TIM3->CCR2 == ((CAR_MOTOR_TEST_MODE == 2U || CAR_MOTOR_TEST_MODE == 3U) ?
                        CAR_MOTOR_TEST_DUTY * 3200U / 1000U : 0U));
    assert_direction_pins();
    for (i = 0U; i < CAR_MOTOR_TEST_RUN_MS + 1000U; ++i) {
        ++now_ms;
        CarApp_Poll();
        if (now_ms >= CAR_STARTUP_MS + CAR_MOTOR_TEST_RUN_MS) {
            assert(g_car_debug.state == CAR_MOTOR_TEST_DONE);
            assert(TIM3->CCR1 == 0U && TIM3->CCR2 == 0U);
            assert(GPIOB->BRR == MOTOR_STBY_Pin);
        }
    }
    reset();
    run_app(CAR_STARTUP_MS + 600U, 1U, 1U);
    now_ms += CAR_CONTROL_MAX_GAP_MS + 1U;
    CarApp_Poll();
    assert(g_car_debug.state == CAR_CONTROL_FAULT);
    assert(TIM3->CCR1 == 0U && TIM3->CCR2 == 0U);
}

static void test_timeouts_noise_and_rearm(void)
{
    reset();
    CarApp_Poll();
    now_ms = CAR_RANGE_TIMEOUT_MS;
    CarApp_Poll();
    assert(g_car_debug.range_status == CAR_RANGE_NO_ECHO);
    assert(!(TIM2->DIER & TIM_IT_CC1));
    capture(500U, 0U); /* A late edge is ignored while capture IRQ is disabled. */
    now_ms = CAR_RANGE_INTERVAL_MS;
    CarApp_Poll();
    assert(trigger_count == 2U && TIM2->polarity == TIM_INPUTCHANNELPOLARITY_RISING);
    capture(1000U, 1U);
    capture(1020U, 0U);
    CarApp_Poll();
    assert(g_car_debug.range_status == CAR_RANGE_BAD_ECHO);

    reset();
    CarApp_Poll();
    capture(500U, 1U);
    now_ms = CAR_RANGE_TIMEOUT_MS;
    CarApp_Poll();
    assert(g_car_debug.range_status == CAR_RANGE_BAD_ECHO);

    reset();
    GPIOA->IDR = US_ECHO_Pin;
    CarApp_Poll();
    assert(trigger_count == 0U && g_car_debug.range_status == CAR_RANGE_BAD_ECHO);

    reset();
    CarApp_Poll();
    TIM2->flags = TIM_FLAG_CC1OF;
    capture(500U, 1U);
    CarApp_Poll();
    assert(g_car_debug.range_status == CAR_RANGE_BAD_ECHO);
    assert(!(TIM2->DIER & TIM_IT_CC1));
}

static void test_live_scheduler_and_emergency_stop(void)
{
    uint32_t observed_trigger = 0U, pulse_start_ms = 0U, i;
    reset();
    for (i = 0U; i < CAR_STARTUP_MS + 600U; ++i) {
        ++now_ms;
        if (observed_trigger && now_ms - pulse_start_ms == 1U) capture(65000U, 1U);
        if (observed_trigger && now_ms - pulse_start_ms == 7U) capture(5464U, 0U);
        CarApp_Poll();
        if (trigger_count != observed_trigger) {
            observed_trigger = trigger_count;
            pulse_start_ms = now_ms;
        }
    }
    assert(g_car_debug.state == CAR_FOLLOW);
    assert(g_car_debug.duty_left == CAR_CRUISE_DUTY);
    assert(TIM3->CCR1 == (uint32_t)CAR_CRUISE_DUTY * 3200U / 1000U);
    assert(TIM3->CCR2 == TIM3->CCR1);
    assert(g_car_debug.max_poll_gap_ms <= 1U);
    now_ms += CAR_CONTROL_MAX_GAP_MS + 1U;
    CarApp_Poll();
    assert(g_car_debug.state == CAR_CONTROL_FAULT);
    assert(TIM3->CCR1 == 0U && TIM3->CCR2 == 0U);
    assert(GPIOB->BRR == MOTOR_STBY_Pin);

    reset();
    htim3.Init.Period = 3599U;
    CarApp_Init();
    CarApp_Poll();
    assert(g_car_debug.state == CAR_CONTROL_FAULT && trigger_count == 0U);
}

static void test_ultrasonic_disabled(void)
{
    uint32_t i;
    reset();
    /* Stuck-high ECHO must not affect line following or cause any trigger. */
    GPIOA->IDR = US_ECHO_Pin;
    for (i = 0U; i < CAR_STARTUP_MS + 1000U; ++i) {
        ++now_ms;
        CarApp_Poll();
    }
    assert(g_car_debug.state == CAR_FOLLOW);
    assert(g_car_debug.duty_left == CAR_CRUISE_DUTY);
    assert(g_car_debug.duty_right == CAR_CRUISE_DUTY);
    assert(g_car_debug.range_status == CAR_RANGE_DISABLED);
    assert(g_car_debug.distance_mm == 0U);
    assert(trigger_count == 0U && counter_reads == 0U);
    assert(!(TIM2->DIER & TIM_IT_CC1));
    assert(!(GPIOA->ODR & US_TRIG_Pin));
    test_confirmed_mounting_and_motor_channels();
    test_live_scheduler_and_emergency_stop();
}

static void test_markers_disabled(void)
{
    reset();
    /* Auto-start on background, without ever presenting a start stripe. */
    run_app4(CAR_STARTUP_MS - 1U, 0U);
    assert(g_car_debug.state == CAR_STARTUP && TIM3->CCR1 == 0U && TIM3->CCR2 == 0U);
    run_app4(600U, 0U);
    assert(g_car_debug.state == CAR_FOLLOW);
    assert(g_car_debug.duty_left == CAR_CRUISE_DUTY && g_car_debug.duty_right == CAR_CRUISE_DUTY);
    run_app4(500U, 6U);
    assert(TIM3->CCR1 == TIM3->CCR2);
    run_app4(400U, 4U);
    assert(g_car_debug.duty_left == CAR_TURN_SLOW_DUTY && g_car_debug.duty_right == CAR_TURN_FAST_DUTY);
    assert_direction_pins();
    run_app4(400U, 12U);
    assert(g_car_debug.duty_left == CAR_SHARP_SLOW_DUTY && g_car_debug.duty_right == CAR_SHARP_FAST_DUTY);
    assert_direction_pins();
    run_app4(400U, 3U);
    assert(g_car_debug.duty_left == CAR_SHARP_FAST_DUTY && g_car_debug.duty_right == CAR_SHARP_SLOW_DUTY);
    assert_direction_pins();
    /* ALL-on, release, ALL-on must never stop or increment marker_count. */
    run_app4(1000U, 15U);
    run_app4(CAR_MARKER_RELEASE_MS + 500U, 0U);
    run_app4(1000U, 15U);
    assert(g_car_debug.state == CAR_FOLLOW && g_car_debug.marker_count == 0U);
    assert(g_car_debug.marker_armed == 0U);
    assert(g_car_debug.duty_left == CAR_CRUISE_DUTY && g_car_debug.duty_right == CAR_CRUISE_DUTY);
    now_ms += CAR_CONTROL_MAX_GAP_MS + 1U;
    CarApp_Poll();
    assert(g_car_debug.state == CAR_CONTROL_FAULT && TIM3->CCR1 == 0U && TIM3->CCR2 == 0U);
}

int main(void)
{
    if (CAR_MOTOR_TEST_MODE != 0U) {
        test_finite_motor_bench_mode();
        printf("PASS: finite motor bench mode %u, actual PWM/direction pins and fault stop\n",
               (unsigned)CAR_MOTOR_TEST_MODE);
        return 0;
    }
    if (!CAR_RACE_MARKERS_ENABLED) {
        test_markers_disabled();
        puts("PASS: marker-free auto-start, straight/turn/PWM, ALL-on never stops, fault stop preserved");
        return 0;
    }
    if (!test_recorded_white_paper_levels()) return 1;
    if (!CAR_ULTRASONIC_ENABLED) {
        test_ultrasonic_disabled();
        puts("PASS: ultrasonic disabled, no trigger/capture, line following/recovery/fault stop");
        return 0;
    }
    test_confirmed_mounting_and_motor_channels();
    test_wrapped_echo();
    test_timeouts_noise_and_rearm();
    test_live_scheduler_and_emergency_stop();
    puts("PASS: actual firmware adapter with simulated GPIO/timer interrupts");
    return 0;
}
