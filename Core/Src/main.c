/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file           : main.c
 * @brief          : Main program body
 *
 * CANbossTouch auf dem STM32H573I-DK mit gen4-FT813-70CTP-CLB (FT81x/EVE2
 * ueber SPI2). Das FT813 dient als "dummer Framebuffer": LVGL rendert in
 * SRAM-Streifenpuffer und der Flush-Callback schiebt die Rechtecke per SPI
 * in das 1-MiB-RAM_G, das eine statische Displayliste auf das 800x480-Panel
 * scannt. Touch kommt aus der kapazitiven Touch-Engine des FT813 (gepollt,
 * gleicher SPI-Bus, gleicher LVGL-Task). CANopen laeuft auf FDCAN2.
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
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "cmsis_os2.h"
#include "fdcan.h"
#include "icache.h"
#include "spi.h"
#include "tim.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "lvgl/lvgl.h"
#include "lvgl_port_touch.h"
#include "lvgl_port_display.h"
#include "ft81x.h"
#include "CO_app_STM32.h"
#include "canboss_ui.h"
#include "canboss_sdo.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */
/* CANopenNode-Instanz des Touchpanels (SDO-Client/NMT via FDCAN2, 1ms via TIM7) */
static CANopenNodeSTM32 canOpenNodeSTM32;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
void MX_FREERTOS_Init(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
 * @brief  The application entry point.
 * @retval int
 */
int main(void)
{
	/* USER CODE BEGIN 1 */

	/* USER CODE END 1 */

	/* MCU Configuration--------------------------------------------------------*/

	/* Reset of all peripherals, Initializes the Flash interface and the Systick. */
	HAL_Init();

	/* USER CODE BEGIN Init */

	/* USER CODE END Init */

	/* Configure the system clock */
	SystemClock_Config();

	/* USER CODE BEGIN SysInit */

	/* USER CODE END SysInit */

	/* Initialize all configured peripherals */
	MX_GPIO_Init();
	MX_ICACHE_Init();
	MX_SPI2_Init();
	MX_FDCAN2_Init();

	/* USER CODE BEGIN 2 */
	/* FT813 hochfahren: Panel-Timings, leere Displayliste, Backlight aus */
	if (ft81x_init() != FT81X_OK)
	{
		Error_Handler();
	}

	/* initialize LVGL framework */
	lv_init();
	lv_tick_set_cb(HAL_GetTick);

	/* initialize display and touchscreen (FT813: Framebuffer + Touch-Engine) */
	lvgl_display_init();
	lvgl_touchscreen_init();

	/* Framebuffer schwarz, statische Scanout-Displayliste, volle SPI-Rate,
	 * Backlight an — das Panel zeigt schwarz, bis LVGL das erste Bild malt */
	ft81x_clear_framebuffer(0x0000);
	ft81x_show_framebuffer();
	ft81x_set_spi_run_speed();
	ft81x_set_backlight(96);

	/* CANopen-Stack starten: FDCAN2 + TIM7 (1ms) */
	MX_TIM7_Init();
	canOpenNodeSTM32.CANHandle = &hfdcan2;
	canOpenNodeSTM32.HWInitFunction = MX_FDCAN2_Init;
	canOpenNodeSTM32.timerHandle = &htim7;
	canOpenNodeSTM32.desiredNodeID = CANBOSS_NODE_ID;
	canOpenNodeSTM32.baudrate = 500; /* kbit/s, muss zur FDCAN2-Bittiming-Konfiguration passen */
	canopen_app_init(&canOpenNodeSTM32);

	/* aus den EDS-Dateien generierte Bedienoberflaeche laden */
	canboss_ui_init();

	/* USER CODE END 2 */

	/* Init scheduler */
	osKernelInitialize();  /* Call init function for freertos objects (in freertos.c) */
	MX_FREERTOS_Init();

	/* Start scheduler */
	osKernelStart();

	/* We should never get here as control is now taken by the scheduler */
	/* Infinite loop */
	/* USER CODE BEGIN WHILE */
	while (1)
	{
		/* USER CODE END WHILE */

		/* USER CODE BEGIN 3 */
	}
	/* USER CODE END 3 */
}

/**
 * @brief System Clock Configuration
 *
 * HSE 25 MHz (Quarz X3 auf dem DK) -> PLL1 -> 250 MHz Sysclk bei
 * Voltage-Scale 0. PLL1Q liefert ebenfalls 250 MHz als SPI2-Kerneltakt;
 * FDCAN2 laeuft direkt vom 25-MHz-HSE (saubere CAN-Bittimings).
 *
 * @retval None
 */
void SystemClock_Config(void)
{
	RCC_OscInitTypeDef RCC_OscInitStruct = {0};
	RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

	/** Configure the main internal regulator output voltage
	 */
	if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE0) != HAL_OK)
	{
		Error_Handler();
	}

	/** Initializes the RCC Oscillators according to the specified parameters
	 * in the RCC_OscInitTypeDef structure.
	 */
	RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
	RCC_OscInitStruct.HSEState = RCC_HSE_ON;
	RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
	RCC_OscInitStruct.PLL.PLLSource = RCC_PLL1_SOURCE_HSE;
	RCC_OscInitStruct.PLL.PLLM = 5;   /* 25 MHz / 5 = 5 MHz Referenz */
	RCC_OscInitStruct.PLL.PLLN = 100; /* 500 MHz VCO */
	RCC_OscInitStruct.PLL.PLLP = 2;   /* 250 MHz Sysclk */
	RCC_OscInitStruct.PLL.PLLQ = 2;   /* 250 MHz SPI2-Kerneltakt */
	RCC_OscInitStruct.PLL.PLLR = 2;
	RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1_VCIRANGE_2;
	RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1_VCORANGE_WIDE;
	RCC_OscInitStruct.PLL.PLLFRACN = 0;
	if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
	{
		Error_Handler();
	}

	/** Initializes the CPU, AHB and APB buses clocks
	 */
	RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
			|RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
			|RCC_CLOCKTYPE_PCLK3;
	RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
	RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
	RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
	RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
	RCC_ClkInitStruct.APB3CLKDivider = RCC_HCLK_DIV1;

	if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK)
	{
		Error_Handler();
	}
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
 * @brief  Period elapsed callback in non blocking mode
 * @note   This function is called  when TIM2 interrupt took place, inside
 * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
 * a global variable "uwTick" used as application time base.
 * @param  htim : TIM handle
 * @retval None
 */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
	/* USER CODE BEGIN Callback 0 */

	/* USER CODE END Callback 0 */
	if (htim->Instance == TIM2) {
		HAL_IncTick();
	}
	/* USER CODE BEGIN Callback 1 */
	if (htim->Instance == TIM7) {
		canopen_app_interrupt(); /* CANopenNode 1ms-Verarbeitung (SYNC/PDO) */
	}
	/* USER CODE END Callback 1 */
}

/**
 * @brief  This function is executed in case of error occurrence.
 * @retval None
 */
void Error_Handler(void)
{
	/* USER CODE BEGIN Error_Handler_Debug */
	/* User can add his own implementation to report the HAL error return state */
	__disable_irq();
	while (1)
	{
	}
	/* USER CODE END Error_Handler_Debug */
}

#ifdef  USE_FULL_ASSERT
/**
 * @brief  Reports the name of the source file and the source line number
 *         where the assert_param error has occurred.
 * @param  file: pointer to the source file name
 * @param  line: assert_param error line source number
 * @retval None
 */
void assert_failed(uint8_t *file, uint32_t line)
{
	/* USER CODE BEGIN 6 */
	/* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
	/* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
