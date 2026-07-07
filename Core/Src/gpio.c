/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    gpio.c
  * @brief   This file provides code for the configuration
  *          of all used GPIO pins.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2024 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "gpio.h"

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/*----------------------------------------------------------------------------*/
/* Configure GPIO                                                             */
/*----------------------------------------------------------------------------*/
/* USER CODE BEGIN 1 */

/* USER CODE END 1 */

/**
  * FT813-Steuerpins am Arduino-Header:
  *   PA3 = D10 = /CS (Software-Chip-Select, idle high)
  *   PA8 = D9  = PD  (Power-Down, aktiv low — beim Start low = Reset)
  */
void MX_GPIO_Init(void)
{
	GPIO_InitTypeDef GPIO_InitStruct = {0};

	/* GPIO Ports Clock Enable */
	__HAL_RCC_GPIOA_CLK_ENABLE();
	__HAL_RCC_GPIOB_CLK_ENABLE();
	__HAL_RCC_GPIOI_CLK_ENABLE();

	/* FT813 /CS: idle high, PD: low (Display im Reset bis ft81x_init) */
	HAL_GPIO_WritePin(FT813_CS_GPIO_Port, FT813_CS_Pin, GPIO_PIN_SET);
	HAL_GPIO_WritePin(FT813_PD_GPIO_Port, FT813_PD_Pin, GPIO_PIN_RESET);

	GPIO_InitStruct.Pin = FT813_CS_Pin;
	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
	HAL_GPIO_Init(FT813_CS_GPIO_Port, &GPIO_InitStruct);

	GPIO_InitStruct.Pin = FT813_PD_Pin;
	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(FT813_PD_GPIO_Port, &GPIO_InitStruct);
}

/* USER CODE BEGIN 2 */

/* USER CODE END 2 */
