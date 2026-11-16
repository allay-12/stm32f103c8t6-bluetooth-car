/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f1xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define US_ECHO_Pin GPIO_PIN_0
#define US_ECHO_GPIO_Port GPIOA
#define US_TRIG_Pin GPIO_PIN_1
#define US_TRIG_GPIO_Port GPIOA
#define MOTOR_LEFT_PWM_Pin GPIO_PIN_6
#define MOTOR_LEFT_PWM_GPIO_Port GPIOA
#define MOTOR_RIGHT_PWM_Pin GPIO_PIN_7
#define MOTOR_RIGHT_PWM_GPIO_Port GPIOA
#define IR_LEFT_Pin GPIO_PIN_0
#define IR_LEFT_GPIO_Port GPIOB
#define IR_RIGHT_Pin GPIO_PIN_1
#define IR_RIGHT_GPIO_Port GPIOB
#define MOTOR_AIN1_Pin GPIO_PIN_12
#define MOTOR_AIN1_GPIO_Port GPIOB
#define MOTOR_AIN2_Pin GPIO_PIN_13
#define MOTOR_AIN2_GPIO_Port GPIOB
#define MOTOR_BIN1_Pin GPIO_PIN_14
#define MOTOR_BIN1_GPIO_Port GPIOB
#define MOTOR_BIN2_Pin GPIO_PIN_15
#define MOTOR_BIN2_GPIO_Port GPIOB
#define MOTOR_STBY_Pin GPIO_PIN_5
#define MOTOR_STBY_GPIO_Port GPIOB
#define IR_LEFT_OUTER_Pin GPIO_PIN_6
#define IR_LEFT_OUTER_GPIO_Port GPIOB
#define IR_RIGHT_OUTER_Pin GPIO_PIN_7
#define IR_RIGHT_OUTER_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
