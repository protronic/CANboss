/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   CANbossTouch auf STM32H573I-DK + gen4-FT813-70CTP-CLB.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2023 STMicroelectronics.
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
#include "stm32h5xx_hal.h"

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
/*
 * gen4-FT813-70CTP-CLB am Arduino-Header des STM32H573I-DK
 * (Pinmap wie examples/gen4-ft813-70ctp-h573i-dk im embassy-Repo):
 *
 *   SCK  = D13 = PI1  (SPI2_SCK)     MISO = D12 = PI2  (SPI2_MISO)
 *   MOSI = D11 = PB15 (SPI2_MOSI)    /CS  = D10 = PA3  (GPIO)
 *   PD   = D9  = PA8  (GPIO)         INT  = D8  = PG8  (unbenutzt, Touch wird gepollt)
 *
 * FDCAN2 ebenfalls am Arduino-Header, das DK hat keinen eigenen
 * CAN-Transceiver (extern 3.3V, z.B. SN65HVD230 / TJA1051T/3):
 *
 *   CAN RX = D3  = PB5 (FDCAN2_RX)   CAN TX = D15 = PB6 (FDCAN2_TX)
 */
#define FT813_CS_Pin GPIO_PIN_3
#define FT813_CS_GPIO_Port GPIOA
#define FT813_PD_Pin GPIO_PIN_8
#define FT813_PD_GPIO_Port GPIOA

/* USER CODE BEGIN Private defines */
#ifndef CANBOSS_NODE_ID
#define CANBOSS_NODE_ID 127 /* CANopen-Node-ID des Touchpanels */
#endif
/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
