#include "car_app.h"
#include "car_config.h"
#include "main.h"
#include "tim.h"
#include <stdio.h>
#include <string.h>

#ifdef HAL_UART_MODULE_ENABLED
#include "usart.h"
static uint8_t g_uart_rx_byte;
static char g_uart_rx_buf[64];
static uint8_t g_uart_rx_idx = 0;
#endif

typedef enum { ECHO_IDLE, ECHO_RISE, ECHO_FALL, ECHO_DONE, ECHO_BAD } EchoPhase;

static CarControl control;
static volatile EchoPhase echo_phase;
static volatile uint16_t echo_rise;
static volatile uint16_t echo_width;
static uint32_t ping_since_ms;
static uint32_t line_since_ms;
static uint32_t last_poll_ms;
static uint8_t initialized;
volatile CarDebug g_car_debug;

void CarApp_EmergencyStop(void)
{
    /* Writing zero duty and STBY low removes motor drive; wheels can coast. */
    __HAL_RCC_GPIOB_CLK_ENABLE();
    GPIOB->BRR = MOTOR_STBY_Pin;
    TIM3->CCR1 = 0U;
    TIM3->CCR2 = 0U;
}

static uint32_t lock_irq(void)
{
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    return mask;
}

static void unlock_irq(uint32_t mask)
{
    __set_PRIMASK(mask);
}

static uint32_t motor_direction_bits(uint16_t in1, uint16_t in2, int16_t duty, uint8_t forward_in1)
{
     uint8_t first_high = (duty > 0) == (forward_in1 != 0U);
    /* With the other channel running (STBY high), short-brake this wheel.
     * TB6612 IN1=IN2=HIGH brakes regardless of PWM. Both-zero uses STBY low. */
    if (duty == 0) return in1 | in2;
    if (first_high) return in1 | ((uint32_t)in2 << 16);
    return in2 | ((uint32_t)in1 << 16);
}

static uint32_t pwm_value(int16_t duty, uint32_t trim)
{
    uint32_t amount = duty < 0 ? (uint32_t)-duty : (uint32_t)duty;
    amount = amount * trim / 1000U;
    if (amount > CAR_MAX_DUTY) amount = CAR_MAX_DUTY;
    return amount * (htim3.Init.Period + 1U) / 1000U;
}

static void motor_apply(void)
{
    int16_t left = control.left_wheel.value;
    int16_t right = control.right_wheel.value;
    if (left == 0 && right == 0) {
        CarApp_EmergencyStop();
        GPIOB->BSRR = ((uint32_t)(MOTOR_AIN1_Pin | MOTOR_AIN2_Pin |
                                  MOTOR_BIN1_Pin | MOTOR_BIN2_Pin) << 16);
        return;
    }
    /* The controller guarantees every reversal passes through zero + deadtime. */
    /* Commit both channels' direction pins together. A=left, B=right. */
    GPIOB->BSRR = motor_direction_bits(MOTOR_AIN1_Pin, MOTOR_AIN2_Pin, left, CAR_LEFT_FORWARD_IN1) |
                  motor_direction_bits(MOTOR_BIN1_Pin, MOTOR_BIN2_Pin, right, CAR_RIGHT_FORWARD_IN1);
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, pwm_value(left, CAR_LEFT_TRIM_PERMILLE));
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_2, pwm_value(right, CAR_RIGHT_TRIM_PERMILLE));
    HAL_GPIO_WritePin(MOTOR_STBY_GPIO_Port, MOTOR_STBY_Pin, GPIO_PIN_SET);
}

static void start_ping(uint32_t now)
{
    uint32_t mask;
    uint16_t start_us;
    ping_since_ms = now;
    if (HAL_GPIO_ReadPin(US_ECHO_GPIO_Port, US_ECHO_Pin) == GPIO_PIN_SET) {
        CarControl_ObserveRange(&control, CAR_RANGE_BAD_ECHO, 0U, now);
        return;
    }
    mask = lock_irq();
    __HAL_TIM_DISABLE_IT(&htim2, TIM_IT_CC1);
    __HAL_TIM_SET_CAPTUREPOLARITY(&htim2, TIM_CHANNEL_1, TIM_INPUTCHANNELPOLARITY_RISING);
    __HAL_TIM_CLEAR_FLAG(&htim2, TIM_FLAG_CC1 | TIM_FLAG_CC1OF);
    echo_phase = ECHO_RISE;
    __HAL_TIM_ENABLE_IT(&htim2, TIM_IT_CC1);
    unlock_irq(mask);
    HAL_GPIO_WritePin(US_TRIG_GPIO_Port, US_TRIG_Pin, GPIO_PIN_SET);
    start_us = (uint16_t)__HAL_TIM_GET_COUNTER(&htim2);
    /* The only bounded busy wait: a 12us trigger, not an echo/distance delay. */
    while ((uint16_t)((uint16_t)__HAL_TIM_GET_COUNTER(&htim2) - start_us) < 12U) { }
    HAL_GPIO_WritePin(US_TRIG_GPIO_Port, US_TRIG_Pin, GPIO_PIN_RESET);
}

static void poll_range(uint32_t now)
{
    uint32_t mask = lock_irq();
    CarRangeStatus status = CAR_RANGE_NONE;
    uint16_t width = 0U;
    uint16_t distance = 0U;
    if (echo_phase == ECHO_DONE || echo_phase == ECHO_BAD) {
        width = echo_width;
        status = echo_phase == ECHO_DONE ? CAR_RANGE_VALID : CAR_RANGE_BAD_ECHO;
        echo_phase = ECHO_IDLE;
    } else if ((echo_phase == ECHO_RISE || echo_phase == ECHO_FALL) &&
               (uint32_t)(now - ping_since_ms) >= CAR_RANGE_TIMEOUT_MS) {
        status = echo_phase == ECHO_RISE ? CAR_RANGE_NO_ECHO : CAR_RANGE_BAD_ECHO;
        __HAL_TIM_DISABLE_IT(&htim2, TIM_IT_CC1);
        __HAL_TIM_SET_CAPTUREPOLARITY(&htim2, TIM_CHANNEL_1, TIM_INPUTCHANNELPOLARITY_RISING);
        __HAL_TIM_CLEAR_FLAG(&htim2, TIM_FLAG_CC1 | TIM_FLAG_CC1OF);
        echo_phase = ECHO_IDLE;
    }
    unlock_irq(mask);
    if (status == CAR_RANGE_VALID) {
        g_car_debug.echo_us = width;
        if (!CarControl_DistanceFromPulse(width, &distance)) status = CAR_RANGE_BAD_ECHO;
    }
    if (status != CAR_RANGE_NONE) CarControl_ObserveRange(&control, status, distance, now);
    if (echo_phase == ECHO_IDLE && (uint32_t)(now - ping_since_ms) >= CAR_RANGE_INTERVAL_MS) {
        start_ping(now);
    }
}

void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *timer)
{
#if CAR_ULTRASONIC_ENABLED
    uint16_t captured;
    if (timer->Instance != TIM2 || timer->Channel != HAL_TIM_ACTIVE_CHANNEL_1) return;
    captured = (uint16_t)HAL_TIM_ReadCapturedValue(timer, TIM_CHANNEL_1);
    if (__HAL_TIM_GET_FLAG(timer, TIM_FLAG_CC1OF) != RESET) {
        __HAL_TIM_CLEAR_FLAG(timer, TIM_FLAG_CC1OF);
        __HAL_TIM_DISABLE_IT(timer, TIM_IT_CC1);
        echo_phase = ECHO_BAD;
        return;
    }
    if (echo_phase == ECHO_RISE) {
        echo_rise = captured;
        __HAL_TIM_SET_CAPTUREPOLARITY(timer, TIM_CHANNEL_1, TIM_INPUTCHANNELPOLARITY_FALLING);
        echo_phase = ECHO_FALL;
    } else if (echo_phase == ECHO_FALL) {
        /* 16-bit subtraction works even when the free-running timer wraps. */
        echo_width = (uint16_t)(captured - echo_rise);
        __HAL_TIM_DISABLE_IT(timer, TIM_IT_CC1);
        echo_phase = ECHO_DONE;
    }
#else
    (void)timer;
#endif
}

void CarApp_Init(void)
{
    uint32_t now = HAL_GetTick();
    initialized = 0U;
    CarApp_EmergencyStop();
    CarControl_Init(&control, now);
    echo_phase = ECHO_IDLE;
    line_since_ms = now;
    last_poll_ms = now;
    ping_since_ms = now - CAR_RANGE_INTERVAL_MS;
    /* Guard the microsecond trigger loop against an incompatible clock edit. */
    if (HAL_RCC_GetHCLKFreq() != 64000000U || HAL_RCC_GetPCLK1Freq() != 32000000U ||
        (CAR_ULTRASONIC_ENABLED && (htim2.Init.Prescaler != 63U ||
         htim2.Init.Period != 65535U)) || htim3.Init.Period != 3199U ||
        htim3.Init.Prescaler != 0U) {
        CarControl_Trip(&control, now);
        g_car_debug.state = CAR_CONTROL_FAULT;
        return;
    }
    HAL_GPIO_WritePin(US_TRIG_GPIO_Port, US_TRIG_Pin, GPIO_PIN_RESET);
    if (CAR_ULTRASONIC_ENABLED &&
        HAL_TIM_IC_Start_IT(&htim2, TIM_CHANNEL_1) != HAL_OK) Error_Handler();
    __HAL_TIM_DISABLE_IT(&htim2, TIM_IT_CC1);
    __HAL_TIM_CLEAR_FLAG(&htim2, TIM_FLAG_CC1 | TIM_FLAG_CC1OF);
    if (HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1) != HAL_OK ||
        HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_2) != HAL_OK) Error_Handler();

#ifdef HAL_UART_MODULE_ENABLED
    HAL_UART_Receive_IT(&huart1, &g_uart_rx_byte, 1);
#endif

    initialized = 1U;
}

void CarApp_Poll(void)
{
    uint32_t now = HAL_GetTick();
    uint32_t gap = (uint32_t)(now - last_poll_ms);
    if (!initialized) return;
    last_poll_ms = now;
    if (gap > g_car_debug.max_poll_gap_ms) {
        g_car_debug.max_poll_gap_ms = gap > 65535U ? 65535U : (uint16_t)gap;
    }
    if (gap > CAR_CONTROL_MAX_GAP_MS) CarControl_Trip(&control, now);
#ifdef HAL_UART_MODULE_ENABLED
    if (huart1.RxState != HAL_UART_STATE_BUSY_RX) {
        __HAL_UART_CLEAR_PEFLAG(&huart1);
        __HAL_UART_CLEAR_FEFLAG(&huart1);
        __HAL_UART_CLEAR_NEFLAG(&huart1);
        __HAL_UART_CLEAR_OREFLAG(&huart1);
        (void)huart1.Instance->SR;
        (void)huart1.Instance->DR;
        huart1.ErrorCode = HAL_UART_ERROR_NONE;
        huart1.RxState = HAL_UART_STATE_READY;
        HAL_UART_Receive_IT(&huart1, &g_uart_rx_byte, 1);
    }
#endif
#ifdef HAL_UART_MODULE_ENABLED
    if (huart1.RxState != HAL_UART_STATE_BUSY_RX) {
        __HAL_UART_CLEAR_PEFLAG(&huart1);
        __HAL_UART_CLEAR_FEFLAG(&huart1);
        __HAL_UART_CLEAR_NEFLAG(&huart1);
        __HAL_UART_CLEAR_OREFLAG(&huart1);
        (void)huart1.Instance->SR;
        (void)huart1.Instance->DR;
        huart1.ErrorCode = HAL_UART_ERROR_NONE;
        huart1.RxState = HAL_UART_STATE_READY;
        HAL_UART_Receive_IT(&huart1, &g_uart_rx_byte, 1);
    }
#endif
    if (CAR_ULTRASONIC_ENABLED) poll_range(now);
    if ((uint32_t)(now - line_since_ms) >= CAR_LINE_SAMPLE_MS) {
        line_since_ms = now;
        g_car_debug.raw_left_level = HAL_GPIO_ReadPin(IR_LEFT_GPIO_Port, IR_LEFT_Pin) == GPIO_PIN_SET;
        g_car_debug.raw_right_level = HAL_GPIO_ReadPin(IR_RIGHT_GPIO_Port, IR_RIGHT_Pin) == GPIO_PIN_SET;
        g_car_debug.raw_left_outer_level = HAL_GPIO_ReadPin(IR_LEFT_OUTER_GPIO_Port, IR_LEFT_OUTER_Pin) == GPIO_PIN_SET;
        g_car_debug.raw_right_outer_level = HAL_GPIO_ReadPin(IR_RIGHT_OUTER_GPIO_Port, IR_RIGHT_OUTER_Pin) == GPIO_PIN_SET;
        CarControl_SampleLine4(&control,
            (g_car_debug.raw_left_outer_level == CAR_LEFT_OUTER_BLACK_LEVEL) == CAR_TRACK_DETECT_BLACK,
            (g_car_debug.raw_left_level == CAR_LEFT_BLACK_LEVEL) == CAR_TRACK_DETECT_BLACK,
            (g_car_debug.raw_right_level == CAR_RIGHT_BLACK_LEVEL) == CAR_TRACK_DETECT_BLACK,
            (g_car_debug.raw_right_outer_level == CAR_RIGHT_OUTER_BLACK_LEVEL) == CAR_TRACK_DETECT_BLACK);
    }
    CarControl_Step(&control, now);
    motor_apply();
    g_car_debug.uptime_ms = now;
    g_car_debug.state = control.state;
    g_car_debug.range_status = control.range_status;
    g_car_debug.distance_mm = control.range_mm;
    g_car_debug.left_black = control.left_line.stable == CAR_TRACK_DETECT_BLACK;
    g_car_debug.right_black = control.right_line.stable == CAR_TRACK_DETECT_BLACK;
    g_car_debug.left_outer_black = control.left_outer_line.stable == CAR_TRACK_DETECT_BLACK;
    g_car_debug.right_outer_black = control.right_outer_line.stable == CAR_TRACK_DETECT_BLACK;
    g_car_debug.line_mask = control.line_mask;
    g_car_debug.marker_count = control.marker_count;
    g_car_debug.marker_armed = control.marker_armed;
    g_car_debug.target_left = control.target_left;
    g_car_debug.target_right = control.target_right;
    g_car_debug.duty_left = control.left_wheel.value;
    g_car_debug.duty_right = control.right_wheel.value;
}

void CarApp_ProcessCommand(const char *cmd)
{
    uint32_t now = HAL_GetTick();
    if (cmd == NULL) return;
    while (*cmd == ' ' || *cmd == '\t' || *cmd == '\r' || *cmd == '\n') ++cmd;
    if (*cmd == '\0') return;

    if (strncmp(cmd, "STOP", 4) == 0) {
        CarControl_Stop(&control, now);
    } else if (strncmp(cmd, "MODE,AUTO", 9) == 0) {
        CarControl_StartAuto(&control, now);
    } else if (strncmp(cmd, "MODE,RACE", 9) == 0) {
        CarControl_StartRace(&control, now);
    } else if (strncmp(cmd, "MODE,MANUAL", 11) == 0) {
        CarControl_StartManual(&control, now);
    } else if (strncmp(cmd, "MANUAL,", 7) == 0) {
        int forward = 0, turn = 0;
        if (sscanf(cmd + 7, "%d,%d", &forward, &turn) == 2) {
            CarControl_SetManual(&control, (int16_t)forward, (int16_t)turn, now);
        }
    }
}

#ifdef HAL_UART_MODULE_ENABLED
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1) {
        char ch = (char)g_uart_rx_byte;
        if (ch == '\r' || ch == '\n') {
            if (g_uart_rx_idx > 0) {
                g_uart_rx_buf[g_uart_rx_idx] = '\0';
                CarApp_ProcessCommand(g_uart_rx_buf);
                g_uart_rx_idx = 0;
            }
        } else if (g_uart_rx_idx < sizeof(g_uart_rx_buf) - 1) {
            g_uart_rx_buf[g_uart_rx_idx++] = ch;
        }
        HAL_UART_Receive_IT(&huart1, &g_uart_rx_byte, 1);
    }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1) {
        __HAL_UART_CLEAR_OREFLAG(huart);
        HAL_UART_Receive_IT(&huart1, &g_uart_rx_byte, 1);
    }
}
#endif
