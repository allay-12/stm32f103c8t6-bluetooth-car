#ifndef TEST_MAIN_H
#define TEST_MAIN_H
#include <stdint.h>

typedef struct { uint32_t IDR, ODR, BSRR, BRR; } GPIO_TypeDef;
typedef struct { uint32_t CCR1, CCR2, CNT, DIER, flags, polarity; } TIM_TypeDef;
typedef struct {
    TIM_TypeDef *Instance;
    uint32_t Channel;
    struct { uint32_t Prescaler, Period; } Init;
} TIM_HandleTypeDef;
typedef enum { GPIO_PIN_RESET = 0, GPIO_PIN_SET = 1 } GPIO_PinState;
typedef enum { HAL_OK = 0, HAL_ERROR = 1 } HAL_StatusTypeDef;

extern GPIO_TypeDef fake_gpioa, fake_gpiob;
extern TIM_TypeDef fake_tim2, fake_tim3;
#define GPIOA (&fake_gpioa)
#define GPIOB (&fake_gpiob)
#define TIM2 (&fake_tim2)
#define TIM3 (&fake_tim3)
#define US_ECHO_Pin (1U << 0)
#define US_ECHO_GPIO_Port GPIOA
#define US_TRIG_Pin (1U << 1)
#define US_TRIG_GPIO_Port GPIOA
#define IR_LEFT_Pin (1U << 0)
#define IR_LEFT_GPIO_Port GPIOB
#define IR_RIGHT_Pin (1U << 1)
#define IR_RIGHT_GPIO_Port GPIOB
#define IR_LEFT_OUTER_Pin (1U << 6)
#define IR_LEFT_OUTER_GPIO_Port GPIOB
#define IR_RIGHT_OUTER_Pin (1U << 7)
#define IR_RIGHT_OUTER_GPIO_Port GPIOB
#define MOTOR_STBY_Pin (1U << 5)
#define MOTOR_STBY_GPIO_Port GPIOB
#define MOTOR_AIN1_Pin (1U << 12)
#define MOTOR_AIN2_Pin (1U << 13)
#define MOTOR_BIN1_Pin (1U << 14)
#define MOTOR_BIN2_Pin (1U << 15)
#define TIM_CHANNEL_1 0U
#define TIM_CHANNEL_2 4U
#define HAL_TIM_ACTIVE_CHANNEL_1 1U
#define TIM_IT_CC1 2U
#define TIM_FLAG_CC1 2U
#define TIM_FLAG_CC1OF 512U
#define TIM_INPUTCHANNELPOLARITY_RISING 0U
#define TIM_INPUTCHANNELPOLARITY_FALLING 2U
#define RESET 0U
#define __HAL_RCC_GPIOB_CLK_ENABLE() ((void)0)
#define __HAL_TIM_DISABLE_IT(t, f) ((t)->Instance->DIER &= ~(f))
#define __HAL_TIM_ENABLE_IT(t, f) ((t)->Instance->DIER |= (f))
#define __HAL_TIM_CLEAR_FLAG(t, f) ((t)->Instance->flags &= ~(f))
#define __HAL_TIM_GET_FLAG(t, f) ((t)->Instance->flags & (f))
#define __HAL_TIM_SET_CAPTUREPOLARITY(t, ch, p) ((void)(ch), (t)->Instance->polarity = (p))
#define __HAL_TIM_GET_COUNTER(t) Fake_GetCounter(t)
#define __HAL_TIM_SET_COMPARE(t, ch, v) Fake_SetCompare(t, ch, v)

uint32_t __get_PRIMASK(void);
void __disable_irq(void);
void __set_PRIMASK(uint32_t mask);
uint32_t HAL_GetTick(void);
uint32_t HAL_RCC_GetPCLK1Freq(void);
uint32_t HAL_RCC_GetHCLKFreq(void);
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *gpio, uint16_t pin);
void HAL_GPIO_WritePin(GPIO_TypeDef *gpio, uint16_t pin, GPIO_PinState value);
uint32_t HAL_TIM_ReadCapturedValue(TIM_HandleTypeDef *timer, uint32_t channel);
HAL_StatusTypeDef HAL_TIM_IC_Start_IT(TIM_HandleTypeDef *timer, uint32_t channel);
HAL_StatusTypeDef HAL_TIM_PWM_Start(TIM_HandleTypeDef *timer, uint32_t channel);
uint32_t Fake_GetCounter(TIM_HandleTypeDef *timer);
void Fake_SetCompare(TIM_HandleTypeDef *timer, uint32_t channel, uint32_t value);
void HAL_TIM_IC_CaptureCallback(TIM_HandleTypeDef *timer);
void Error_Handler(void);
#endif
